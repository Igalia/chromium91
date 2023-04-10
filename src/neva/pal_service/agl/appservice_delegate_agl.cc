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

#include <grpcpp/grpcpp.h>

#include "base/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/threading/simple_thread.h"
#include "base/values.h"
#include "neva/pal_service/agl/protos/applauncher.grpc.pb.h"

namespace pal {
namespace agl {

namespace {

using GRPCClientReader =  std::unique_ptr<grpc::ClientReader<automotivegradelinux::StatusResponse>>;

class AppLaunchHelper {
 public:
  using OnceResponse = base::OnceCallback<void(const std::string&)>;
  using RepeatingCallback = base::RepeatingCallback<void(const std::string&)>;

  class AppStatusPollingWorker : public base::DelegateSimpleThread::Delegate {
   public:
    AppStatusPollingWorker(AppLaunchHelper *helper)
      : helper_{helper}
      , thread_{MakeThread()} {
        thread_->StartAsync();
    }

    void Run() override {
      VLOG(1) << __func__ << " Started appstatus polling thread...";
      grpc::ClientContext context;
      automotivegradelinux::StatusResponse response;

      // StatusRequest doesn't have any members, so call GetStatusEvents with an empty request
      GRPCClientReader reader(helper_->stub_->GetStatusEvents(&context,
                                                              automotivegradelinux::StatusRequest()));

      // applaunchd doesn't finish its notification loop, meaning that our
      // thread should keep waiting for new events.
      // See applaunchd's AppLauncherImpl::SendStatus()
      while (reader->Read(&response)) {
        if (response.has_app()) {
          automotivegradelinux::AppStatus app_status = response.app();
          VLOG(1) << __func__ << " received status " << app_status.status()
                  << " for app " << app_status.id();
          if (app_status.status() == "started") {
            helper_->subscription_callback_.Run(app_status.id());
          }
          // TODO(rzanoni): handle "terminated"
        }
      }

      grpc::Status status = reader->Finish();
      VLOG(1) << __func__ << " status=" << status.ok();
    }

   private:
    std::unique_ptr<base::DelegateSimpleThread> MakeThread() {
      return std::make_unique<base::DelegateSimpleThread>(this, "ApplauncherAppStatusPollWorker",
                                                          base::SimpleThread::Options());
    }

    AppLaunchHelper* helper_;
    std::unique_ptr<base::SimpleThread> thread_;
  };

  AppLaunchHelper()
    : stub_{MakeStub()}
    , app_status_worker_{this} {}

  AppLaunchHelper(const AppLaunchHelper&) = delete;
  AppLaunchHelper& operator=(const AppLaunchHelper&) = delete;

  static AppLaunchHelper& GetInstance() {
    static base::NoDestructor<AppLaunchHelper> instance;
    return *instance;
  }

  void Start(const std::string& application_id) {
    automotivegradelinux::StartRequest request;
    request.set_id(application_id);

    grpc::ClientContext context;
    automotivegradelinux::StartResponse response;

    grpc::Status status = stub_->StartApplication(&context, request, &response);
    VLOG(1) << __func__ << " status.ok=" << status.ok();
  }

  void GetApplications(bool only_graphical, OnceResponse callback) {
    automotivegradelinux::ListRequest request;
    automotivegradelinux::ListResponse response;
    grpc::ClientContext context;

    grpc::Status status = stub_->ListApplications(&context, request, &response);
    VLOG(1) << __func__ << " status.ok=" << status.ok();
    if (!status.ok())
      return;

    base::Value apps_list(base::Value::Type::LIST);
    for (int i = 0; i < response.apps_size(); i++) {
      automotivegradelinux::AppInfo app_info = response.apps(i);
      base::Value app_info_dict(base::Value::Type::DICTIONARY);
      app_info_dict.SetKey("id", base::Value(app_info.id()));
      app_info_dict.SetKey("name", base::Value(app_info.name()));
      app_info_dict.SetKey("icon", base::Value(app_info.icon_path()));
      apps_list.Append(std::move(app_info_dict));
    }

    std::string response_string;
    if (!base::JSONWriter::Write(apps_list, &response_string)) {
      VLOG(1) << __func__ << " Failed to write JSON.";
      return;
    }

    std::move(callback).Run(response_string);
  }

  void SubscribeToApplicationStarted(RepeatingCallback callback) {
    subscription_callback_ = std::move(callback);
    subscribed_ = true;
  }

  void UnsubscribeFromApplicationStarted() { subscribed_ = false; }

  bool IsSubscribed() const { return subscribed_; }

 private:
  std::shared_ptr<automotivegradelinux::AppLauncher::Stub> MakeStub() const {
    return automotivegradelinux::AppLauncher::NewStub(grpc::CreateChannel("localhost:50052",
                                         grpc::InsecureChannelCredentials()));
  }

  RepeatingCallback subscription_callback_;
  bool subscribed_ = false;
  std::shared_ptr<automotivegradelinux::AppLauncher::Stub> stub_;
  base::WeakPtrFactory<AppLaunchHelper> weak_ptr_factory_{this};
  AppStatusPollingWorker app_status_worker_;
};

}  // namespace

AppServiceDelegateAGL::AppServiceDelegateAGL() = default;
AppServiceDelegateAGL::~AppServiceDelegateAGL() = default;

void AppServiceDelegateAGL::Start(const std::string& application_id) {
  VLOG(1) << __func__ << " application_id=" << application_id;
  AppLaunchHelper::GetInstance().Start(application_id);
}

void AppServiceDelegateAGL::GetApplications(bool graphical_only,
                                            OnceResponse callback) {
  VLOG(1) << __func__ << " graphical_only=" << graphical_only;
  AppLaunchHelper::GetInstance().GetApplications(graphical_only,
                                                 std::move(callback));
}

void AppServiceDelegateAGL::SubscribeToApplicationStarted(
    RepeatingResponse callback) {
  VLOG(1) << __func__;
  AppLaunchHelper::GetInstance().SubscribeToApplicationStarted(
      std::move(callback));
}

void AppServiceDelegateAGL::UnsubscribeFromApplicationStarted() {
  VLOG(1) << __func__;
  AppLaunchHelper::GetInstance().UnsubscribeFromApplicationStarted();
}

bool AppServiceDelegateAGL::IsSubscribed() const {
  return AppLaunchHelper::GetInstance().IsSubscribed();
}

}  // namespace agl
}  // namespace pal
