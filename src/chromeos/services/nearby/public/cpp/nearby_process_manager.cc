// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/nearby/public/cpp/nearby_process_manager.h"

namespace chromeos {
namespace nearby {

std::ostream& operator<<(
    std::ostream& os,
    const NearbyProcessManager::NearbyProcessShutdownReason& reason) {
  switch (reason) {
    case NearbyProcessManager::NearbyProcessShutdownReason::kNormal:
      return os << "Normal";
    case NearbyProcessManager::NearbyProcessShutdownReason::kCrash:
      return os << "Crash";
    case NearbyProcessManager::NearbyProcessShutdownReason::
        kMojoPipeDisconnection:
      return os << "Mojo Pipe Disconnection";
  }
}

}  // namespace nearby
}  // namespace chromeos
