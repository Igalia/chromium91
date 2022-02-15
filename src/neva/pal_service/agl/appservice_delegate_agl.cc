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

#include "base/logging.h"

namespace pal {
namespace agl {

const char kDefaultGetApplicationsResponse[] = "{}";

AppServiceDelegateAGL::AppServiceDelegateAGL() = default;
AppServiceDelegateAGL::~AppServiceDelegateAGL() = default;

void AppServiceDelegateAGL::Start(const std::string& application_id) {
  LOG(ERROR) << __func__ << " application_id=" << application_id;
}

void AppServiceDelegateAGL::GetApplications(bool graphical_only,
                                            OnceResponse callback) {
  LOG(ERROR) << __func__ << " graphical_only=" << graphical_only;
  std::move(callback).Run(kDefaultGetApplicationsResponse);
}

}  // namespace agl
}  // namespace pal
