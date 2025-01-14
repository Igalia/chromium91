// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/attestation/attestation_flow_type_decider.h"

#include <utility>

#include "base/bind.h"
#include "chromeos/attestation/attestation_flow.h"
#include "chromeos/attestation/attestation_flow_status_reporter.h"

#include "base/logging.h"

namespace chromeos {
namespace attestation {

AttestationFlowTypeDecider::AttestationFlowTypeDecider() = default;

AttestationFlowTypeDecider::~AttestationFlowTypeDecider() = default;

void AttestationFlowTypeDecider::CheckType(
    ServerProxy* server_proxy,
    AttestationFlowStatusReporter* reporter,
    AttestationFlowTypeCheckCallback callback) {
  server_proxy->CheckIfAnyProxyPresent(base::BindOnce(
      &AttestationFlowTypeDecider::OnCheckProxyPresence,
      weak_factory_.GetWeakPtr(), reporter, std::move(callback)));
}

void AttestationFlowTypeDecider::OnCheckProxyPresence(
    AttestationFlowStatusReporter* reporter,
    AttestationFlowTypeCheckCallback callback,
    bool is_proxy_present) {
  reporter->OnHasProxy(is_proxy_present);
  // The integrated flow is currently only allowed if no proxy is present.
  // TODO(b/158532239): Determine if system proxy is available at runtime.
  reporter->OnIsSystemProxyAvailable(false);
  std::move(callback).Run(!is_proxy_present);
}

}  // namespace attestation
}  // namespace chromeos
