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

#ifndef NEVA_PAL_SERVICE_AGL_APPSERVICE_DELEGATE_AGL_H_
#define NEVA_PAL_SERVICE_AGL_APPSERVICE_DELEGATE_AGL_H_

#include "base/callback.h"
#include "neva/pal_service/appservice_delegate.h"

#include <memory>
#include <string>

namespace pal {
namespace agl {

class AppServiceDelegateAGL : public AppServiceDelegate {
 public:
  AppServiceDelegateAGL();
  AppServiceDelegateAGL(const AppServiceDelegateAGL&) = delete;
  AppServiceDelegateAGL& operator=(const AppServiceDelegateAGL&) = delete;
  ~AppServiceDelegateAGL() override;

  void Start(const std::string& application_id) override;
  void GetApplications(bool graphical_only, OnceResponse callback) override;
  void SubscribeToApplicationStarted(RepeatingResponse callback) override;
  void UnsubscribeFromApplicationStarted() override;
  bool IsSubscribed() const override;
};

}  // namespace agl
}  // namespace pal

#endif  // NEVA_PAL_SERVICE_AGL_MEMORYMANAGER_DELEGATE_AGL_H_
