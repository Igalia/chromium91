// Copyright 2022 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "neva/pal_service/agl/appservice_delegate_agl.h"

#include "base/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/values.h"
#include "components/dbus/thread_linux/dbus_thread_linux.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_proxy.h"
#include "dbus/values_util.h"

namespace pal {
namespace agl {

namespace {

const char kAutomotiveLinuxAppLaunchName[] = "org.automotivelinux.AppLaunch";
const char kAutomotiveLinuxAppLaunchPath[] = "/org/automotivelinux/AppLaunch";
const char kMethodStart[] = "start";
const char kMethodListApplications[] = "listApplications";
const char kSignalStarted[] = "started";
const char kDefaultGetApplicationsResponse[] = "{}";

class AppLaunchHelper {
 public:
  using OnceResponse = base::OnceCallback<void(const std::string&)>;
  using RepeatingCallback = base::RepeatingCallback<void(const std::string&)>;

  AppLaunchHelper() = default;
  AppLaunchHelper(const AppLaunchHelper&) = delete;
  AppLaunchHelper& operator=(const AppLaunchHelper&) = delete;

  static AppLaunchHelper& GetInstance() {
    static base::NoDestructor<AppLaunchHelper> instance;
    return *instance;
  }

  void Start(const std::string& application_id) {
    EnsureProxy();

    dbus::MethodCall method_call(kAutomotiveLinuxAppLaunchName, kMethodStart);
    dbus::MessageWriter writer(&method_call);

    writer.AppendString(application_id);

    applaunch_proxy_->CallMethod(&method_call,
                                 dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
                                 base::DoNothing());
  }

  void GetApplications(bool only_graphical, OnceResponse callback) {
    EnsureProxy();

    dbus::MethodCall method_call(kAutomotiveLinuxAppLaunchName,
                                 kMethodListApplications);
    dbus::MessageWriter writer(&method_call);

    writer.AppendBool(only_graphical);

    applaunch_proxy_->CallMethod(
        &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT,
        base::BindOnce(&AppLaunchHelper::OnListApplicationsResponse,
                       weak_ptr_factory_.GetWeakPtr(), std::move(callback)));
  }

  void SubscribeToApplicationStarted(RepeatingCallback callback) {
    subscription_callback_ = std::move(callback);
    subscribed_ = true;
  }

  void UnsubscribeFromApplicationStarted() { subscribed_ = false; }

  bool IsSubscribed() const { return subscribed_; }

 private:
  void EnsureProxy() {
    if (!bus_) {
      dbus::Bus::Options bus_options;
      bus_options.bus_type = dbus::Bus::SESSION;
      bus_options.connection_type = dbus::Bus::PRIVATE;
      bus_options.dbus_task_runner = dbus_thread_linux::GetTaskRunner();
      bus_ = base::MakeRefCounted<dbus::Bus>(bus_options);
    }

    if (!applaunch_proxy_) {
      applaunch_proxy_ =
          bus_->GetObjectProxy(kAutomotiveLinuxAppLaunchName,
                               dbus::ObjectPath(kAutomotiveLinuxAppLaunchPath));
      applaunch_proxy_->ConnectToSignal(
          kAutomotiveLinuxAppLaunchName, kSignalStarted,
          base::BindRepeating(&AppLaunchHelper::OnStarted,
                              weak_ptr_factory_.GetWeakPtr()),
          base::BindOnce(&AppLaunchHelper::OnSignalConnected,
                         weak_ptr_factory_.GetWeakPtr()));
    }
  }

  void ReturnEmptyListApplications(OnceResponse callback) {
    std::move(callback).Run(kDefaultGetApplicationsResponse);
  }

  void OnListApplicationsResponse(OnceResponse callback,
                                  dbus::Response* response) {
    if (!response) {
      LOG(ERROR) << __func__
                 << " failed to get a DBus response from ListApplications";
      ReturnEmptyListApplications(std::move(callback));
      return;
    }

    dbus::MessageReader reader(response);
    std::unique_ptr<base::Value> response_value = dbus::PopDataAsValue(&reader);
    if (!response_value) {
      LOG(ERROR) << __func__ << " failed to retrieve listApplications response";
      ReturnEmptyListApplications(std::move(callback));
      return;
    }

    std::string response_string;
    if (!base::JSONWriter::Write(*response_value, &response_string)) {
      LOG(ERROR) << __func__
                 << " failed to generate the JSON string of the response";
      ReturnEmptyListApplications(std::move(callback));
      return;
    }
    std::move(callback).Run(response_string);
  }

  void OnStarted(dbus::Signal* signal) {
    if (!subscribed_)
      return;

    dbus::MessageReader reader(signal);
    std::string application_id;
    if (!reader.PopString(&application_id)) {
      LOG(ERROR) << __func__ << "no application id on signal "
                 << signal->ToString();
      return;
    }
    subscription_callback_.Run(application_id);
  }

  void OnSignalConnected(const std::string& interface,
                         const std::string& signal,
                         bool succeeded) {
    LOG_IF(ERROR, !succeeded)
        << "Connect to " << interface << " " << signal << " failed.";
  }

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* applaunch_proxy_ = nullptr;
  RepeatingCallback subscription_callback_;
  bool subscribed_ = false;

  base::WeakPtrFactory<AppLaunchHelper> weak_ptr_factory_{this};
};

}  // namespace

AppServiceDelegateAGL::AppServiceDelegateAGL() = default;
AppServiceDelegateAGL::~AppServiceDelegateAGL() = default;

void AppServiceDelegateAGL::Start(const std::string& application_id) {
  VLOG(1) << __func__ << " application_id=" << application_id;
  AppLaunchHelper::GetInstance().Start(application_id);
}

void AppServiceDelegateAGL::GetApplications(bool graphical_only,
                                            OnceResponse callback) {
  VLOG(1) << __func__ << " graphical_only=" << graphical_only;
  AppLaunchHelper::GetInstance().GetApplications(graphical_only,
                                                 std::move(callback));
}

void AppServiceDelegateAGL::SubscribeToApplicationStarted(
    RepeatingResponse callback) {
  VLOG(1) << __func__;
  AppLaunchHelper::GetInstance().SubscribeToApplicationStarted(
      std::move(callback));
}

void AppServiceDelegateAGL::UnsubscribeFromApplicationStarted() {
  VLOG(1) << __func__;
  AppLaunchHelper::GetInstance().UnsubscribeFromApplicationStarted();
}

bool AppServiceDelegateAGL::IsSubscribed() const {
  return AppLaunchHelper::GetInstance().IsSubscribed();
}

}  // namespace agl
}  // namespace pal
