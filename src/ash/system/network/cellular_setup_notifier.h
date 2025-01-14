// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_NETWORK_CELLULAR_SETUP_NOTIFIER_H_
#define ASH_SYSTEM_NETWORK_CELLULAR_SETUP_NOTIFIER_H_

#include "ash/ash_export.h"
#include "ash/public/cpp/session/session_observer.h"
#include "chromeos/services/network_config/public/mojom/cros_network_config.mojom.h"
#include "components/prefs/pref_service.h"
#include "mojo/public/cpp/bindings/remote.h"

class PrefRegistrySimple;

namespace base {
class OneShotTimer;
}  // namespace base

namespace ash {

// Notifies the user after OOBE to finish setting up their cellular network if
// user has a device with eSIM but no profiles have been configured, or they
// inserted a cold pSIM and need to provision in-session.
class ASH_EXPORT CellularSetupNotifier : public SessionObserver {
 public:
  CellularSetupNotifier();
  CellularSetupNotifier(const CellularSetupNotifier&) = delete;
  CellularSetupNotifier& operator=(const CellularSetupNotifier&) = delete;
  ~CellularSetupNotifier() override;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

 private:
  friend class CellularSetupNotifierTest;
  FRIEND_TEST_ALL_PREFIXES(CellularSetupNotifierTest,
                           DontShowNotificationUnfinishedOOBE);
  FRIEND_TEST_ALL_PREFIXES(CellularSetupNotifierTest,
                           ShowNotificationUnactivatedNetwork);
  FRIEND_TEST_ALL_PREFIXES(CellularSetupNotifierTest,
                           DontShowNotificationActivatedNetwork);
  FRIEND_TEST_ALL_PREFIXES(CellularSetupNotifierTest,
                           ShowNotificationMultipleUnactivatedNetworks);
  FRIEND_TEST_ALL_PREFIXES(CellularSetupNotifierTest,
                           LogOutBeforeNotificationShowsLogInAgain);
  FRIEND_TEST_ALL_PREFIXES(CellularSetupNotifierTest,
                           LogInAgainAfterShowingNotification);
  FRIEND_TEST_ALL_PREFIXES(CellularSetupNotifierTest,
                           LogInAgainAfterCheckingNonCellularDevice);

  // SessionObserver:
  void OnSessionStateChanged(session_manager::SessionState state) override;

  void OnTimerFired();
  void OnGetDeviceStateList(
      std::vector<chromeos::network_config::mojom::DeviceStatePropertiesPtr>
          devices);
  void OnCellularNetworksList(
      std::vector<chromeos::network_config::mojom::NetworkStatePropertiesPtr>
          networks);
  void ShowCellularSetupNotification();
  void SetTimerForTesting(std::unique_ptr<base::OneShotTimer> test_timer) {
    timer_ = std::move(test_timer);
  }

  static const char kCellularSetupNotificationId[];

  mojo::Remote<chromeos::network_config::mojom::CrosNetworkConfig>
      remote_cros_network_config_;

  std::unique_ptr<base::OneShotTimer> timer_;
};

}  // namespace ash

#endif  // ASH_SYSTEM_NETWORK_CELLULAR_SETUP_NOTIFIER_H_
