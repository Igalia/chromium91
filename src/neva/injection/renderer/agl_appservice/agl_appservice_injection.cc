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

#include "neva/injection/renderer/agl_appservice/agl_appservice_injection.h"

#include "base/bind.h"
#include "base/macros.h"
#include "gin/arguments.h"
#include "gin/function_template.h"
#include "gin/handle.h"
#include "neva/pal_service/public/mojom/constants.mojom.h"
#include "third_party/blink/public/common/thread_safe_browser_interface_broker_proxy.h"
#include "third_party/blink/public/platform/platform.h"
#include "third_party/blink/public/web/blink.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_script_source.h"

namespace injections {

namespace {

const char kNavigatorObjectName[] = "navigator";
const char kAppServiceObjectName[] = "appService";
const char kStartMethodName[] = "start";
const char kGetApplicationsMethodName[] = "getApplications";

// Returns true if |maybe| is both a value, and that value is true.
inline bool IsTrue(v8::Maybe<bool> maybe) {
  return maybe.IsJust() && maybe.FromJust();
}

}  // anonymous namespace

gin::WrapperInfo AGLAppServiceInjection::kWrapperInfo = {
    gin::kEmbedderNativeGin};

AGLAppServiceInjection::AGLAppServiceInjection() {
  blink::Platform::Current()->GetBrowserInterfaceBroker()->GetInterface(
      remote_appservice_.BindNewPipeAndPassReceiver());
}

AGLAppServiceInjection::~AGLAppServiceInjection() = default;

void AGLAppServiceInjection::Start(gin::Arguments* args) {
  std::string application_id;
  if (!args->GetNext(&application_id)) {
    args->ThrowError();
    return;
  }
  remote_appservice_->Start(application_id);
}

void AGLAppServiceInjection::GetApplications(gin::Arguments* args) {
  v8::Local<v8::Value> only_graphical_value;
  if (!args->GetNext(&only_graphical_value) || only_graphical_value.IsEmpty()) {
    args->ThrowError();
    return;
  }
  bool only_graphical = only_graphical_value->IsTrue();

  v8::Local<v8::Function> local_func;
  if (!args->GetNext(&local_func)) {
    args->ThrowError();
    return;
  }

  auto callback_ptr = std::make_unique<v8::Persistent<v8::Function>>(
      args->isolate(), local_func);

  remote_appservice_->GetApplications(
      only_graphical,
      base::BindOnce(&AGLAppServiceInjection::OnGetApplicationsRespond,
                     base::Unretained(this),
                     std::move(std::move(callback_ptr))));
}

gin::ObjectTemplateBuilder AGLAppServiceInjection::GetObjectTemplateBuilder(
    v8::Isolate* isolate) {
  return gin::Wrappable<AGLAppServiceInjection>::GetObjectTemplateBuilder(
             isolate)
      .SetMethod(kStartMethodName, &AGLAppServiceInjection::Start)
      .SetMethod(kGetApplicationsMethodName,
                 &AGLAppServiceInjection::GetApplications);
}

void AGLAppServiceInjection::OnGetApplicationsRespond(
    std::unique_ptr<v8::Persistent<v8::Function>> callback,
    const std::string& app_list) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Object> wrapper;
  if (!GetWrapper(isolate).ToLocal(&wrapper)) {
    LOG(ERROR) << __func__ << "(): can not get wrapper";
    return;
  }

  v8::Local<v8::Context> context = wrapper->CreationContext();
  v8::Context::Scope context_scope(context);
  v8::Local<v8::Function> local_callback = callback->Get(isolate);

  v8::MaybeLocal<v8::Value> maybe_json =
      v8::JSON::Parse(context, gin::StringToV8(isolate, app_list));
  v8::Local<v8::Value> json;
  if (maybe_json.ToLocal(&json)) {
    const int argc = 1;
    v8::Local<v8::Value> argv[] = {json};
    ALLOW_UNUSED_LOCAL(local_callback->Call(context, wrapper, argc, argv));
  } else {
    LOG(ERROR) << __func__ << "(): malformed JSON";
  }
}

// static
void AGLAppServiceInjection::Install(blink::WebLocalFrame* frame) {
  v8::Isolate* isolate = blink::MainThreadIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty())
    return;

  v8::Local<v8::Object> global = context->Global();
  v8::Context::Scope context_scope(context);

  v8::Local<v8::String> navigator_name =
      gin::StringToV8(isolate, kNavigatorObjectName);
  v8::Local<v8::Object> navigator;
  if (!gin::Converter<v8::Local<v8::Object>>::FromV8(
          isolate, global->Get(context, navigator_name).ToLocalChecked(),
          &navigator))
    return;

  if (IsTrue(navigator->Has(context,
                            gin::StringToV8(isolate, kAppServiceObjectName))))
    return;

  v8::Local<v8::Object> app_service;
  CreateAppServiceObject(isolate, navigator).ToLocal(&app_service);
}

void AGLAppServiceInjection::Uninstall(blink::WebLocalFrame* frame) {
  v8::Isolate* isolate = blink::MainThreadIsolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::Context> context = frame->MainWorldScriptContext();
  if (context.IsEmpty())
    return;

  v8::Local<v8::Object> global = context->Global();
  v8::Context::Scope context_scope(context);

  v8::Local<v8::String> navigator_name =
      gin::StringToV8(isolate, kNavigatorObjectName);
  v8::Local<v8::Object> navigator;
  if (gin::Converter<v8::Local<v8::Object>>::FromV8(
          isolate, global->Get(context, navigator_name).ToLocalChecked(),
          &navigator)) {
    v8::Local<v8::String> appservice_name =
        gin::StringToV8(isolate, kAppServiceObjectName);
    if (IsTrue(navigator->Has(context, appservice_name)))
      ALLOW_UNUSED_LOCAL(navigator->Delete(context, appservice_name));
  }
}

// static
v8::MaybeLocal<v8::Object> AGLAppServiceInjection::CreateAppServiceObject(
    v8::Isolate* isolate,
    v8::Local<v8::Object> parent) {
  gin::Handle<AGLAppServiceInjection> appservice =
      gin::CreateHandle(isolate, new AGLAppServiceInjection());
  parent
      ->Set(isolate->GetCurrentContext(),
            gin::StringToV8(isolate, kAppServiceObjectName), appservice.ToV8())
      .Check();
  return appservice->GetWrapper(isolate);
}

}  // namespace injections
