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

#include "neva/pal_service/appservice.h"

#include "base/memory/weak_ptr.h"
#include "neva/pal_service/appservice_delegate.h"
#include "neva/pal_service/pal_platform_factory.h"

namespace pal {

AppServiceImpl::AppServiceImpl()
    : delegate_(PlatformFactory::Get()->CreateAppServiceDelegate()),
      weak_factory_(this) {}

AppServiceImpl::~AppServiceImpl() = default;

void AppServiceImpl::AddBinding(
    mojo::PendingReceiver<mojom::AppService> receiver) {
  receivers_.Add(this, std::move(receiver));
}

void AppServiceImpl::GetApplications(bool only_graphical,
                                     GetApplicationsCallback callback) {
  if (delegate_)
    delegate_->GetApplications(only_graphical, std::move(callback));
  else
    std::move(callback).Run("{}");
}

void AppServiceImpl::Start(const std::string& application_id) {
  if (delegate_)
    delegate_->Start(application_id);
}

}  // namespace pal
