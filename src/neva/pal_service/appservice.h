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

#ifndef NEVA_PAL_SERVICE_APPSERVICE_H_
#define NEVA_PAL_SERVICE_APPSERVICE_H_

#include "base/callback.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "neva/pal_service/public/mojom/appservice.mojom.h"

namespace pal {

class AppServiceDelegate;

class AppServiceImpl : public mojom::AppService {
 public:
  AppServiceImpl();
  AppServiceImpl(const AppServiceImpl&) = delete;
  AppServiceImpl& operator=(const AppServiceImpl&) = delete;
  ~AppServiceImpl() override;

  void AddBinding(mojo::PendingReceiver<mojom::AppService> receiver);

  // mojom::AppService
  void Start(const std::string& application_id) override;
  void GetApplications(bool graphical_only,
                       GetApplicationsCallback callback) override;
  void Subscribe(SubscribeCallback callback) override;

 private:
  void OnApplicationStarted(const std::string& application_id);

  std::unique_ptr<AppServiceDelegate> delegate_;
  mojo::ReceiverSet<mojom::AppService> receivers_;
  mojo::AssociatedRemoteSet<mojom::AppServiceListener> listeners_;
  base::WeakPtrFactory<AppServiceImpl> weak_factory_;
};

}  // namespace pal

#endif  // NEVA_PAL_SERVICE_APPSERVICE_H_
