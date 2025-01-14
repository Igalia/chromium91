// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/system_notification_controller.h"

#include "ash/constants/ash_features.h"
#include "ash/system/caps_lock_notification_controller.h"
#include "ash/system/cast/cast_notification_controller.h"
#include "ash/system/gesture_education/gesture_education_notification_controller.h"
#include "ash/system/network/auto_connect_notifier.h"
#include "ash/system/network/cellular_setup_notifier.h"
#include "ash/system/network/wifi_toggle_notification_controller.h"
#include "ash/system/power/power_notification_controller.h"
#include "ash/system/screen_security/screen_security_notification_controller.h"
#include "ash/system/session/session_limit_notification_controller.h"
#include "ash/system/tracing_notification_controller.h"
#include "ash/system/update/update_notification_controller.h"
#include "ui/message_center/message_center.h"

namespace ash {
namespace {

std::unique_ptr<ash::CellularSetupNotifier> CreateCellularSetupNotifier() {
  return chromeos::features::IsCellularActivationUiEnabled()
             ? std::make_unique<ash::CellularSetupNotifier>()
             : nullptr;
}

}  // namespace

SystemNotificationController::SystemNotificationController()
    : auto_connect_(std::make_unique<AutoConnectNotifier>()),
      caps_lock_(std::make_unique<CapsLockNotificationController>()),
      cast_(std::make_unique<CastNotificationController>()),
      cellular_setup_notifier_(CreateCellularSetupNotifier()),
      gesture_education_(
          std::make_unique<GestureEducationNotificationController>()),
      power_(std::make_unique<PowerNotificationController>(
          message_center::MessageCenter::Get())),
      screen_security_(
          std::make_unique<ScreenSecurityNotificationController>()),
      session_limit_(std::make_unique<SessionLimitNotificationController>()),
      tracing_(std::make_unique<TracingNotificationController>()),
      update_(std::make_unique<UpdateNotificationController>()),
      wifi_toggle_(std::make_unique<WifiToggleNotificationController>()) {}

SystemNotificationController::~SystemNotificationController() = default;

}  // namespace ash
