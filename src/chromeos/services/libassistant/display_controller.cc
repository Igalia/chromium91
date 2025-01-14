// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/libassistant/display_controller.h"

#include <memory>

#include "base/threading/sequenced_task_runner_handle.h"
#include "chromeos/assistant/internal/internal_util.h"
#include "chromeos/services/assistant/public/cpp/features.h"
#include "chromeos/services/libassistant/display_connection_impl.h"
#include "chromeos/services/libassistant/public/mojom/speech_recognition_observer.mojom.h"
#include "libassistant/shared/internal_api/assistant_manager_internal.h"

namespace chromeos {
namespace libassistant {

namespace {
// A macro which ensures we are running on the main thread.
#define ENSURE_MOJOM_THREAD(method, ...)                                    \
  if (!mojom_task_runner_->RunsTasksInCurrentSequence()) {                  \
    mojom_task_runner_->PostTask(                                           \
        FROM_HERE,                                                          \
        base::BindOnce(method, weak_factory_.GetWeakPtr(), ##__VA_ARGS__)); \
    return;                                                                 \
  }
}  // namespace

class DisplayController::EventObserver : public DisplayConnectionObserver {
 public:
  explicit EventObserver(DisplayController* parent) : parent_(parent) {}
  EventObserver(const EventObserver&) = delete;
  EventObserver& operator=(const EventObserver&) = delete;
  ~EventObserver() override = default;

  void OnSpeechLevelUpdated(const float speech_level) override {
    for (auto& observer : parent_->speech_recognition_observers_)
      observer->OnSpeechLevelUpdated(speech_level);
  }

 private:
  DisplayController* const parent_;
};

DisplayController::DisplayController(
    mojo::RemoteSet<mojom::SpeechRecognitionObserver>*
        speech_recognition_observers)
    : event_observer_(std::make_unique<EventObserver>(this)),
      display_connection_(std::make_unique<DisplayConnectionImpl>(
          event_observer_.get(),
          /*feedback_ui_enabled=*/true,
          assistant::features::IsMediaSessionIntegrationEnabled())),
      speech_recognition_observers_(*speech_recognition_observers),
      mojom_task_runner_(base::SequencedTaskRunnerHandle::Get()) {
  DCHECK(speech_recognition_observers);
}

DisplayController::~DisplayController() = default;

void DisplayController::Bind(
    mojo::PendingReceiver<mojom::DisplayController> receiver) {
  receiver_.Bind(std::move(receiver));
}

void DisplayController::SetActionModule(
    chromeos::assistant::action::CrosActionModule* action_module) {
  DCHECK(action_module);
  action_module_ = action_module;
}

void DisplayController::SetArcPlayStoreEnabled(bool enabled) {
  display_connection_->SetArcPlayStoreEnabled(enabled);
}

void DisplayController::SetDeviceAppsEnabled(bool enabled) {
  DCHECK(action_module_);
  display_connection_->SetDeviceAppsEnabled(enabled);

  DCHECK(action_module_);
  action_module_->SetAppSupportEnabled(
      chromeos::assistant::features::IsAppSupportEnabled() && enabled);
}

void DisplayController::SetRelatedInfoEnabled(bool enabled) {
  display_connection_->SetAssistantContextEnabled(enabled);
}

void DisplayController::SetAndroidAppList(
    const std::vector<::chromeos::assistant::AndroidAppInfo>& apps) {
  display_connection_->OnAndroidAppListRefreshed(apps);
}

void DisplayController::OnAssistantManagerCreated(
    assistant_client::AssistantManager* assistant_manager,
    assistant_client::AssistantManagerInternal* assistant_manager_internal) {
  DCHECK(assistant_manager_internal);
  assistant_manager_internal->SetDisplayConnection(display_connection_.get());

  assistant_manager_internal_ = assistant_manager_internal;
}

void DisplayController::OnDestroyingAssistantManager(
    assistant_client::AssistantManager* assistant_manager,
    assistant_client::AssistantManagerInternal* assistant_manager_internal) {
  assistant_manager_internal_ = nullptr;
}

// Called from Libassistant thread.
void DisplayController::OnVerifyAndroidApp(
    const std::vector<chromeos::assistant::AndroidAppInfo>& apps_info,
    const chromeos::assistant::InteractionInfo& interaction) {
  ENSURE_MOJOM_THREAD(&DisplayController::OnVerifyAndroidApp, apps_info,
                      interaction);

  std::vector<chromeos::assistant::AndroidAppInfo> result_apps_info;
  for (auto& app_info : apps_info) {
    chromeos::assistant::AndroidAppInfo result_app_info(app_info);
    auto app_status = GetAndroidAppStatus(app_info.package_name);
    result_app_info.status = app_status;
    result_apps_info.emplace_back(result_app_info);
  }

  std::string interaction_proto = CreateVerifyProviderResponseInteraction(
      interaction.interaction_id, result_apps_info);

  assistant_client::VoicelessOptions options;
  options.obfuscated_gaia_id = interaction.user_id;
  // Set the request to be user initiated so that a new conversation will be
  // created to handle the client OPs in the response of this request.
  options.is_user_initiated = true;

  assistant_manager_internal_->SendVoicelessInteraction(
      interaction_proto, /*description=*/"verify_provider_response", options,
      [](auto) {});
}

chromeos::assistant::AppStatus DisplayController::GetAndroidAppStatus(
    const std::string& package_name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto& app_info : display_connection_->GetCachedAndroidAppList()) {
    if (app_info.package_name == package_name) {
      DVLOG(1) << "Assistant: App is available on the device.";
      return assistant::AppStatus::kAvailable;
    }
  }

  DVLOG(1) << "Assistant: App is unavailable on the device";
  return assistant::AppStatus::kUnavailable;
}

}  // namespace libassistant
}  // namespace chromeos
