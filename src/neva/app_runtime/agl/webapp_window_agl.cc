// Copyright 2021 LG Electronics, Inc.
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

#include "neva/app_runtime/agl/webapp_window_agl.h"

#include "base/command_line.h"
#include "neva/app_runtime/webapp_window.h"
#include "neva/pal_service/pal_platform_factory.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ui_base_neva_switches.h"
#include "ui/views/widget/desktop_aura/desktop_window_tree_host.h"

namespace neva_app_runtime {
namespace agl {

WebAppWindowAgl::WebAppWindowAgl(WebAppWindow* webapp_window)
    : webapp_window_(webapp_window) {}

void WebAppWindowAgl::SetAglActivateApp(const std::string& app) {
  auto host = webapp_window_->GetDesktopWindowTreeHost();
  if (host)
    host->AsWindowTreeHost()->SetAglActivateApp(app);
}

void WebAppWindowAgl::SetAglAppId(const std::string& title) {
  std::string agl_shell_app_id =
      base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
          switches::kAglShellAppId);
  if (title == agl_shell_app_id) {
    app_service_delegate_ =
        pal::PlatformFactory::Get()->CreateAppServiceDelegate();
    if (app_service_delegate_) {
      app_service_delegate_->SubscribeToApplicationStarted(base::BindRepeating(
          &WebAppWindowAgl::ApplicationStarted, weak_factory_.GetWeakPtr()));
    }
  }

  auto host = webapp_window_->GetDesktopWindowTreeHost();
  if (host)
    host->AsWindowTreeHost()->SetAglAppId(title);
}

void WebAppWindowAgl::ApplicationStarted(const std::string& application_id) {
  LOG(ERROR) << __func__ << " app_id=" << application_id;
  SetAglActivateApp(application_id);
}

void WebAppWindowAgl::SetAglBackground() {
  auto host = webapp_window_->GetDesktopWindowTreeHost();
  if (host)
    host->AsWindowTreeHost()->SetAglBackground();
}

void WebAppWindowAgl::SetAglReady() {
  auto host = webapp_window_->GetDesktopWindowTreeHost();
  if (host)
    host->AsWindowTreeHost()->SetAglReady();
}

void WebAppWindowAgl::SetAglPanel(uint32_t edge) {
  auto host = webapp_window_->GetDesktopWindowTreeHost();
  if (host)
    host->AsWindowTreeHost()->SetAglPanel(edge);
}

}  // namespace agl
}  // namespace neva_app_runtime