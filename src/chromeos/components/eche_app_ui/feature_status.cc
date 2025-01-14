// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/eche_app_ui/feature_status.h"

namespace chromeos {
namespace eche_app {

std::ostream& operator<<(std::ostream& stream, FeatureStatus status) {
  switch (status) {
    case FeatureStatus::kIneligible:
      stream << "[Ineligible for feature]";
      break;
    case FeatureStatus::kDisabled:
      stream << "[Disabled]";
      break;
    case FeatureStatus::kDisconnected:
      stream << "[Enabled; disconnected]";
      break;
    case FeatureStatus::kConnecting:
      stream << "[Enabled; connecting]";
      break;
    case FeatureStatus::kConnected:
      stream << "[Enabled; connected]";
      break;
  }

  return stream;
}

}  // namespace eche_app
}  // namespace chromeos
