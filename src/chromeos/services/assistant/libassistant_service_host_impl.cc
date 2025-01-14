// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/assistant/libassistant_service_host_impl.h"

#include "base/check.h"
#include "base/sequence_checker.h"
#include "build/buildflag.h"
#include "chromeos/assistant/buildflags.h"

#if BUILDFLAG(ENABLE_CROS_LIBASSISTANT)
#include "chromeos/services/libassistant/libassistant_service.h"
#endif

namespace chromeos {
namespace assistant {

#if BUILDFLAG(ENABLE_CROS_LIBASSISTANT)

LibassistantServiceHostImpl::LibassistantServiceHostImpl() {
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

LibassistantServiceHostImpl::~LibassistantServiceHostImpl() = default;

void LibassistantServiceHostImpl::Launch(
    mojo::PendingReceiver<chromeos::libassistant::mojom::LibassistantService>
        receiver) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(!libassistant_service_);
  libassistant_service_ =
      std::make_unique<chromeos::libassistant::LibassistantService>(
          std::move(receiver));
}

void LibassistantServiceHostImpl::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  libassistant_service_ = nullptr;
}

#else

LibassistantServiceHostImpl::LibassistantServiceHostImpl() = default;
LibassistantServiceHostImpl::~LibassistantServiceHostImpl() = default;

void LibassistantServiceHostImpl::Launch(
    mojo::PendingReceiver<chromeos::libassistant::mojom::LibassistantService>
        receiver) {}

void LibassistantServiceHostImpl::Stop() {}

#endif

}  // namespace assistant
}  // namespace chromeos
