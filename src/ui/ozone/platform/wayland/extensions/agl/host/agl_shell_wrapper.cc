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

#include "ui/ozone/platform/wayland/extensions/agl/host/agl_shell_wrapper.h"

#include "agl_shell_wrapper.h"
#include "base/logging.h"
#include "base/stl_util.h"
#include "ui/ozone/platform/wayland/host/wayland_connection.h"
#include "ui/ozone/platform/wayland/host/wayland_output_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_window.h"

namespace ui {

static const struct agl_shell_listener shell_listener = {
    &AglShellWrapper::AglShellBoundOk,
    &AglShellWrapper::AglShellBoundFail,
#ifdef AGL_SHELL_APP_STATE_SINCE_VERSION
    &AglShellWrapper::AglAppState,
#endif
};

AglShellWrapper::AglShellWrapper(agl_shell* agl_shell,
                                 WaylandConnection* wayland_connection)
    : agl_shell_(agl_shell), connection_(wayland_connection) {
  if (wl::get_version_of_object(agl_shell) >= AGL_SHELL_BOUND_OK_SINCE_VERSION)
    agl_shell_add_listener(agl_shell, &shell_listener, this);
}

AglShellWrapper::~AglShellWrapper() = default;

void AglShellWrapper::SetAglActivateApp(const std::string& app_id) {
  wl_output* output =
      connection_->wayland_output_manager()->GetPrimaryOutput()->output();
  agl_shell_activate_app(agl_shell_.get(), app_id.c_str(), output);
}

void AglShellWrapper::SetAglPanel(WaylandWindow* window, uint32_t edge) {
  wl_surface* surface = window->root_surface()->surface();
  wl_output* output =
      connection_->wayland_output_manager()->GetPrimaryOutput()->output();

  agl_shell_set_panel(agl_shell_.get(), surface, output, edge);
}

void AglShellWrapper::SetAglBackground(WaylandWindow* window) {
  wl_surface* surface = window->root_surface()->surface();
  wl_output* output =
      connection_->wayland_output_manager()->GetPrimaryOutput()->output();

  agl_shell_set_background(agl_shell_.get(), surface, output);
}

void AglShellWrapper::SetAglReady() {
  agl_shell_ready(agl_shell_.get());
}

// static
void AglShellWrapper::AglShellBoundOk(void* data, struct agl_shell*) {
  AglShellWrapper* wrapper = static_cast<AglShellWrapper*>(data);
  wrapper->wait_for_bound_ = false;
  wrapper->bound_ok_ = true;
  LOG(INFO) << "Bound to agl_shell (bound_ok)";
}

// static
void AglShellWrapper::AglShellBoundFail(void* data, struct agl_shell*) {
  AglShellWrapper* wrapper = static_cast<AglShellWrapper*>(data);
  wrapper->wait_for_bound_ = false;
  wrapper->bound_ok_ = false;
  LOG(INFO) << "Failed to bind to agl_shell (bound_fail)";
}

#ifdef AGL_SHELL_APP_STATE_SINCE_VERSION
// static
void AglShellWrapper::AglAppState(void* data,
                                  struct agl_shell*,
                                  const char* app_id,
                                  uint32_t state) {
  AglShellWrapper* wrapper = static_cast<AglShellWrapper*>(data);
  static const char* const state2string[] = {
      "started",
      "terminated",
      "activated",
      "deactivated",
  };

  LOG(INFO) << "State for app " << app_id << " changed to "
            << (state < base::size(state2string) ? state2string[state]
                                                 : "unknown");

  if (state == AGL_SHELL_APP_STATE_STARTED) {
    wrapper->SetAglActivateApp(app_id);
    LOG(INFO) << "Activating app " << app_id;
  }
}
#endif

bool AglShellWrapper::WaitUntilBoundOk() {
  int ret = 0;
  while (ret != -1 && wait_for_bound_) {
    ret = wl_display_dispatch(connection_->display());
  }

  return bound_ok_;
}

}  // namespace ui
