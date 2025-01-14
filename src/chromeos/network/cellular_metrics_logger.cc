// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/cellular_metrics_logger.h"

#include "base/metrics/histogram_macros.h"
#include "base/task/post_task.h"
#include "chromeos/components/feature_usage/feature_usage_metrics.h"
#include "chromeos/dbus/hermes/hermes_manager_client.h"
#include "chromeos/network/cellular_esim_profile.h"
#include "chromeos/network/cellular_esim_profile_handler.h"
#include "chromeos/network/cellular_utils.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_event_log.h"
#include "chromeos/network/network_state_handler.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

const char kESimUMAFeatureName[] = "ESim";

// Checks whether the current logged in user type is an owner or regular.
bool IsLoggedInUserOwnerOrRegular() {
  if (!LoginState::IsInitialized())
    return false;

  LoginState::LoggedInUserType user_type =
      LoginState::Get()->GetLoggedInUserType();
  return user_type == LoginState::LoggedInUserType::LOGGED_IN_USER_OWNER ||
         user_type == LoginState::LoggedInUserType::LOGGED_IN_USER_REGULAR;
}

SimType GetSimType(const NetworkState* network) {
  return network->eid().empty() ? SimType::kPSim : SimType::kESim;
}

}  // namespace

// static
const char CellularMetricsLogger::kSimPinLockSuccessHistogram[] =
    "Network.Cellular.Pin.LockSuccess";

// static
const char CellularMetricsLogger::kSimPinUnlockSuccessHistogram[] =
    "Network.Cellular.Pin.UnlockSuccess";

// static
const char CellularMetricsLogger::kSimPinUnblockSuccessHistogram[] =
    "Network.Cellular.Pin.UnblockSuccess";

// static
const char CellularMetricsLogger::kSimPinChangeSuccessHistogram[] =
    "Network.Cellular.Pin.ChangeSuccess";

// static
const base::TimeDelta CellularMetricsLogger::kInitializationTimeout =
    base::TimeDelta::FromSeconds(15);

// static
const base::TimeDelta CellularMetricsLogger::kDisconnectRequestTimeout =
    base::TimeDelta::FromSeconds(5);

// static
CellularMetricsLogger::SimPinOperationResult
CellularMetricsLogger::GetSimPinOperationResultForShillError(
    const std::string& shill_error_name) {
  if (shill_error_name == shill::kErrorResultFailure ||
      shill_error_name == shill::kErrorResultInvalidArguments) {
    return SimPinOperationResult::kErrorFailure;
  }
  if (shill_error_name == shill::kErrorResultNotSupported)
    return SimPinOperationResult::kErrorNotSupported;
  if (shill_error_name == shill::kErrorResultIncorrectPin)
    return SimPinOperationResult::kErrorIncorrectPin;
  if (shill_error_name == shill::kErrorResultPinBlocked)
    return SimPinOperationResult::kErrorPinBlocked;
  if (shill_error_name == shill::kErrorResultPinRequired)
    return SimPinOperationResult::kErrorPinRequired;
  if (shill_error_name == shill::kErrorResultNotFound)
    return SimPinOperationResult::kErrorDeviceMissing;
  return SimPinOperationResult::kErrorUnknown;
}

// static
void CellularMetricsLogger::RecordSimPinOperationResult(
    const SimPinOperation& pin_operation,
    const base::Optional<std::string>& shill_error_name) {
  SimPinOperationResult result =
      shill_error_name.has_value()
          ? GetSimPinOperationResultForShillError(*shill_error_name)
          : SimPinOperationResult::kSuccess;

  switch (pin_operation) {
    case SimPinOperation::kLock:
      UMA_HISTOGRAM_ENUMERATION(kSimPinLockSuccessHistogram, result);
      return;
    case SimPinOperation::kUnlock:
      UMA_HISTOGRAM_ENUMERATION(kSimPinUnlockSuccessHistogram, result);
      return;
    case SimPinOperation::kUnblock:
      UMA_HISTOGRAM_ENUMERATION(kSimPinUnblockSuccessHistogram, result);
      return;
    case SimPinOperation::kChange:
      UMA_HISTOGRAM_ENUMERATION(kSimPinChangeSuccessHistogram, result);
      return;
  }
}

// static
void CellularMetricsLogger::RegisterLocalStatePrefs(
    PrefRegistrySimple* registry) {
  feature_usage::FeatureUsageMetrics::RegisterPref(registry,
                                                   kESimUMAFeatureName);
}

// Reports daily ESim Standard Feature Usage Logging metrics. Note that
// if an object of this type is destroyed and created in the same day,
// metrics eligibility and enablement will only be reported once. Registers
// to local state prefs instead of profile prefs as cellular network is
// available to anyone using the device, as opposed to per profile basis.
class ESimFeatureUsageMetrics
    : public feature_usage::FeatureUsageMetrics::Delegate {
 public:
  explicit ESimFeatureUsageMetrics(PrefService* device_prefs) {
    DCHECK(device_prefs);
    feature_usage_metrics_ =
        std::make_unique<feature_usage::FeatureUsageMetrics>(
            kESimUMAFeatureName, device_prefs, this);
  }

  ~ESimFeatureUsageMetrics() override = default;

  // feature_usage::FeatureUsageMetrics::Delegate:
  bool IsEligible() const final {
    // If the device is eligible to use ESim.
    return HermesManagerClient::Get()->GetAvailableEuiccs().size() != 0;
  }

  // feature_usage::FeatureUsageMetrics::Delegate:
  bool IsEnabled() const final {
    // If there are installed ESim profiles.
    for (const auto& profile : GenerateProfilesFromHermes()) {
      if (profile.state() == CellularESimProfile::State::kActive ||
          profile.state() == CellularESimProfile::State::kInactive) {
        return true;
      }
    }
    return false;
  }

  // Should be called after an attempt to connect to an ESim profile.
  void RecordUsage(bool success) const {
    feature_usage_metrics_->RecordUsage(success);
  }

 private:
  std::unique_ptr<feature_usage::FeatureUsageMetrics> feature_usage_metrics_;
};

CellularMetricsLogger::ConnectResult
CellularMetricsLogger::NetworkConnectionErrorToConnectResult(
    const std::string& error_name) {
  if (error_name == NetworkConnectionHandler::kErrorNotFound)
    return CellularMetricsLogger::ConnectResult::kInvalidGuid;

  if (error_name == NetworkConnectionHandler::kErrorConnected ||
      error_name == NetworkConnectionHandler::kErrorConnecting) {
    return CellularMetricsLogger::ConnectResult::kInvalidState;
  }

  if (error_name == NetworkConnectionHandler::kErrorConnectCanceled)
    return CellularMetricsLogger::ConnectResult::kCanceled;

  if (error_name == NetworkConnectionHandler::kErrorPassphraseRequired ||
      error_name == NetworkConnectionHandler::kErrorBadPassphrase ||
      error_name == NetworkConnectionHandler::kErrorCertificateRequired ||
      error_name == NetworkConnectionHandler::kErrorConfigurationRequired ||
      error_name == NetworkConnectionHandler::kErrorAuthenticationRequired ||
      error_name == NetworkConnectionHandler::kErrorCertLoadTimeout ||
      error_name == NetworkConnectionHandler::kErrorConfigureFailed) {
    return CellularMetricsLogger::ConnectResult::kNotConfigured;
  }

  if (error_name == NetworkConnectionHandler::kErrorBlockedByPolicy)
    return CellularMetricsLogger::ConnectResult::kBlocked;

  return CellularMetricsLogger::ConnectResult::kUnknown;
}

void CellularMetricsLogger::LogCellularConnectionSuccessHistogram(
    CellularMetricsLogger::ConnectResult start_connect_result,
    SimType sim_type) {
  if (sim_type == SimType::kPSim) {
    UMA_HISTOGRAM_ENUMERATION("Network.Cellular.PSim.ConnectionSuccess",
                              start_connect_result);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Network.Cellular.ESim.ConnectionSuccess",
                              start_connect_result);

    // |esim_feature_usage_metrics_| may not have been created yet.
    if (!esim_feature_usage_metrics_.get())
      return;

    esim_feature_usage_metrics_
        ->RecordUsage(/*success=*/
                      start_connect_result ==
                      CellularMetricsLogger::ConnectResult::kSuccess);
  }
}

CellularMetricsLogger::ConnectionInfo::ConnectionInfo(
    const std::string& network_guid,
    bool is_connected,
    bool is_connecting)
    : network_guid(network_guid),
      is_connected(is_connected),
      is_connecting(is_connecting) {}

CellularMetricsLogger::ConnectionInfo::ConnectionInfo(
    const std::string& network_guid)
    : network_guid(network_guid) {}

CellularMetricsLogger::ConnectionInfo::~ConnectionInfo() = default;

CellularMetricsLogger::CellularMetricsLogger() = default;

CellularMetricsLogger::~CellularMetricsLogger() {
  if (network_state_handler_)
    OnShuttingDown();

  if (initialized_) {
    if (LoginState::IsInitialized())
      LoginState::Get()->RemoveObserver(this);

    if (network_connection_handler_)
      network_connection_handler_->RemoveObserver(this);
  }
}

void CellularMetricsLogger::Init(
    NetworkStateHandler* network_state_handler,
    NetworkConnectionHandler* network_connection_handler,
    CellularESimProfileHandler* cellular_esim_profile_handler) {
  network_state_handler_ = network_state_handler;
  cellular_esim_profile_handler_ = cellular_esim_profile_handler;
  network_state_handler_->AddObserver(this, FROM_HERE);

  if (network_connection_handler) {
    network_connection_handler_ = network_connection_handler;
    network_connection_handler_->AddObserver(this);
  }

  if (LoginState::IsInitialized())
    LoginState::Get()->AddObserver(this);

  // Devices and networks may already be present before this method is called.
  // Make sure that lists and timers are initialized properly.
  DeviceListChanged();
  NetworkListChanged();
  initialized_ = true;
}

void CellularMetricsLogger::DeviceListChanged() {
  NetworkStateHandler::DeviceStateList device_list;
  network_state_handler_->GetDeviceListByType(NetworkTypePattern::Cellular(),
                                              &device_list);
  bool new_is_cellular_available = !device_list.empty();
  if (is_cellular_available_ == new_is_cellular_available)
    return;

  is_cellular_available_ = new_is_cellular_available;
  // Start a timer to wait for cellular networks to initialize.
  // This makes sure that intermediate not-connected states are
  // not logged before initialization is completed.
  if (is_cellular_available_) {
    initialization_timer_.Start(
        FROM_HERE, kInitializationTimeout, this,
        &CellularMetricsLogger::OnInitializationTimeout);
  }
}

void CellularMetricsLogger::NetworkListChanged() {
  base::flat_map<std::string, std::unique_ptr<ConnectionInfo>>
      old_connection_info_map;
  // Clear |guid_to_connection_info_map| so that only new and existing
  // networks are added back to it.
  old_connection_info_map.swap(guid_to_connection_info_map_);

  NetworkStateHandler::NetworkStateList network_list;
  network_state_handler_->GetVisibleNetworkListByType(
      NetworkTypePattern::Cellular(), &network_list);

  // Check the current cellular networks list and copy existing connection info
  // from old map to new map or create new ones if it does not exist.
  for (const auto* network : network_list) {
    const std::string& guid = network->guid();
    auto old_connection_info_map_iter = old_connection_info_map.find(guid);
    if (old_connection_info_map_iter != old_connection_info_map.end()) {
      guid_to_connection_info_map_.insert_or_assign(
          guid, std::move(old_connection_info_map_iter->second));
      old_connection_info_map.erase(old_connection_info_map_iter);
      continue;
    }

    guid_to_connection_info_map_.insert_or_assign(
        guid,
        std::make_unique<ConnectionInfo>(guid, network->IsConnectedState(),
                                         network->IsConnectingState()));
  }
}

void CellularMetricsLogger::OnInitializationTimeout() {
  CheckForPSimActivationStateMetric();
  CheckForESimProfileStatusMetric();
  CheckForCellularUsageMetrics();
  CheckForCellularServiceCountMetric();
}

void CellularMetricsLogger::LoggedInStateChanged() {
  if (!IsLoggedInUserOwnerOrRegular())
    return;

  // This flag enures that activation state is only logged once when
  // the user logs in.
  is_psim_activation_state_logged_ = false;
  CheckForPSimActivationStateMetric();

  // This flag enures that activation state is only logged once when
  // the user logs in.
  is_esim_profile_status_logged_ = false;
  CheckForESimProfileStatusMetric();

  // This flag ensures that the service count is only logged once when
  // the user logs in.
  is_service_count_logged_ = false;
  CheckForCellularServiceCountMetric();
}

void CellularMetricsLogger::NetworkConnectionStateChanged(
    const NetworkState* network) {
  DCHECK(network_state_handler_);
  CheckForCellularUsageMetrics();

  if (network->type().empty() ||
      !network->Matches(NetworkTypePattern::Cellular())) {
    return;
  }

  CheckForTimeToConnectedMetric(network);
  // Check for connection failures triggered by shill changes, unlike in
  // ConnectFailed() which is triggered by connection attempt failures at
  // chrome layers.
  CheckForShillConnectionFailureMetric(network);
  CheckForConnectionStateMetric(network);
}

void CellularMetricsLogger::SetDevicePrefs(PrefService* device_prefs) {
  if (!device_prefs)
    return;
  esim_feature_usage_metrics_ =
      std::make_unique<ESimFeatureUsageMetrics>(device_prefs);
}

void CellularMetricsLogger::CheckForTimeToConnectedMetric(
    const NetworkState* network) {
  if (network->activation_state() != shill::kActivationStateActivated)
    return;

  // We could be receiving a connection state change for a network different
  // from the one observed when the start time was recorded. Make sure that we
  // only look up time to connected of the corresponding network.
  ConnectionInfo* connection_info =
      GetConnectionInfoForCellularNetwork(network->guid());

  if (network->IsConnectingState()) {
    if (!connection_info->last_connect_start_time.has_value())
      connection_info->last_connect_start_time = base::TimeTicks::Now();

    return;
  }

  if (!connection_info->last_connect_start_time.has_value())
    return;

  if (network->IsConnectedState()) {
    base::TimeDelta time_to_connected =
        base::TimeTicks::Now() - *connection_info->last_connect_start_time;

    if (GetSimType(network) == SimType::kPSim) {
      UMA_HISTOGRAM_MEDIUM_TIMES("Network.Cellular.PSim.TimeToConnected",
                                 time_to_connected);
    } else {
      UMA_HISTOGRAM_MEDIUM_TIMES("Network.Cellular.ESim.TimeToConnected",
                                 time_to_connected);
    }
  }

  // This is hit when the network is no longer in connecting state,
  // successfully connected or otherwise. Reset the connect start_time
  // so that it is not used for further connection state changes.
  connection_info->last_connect_start_time.reset();
}

void CellularMetricsLogger::ConnectFailed(const std::string& service_path,
                                          const std::string& error_name) {
  const NetworkState* network =
      network_state_handler_->GetNetworkState(service_path);
  if (!network || network->type().empty() ||
      !network->Matches(NetworkTypePattern::Cellular())) {
    return;
  }

  // Check for connection failures at chrome layers, instead of connection
  // failures triggered by shill which is tracked in
  // CheckForShillConnectionFailureMetric().
  LogCellularConnectionSuccessHistogram(
      NetworkConnectionErrorToConnectResult(error_name), GetSimType(network));
}

void CellularMetricsLogger::DisconnectRequested(
    const std::string& service_path) {
  const NetworkState* network =
      network_state_handler_->GetNetworkState(service_path);
  if (!network->Matches(NetworkTypePattern::Cellular()))
    return;

  ConnectionInfo* connection_info =
      GetConnectionInfoForCellularNetwork(network->guid());

  // A disconnect request could fail and result in no cellular connection state
  // change. Save the request time so that only disconnections that do not
  // correspond to a request received within |kDisconnectRequestTimeout| are
  // tracked.
  connection_info->last_disconnect_request_time = base::TimeTicks::Now();
}

CellularMetricsLogger::PSimActivationState
CellularMetricsLogger::PSimActivationStateToEnum(const std::string& state) {
  if (state == shill::kActivationStateActivated)
    return PSimActivationState::kActivated;
  else if (state == shill::kActivationStateActivating)
    return PSimActivationState::kActivating;
  else if (state == shill::kActivationStateNotActivated)
    return PSimActivationState::kNotActivated;
  else if (state == shill::kActivationStatePartiallyActivated)
    return PSimActivationState::kPartiallyActivated;

  return PSimActivationState::kUnknown;
}

void CellularMetricsLogger::LogCellularDisconnectionsHistogram(
    ConnectionState connection_state,
    SimType sim_type) {
  if (sim_type == SimType::kPSim) {
    UMA_HISTOGRAM_ENUMERATION("Network.Cellular.PSim.Disconnections",
                              connection_state);
  } else {
    UMA_HISTOGRAM_ENUMERATION("Network.Cellular.ESim.Disconnections",
                              connection_state);
  }
}

void CellularMetricsLogger::CheckForShillConnectionFailureMetric(
    const NetworkState* network) {
  ConnectionInfo* connection_info =
      GetConnectionInfoForCellularNetwork(network->guid());

  // If the network connection state just failed.
  if (!network->IsConnectingOrConnected() && connection_info->is_connecting) {
    // Note: Currently all shill errors that result in a connection failure are
    // mapped to CellularMetricsLogger::ConnectResult::kUnknown.
    LogCellularConnectionSuccessHistogram(
        CellularMetricsLogger::ConnectResult::kUnknown, GetSimType(network));
  }

  connection_info->is_connecting = network->IsConnectingState();
}

void CellularMetricsLogger::CheckForConnectionStateMetric(
    const NetworkState* network) {
  ConnectionInfo* connection_info =
      GetConnectionInfoForCellularNetwork(network->guid());

  bool new_is_connected = network->IsConnectedState();
  if (connection_info->is_connected == new_is_connected)
    return;
  base::Optional<bool> old_is_connected = connection_info->is_connected;
  connection_info->is_connected = new_is_connected;

  if (new_is_connected) {
    LogCellularConnectionSuccessHistogram(
        CellularMetricsLogger::ConnectResult::kSuccess, GetSimType(network));
    LogCellularDisconnectionsHistogram(ConnectionState::kConnected,
                                       GetSimType(network));
    connection_info->last_disconnect_request_time.reset();
    return;
  }

  // If the previous connection state is nullopt then this is a new connection
  // info entry and a disconnection did not really occur. Skip logging the
  // metric in this case.
  if (!old_is_connected.has_value())
    return;

  base::Optional<base::TimeDelta> time_since_disconnect_requested;
  if (connection_info->last_disconnect_request_time) {
    time_since_disconnect_requested =
        base::TimeTicks::Now() - *connection_info->last_disconnect_request_time;
  }

  // If the disconnect occurred in less than |kDisconnectRequestTimeout|
  // from the last disconnect request time then treat it as a user
  // initiated disconnect and skip histogram log.
  if (time_since_disconnect_requested &&
      time_since_disconnect_requested < kDisconnectRequestTimeout) {
    return;
  }
  LogCellularDisconnectionsHistogram(ConnectionState::kDisconnected,
                                     GetSimType(network));
}

void CellularMetricsLogger::CheckForESimProfileStatusMetric() {
  if (!cellular_esim_profile_handler_ || !is_cellular_available_ ||
      is_esim_profile_status_logged_ || !IsLoggedInUserOwnerOrRegular()) {
    return;
  }

  std::vector<CellularESimProfile> esim_profiles =
      cellular_esim_profile_handler_->GetESimProfiles();

  bool pending_profiles_exist = false;
  bool active_profiles_exist = false;
  for (const auto& profile : esim_profiles) {
    switch (profile.state()) {
      case CellularESimProfile::State::kPending:
        FALLTHROUGH;
      case CellularESimProfile::State::kInstalling:
        pending_profiles_exist = true;
        break;

      case CellularESimProfile::State::kInactive:
        FALLTHROUGH;
      case CellularESimProfile::State::kActive:
        active_profiles_exist = true;
        break;
    }
  }

  ESimProfileStatus activation_state;
  if (active_profiles_exist && !pending_profiles_exist)
    activation_state = ESimProfileStatus::kActive;
  else if (active_profiles_exist && pending_profiles_exist)
    activation_state = ESimProfileStatus::kActiveWithPendingProfiles;
  else if (!active_profiles_exist && pending_profiles_exist)
    activation_state = ESimProfileStatus::kPendingProfilesOnly;
  else
    activation_state = ESimProfileStatus::kNoProfiles;

  UMA_HISTOGRAM_ENUMERATION("Network.Cellular.ESim.StatusAtLogin",
                            activation_state);
  is_esim_profile_status_logged_ = true;
}

void CellularMetricsLogger::CheckForPSimActivationStateMetric() {
  if (!is_cellular_available_ || is_psim_activation_state_logged_ ||
      !IsLoggedInUserOwnerOrRegular())
    return;

  NetworkStateHandler::NetworkStateList network_list;
  network_state_handler_->GetVisibleNetworkListByType(
      NetworkTypePattern::Cellular(), &network_list);

  if (network_list.size() == 0)
    return;

  base::Optional<std::string> psim_activation_state;
  for (const auto* network : network_list) {
    if (GetSimType(network) == SimType::kPSim)
      psim_activation_state = network->activation_state();
  }

  // No PSim networks exist.
  if (!psim_activation_state.has_value())
    return;

  UMA_HISTOGRAM_ENUMERATION("Network.Cellular.PSim.StatusAtLogin",
                            PSimActivationStateToEnum(*psim_activation_state));
  is_psim_activation_state_logged_ = true;
}

void CellularMetricsLogger::CheckForCellularServiceCountMetric() {
  if (!is_cellular_available_ || is_service_count_logged_ ||
      !IsLoggedInUserOwnerOrRegular()) {
    return;
  }

  NetworkStateHandler::NetworkStateList network_list;
  network_state_handler_->GetVisibleNetworkListByType(
      NetworkTypePattern::Cellular(), &network_list);

  size_t psim_networks = 0;
  size_t esim_profiles = 0;

  for (const auto* network : network_list) {
    if (GetSimType(network) == SimType::kESim)
      esim_profiles++;
    else
      psim_networks++;
  }

  UMA_HISTOGRAM_COUNTS_100("Network.Cellular.PSim.ServiceAtLogin.Count",
                           psim_networks);
  UMA_HISTOGRAM_COUNTS_100("Network.Cellular.ESim.ServiceAtLogin.Count",
                           esim_profiles);
  is_service_count_logged_ = true;
}

void CellularMetricsLogger::CheckForCellularUsageMetrics() {
  if (!is_cellular_available_)
    return;

  NetworkStateHandler::NetworkStateList network_list;
  network_state_handler_->GetVisibleNetworkListByType(
      NetworkTypePattern::NonVirtual(), &network_list);

  base::Optional<const NetworkState*> connected_cellular_network;
  bool is_non_cellular_connected = false;
  for (auto* network : network_list) {
    if (!network->IsConnectedState())
      continue;

    // Note: Only one cellular network may be ever connected.
    if (network->Matches(NetworkTypePattern::Cellular()))
      connected_cellular_network = network;
    else
      is_non_cellular_connected = true;
  }

  // Discard not-connected states received before the timer runs out.
  if (!connected_cellular_network.has_value() &&
      initialization_timer_.IsRunning()) {
    return;
  }

  CellularUsage usage;
  base::Optional<SimType> sim_type;
  if (connected_cellular_network.has_value()) {
    usage = is_non_cellular_connected
                ? CellularUsage::kConnectedWithOtherNetwork
                : CellularUsage::kConnectedAndOnlyNetwork;
    sim_type = GetSimType(connected_cellular_network.value());
  } else {
    usage = CellularUsage::kNotConnected;
  }

  if (!sim_type.has_value() || *sim_type == SimType::kPSim) {
    if (usage != last_psim_cellular_usage_) {
      UMA_HISTOGRAM_ENUMERATION("Network.Cellular.PSim.Usage.Count", usage);
      if (last_psim_cellular_usage_ ==
          CellularUsage::kConnectedAndOnlyNetwork) {
        UMA_HISTOGRAM_LONG_TIMES(
            "Network.Cellular.PSim.Usage.Duration",
            base::Time::Now() - *last_psim_usage_change_timestamp_);
      }
    }

    last_psim_usage_change_timestamp_ = base::Time::Now();
    last_psim_cellular_usage_ = usage;
  }

  if (!sim_type.has_value() || *sim_type == SimType::kESim) {
    if (usage != last_esim_cellular_usage_) {
      UMA_HISTOGRAM_ENUMERATION("Network.Cellular.ESim.Usage.Count", usage);
      if (last_esim_cellular_usage_ ==
          CellularUsage::kConnectedAndOnlyNetwork) {
        UMA_HISTOGRAM_LONG_TIMES(
            "Network.Cellular.ESim.Usage.Duration",
            base::Time::Now() - *last_esim_usage_change_timestamp_);
      }
    }

    last_esim_usage_change_timestamp_ = base::Time::Now();
    last_esim_cellular_usage_ = usage;
  }
}

CellularMetricsLogger::ConnectionInfo*
CellularMetricsLogger::GetConnectionInfoForCellularNetwork(
    const std::string& cellular_network_guid) {
  auto it = guid_to_connection_info_map_.find(cellular_network_guid);

  ConnectionInfo* connection_info;
  if (it == guid_to_connection_info_map_.end()) {
    // We could get connection events in some cases before network
    // list change event. Insert new network into the list.
    auto insert_result = guid_to_connection_info_map_.insert_or_assign(
        cellular_network_guid,
        std::make_unique<ConnectionInfo>(cellular_network_guid));
    connection_info = insert_result.first->second.get();
  } else {
    connection_info = it->second.get();
  }

  return connection_info;
}

void CellularMetricsLogger::OnShuttingDown() {
  network_state_handler_->RemoveObserver(this, FROM_HERE);
  network_state_handler_ = nullptr;
  esim_feature_usage_metrics_.reset();
}

}  // namespace chromeos
