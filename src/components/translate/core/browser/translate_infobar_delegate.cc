// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/translate/core/browser/translate_infobar_delegate.h"

#include <algorithm>
#include <utility>

#include "base/i18n/string_compare.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial_params.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "components/infobars/core/infobar.h"
#include "components/infobars/core/infobar_manager.h"
#include "components/language/core/common/language_experiments.h"
#include "components/strings/grit/components_strings.h"
#include "components/translate/core/browser/language_state.h"
#include "components/translate/core/browser/translate_accept_languages.h"
#include "components/translate/core/browser/translate_client.h"
#include "components/translate/core/browser/translate_download_manager.h"
#include "components/translate/core/browser/translate_driver.h"
#include "components/translate/core/browser/translate_manager.h"
#include "components/translate/core/common/translate_constants.h"
#include "ui/base/l10n/l10n_util.h"

namespace {

// The default number of times user should consecutively translate for "Always
// Translate" to automatically trigger.
const int kAutoAlwaysThreshold = 5;
// The default number of times user should consecutively dismiss the translate
// infobar for "Never Translate" to automatically trigger.
const int kAutoNeverThreshold = 20;
// The default maximum number of times "Always Translate" is automatically
// triggered.
const int kMaxNumberOfAutoAlways = 2;
// The default maximum number of times "Never Translate" is automatically
// triggered.
const int kMaxNumberOfAutoNever = 2;

}  // namespace

namespace translate {

const base::Feature kTranslateAutoSnackbars{"TranslateAutoSnackbars",
                                            base::FEATURE_ENABLED_BY_DEFAULT};

const base::Feature kTranslateCompactUI{"TranslateCompactUI",
                                        base::FEATURE_ENABLED_BY_DEFAULT};

const size_t TranslateInfoBarDelegate::kNoIndex = TranslateUIDelegate::kNoIndex;

TranslateInfoBarDelegate::~TranslateInfoBarDelegate() {
  for (auto& observer : observers_) {
    observer.OnTranslateInfoBarDelegateDestroyed(this);
  }
}

infobars::InfoBarDelegate::InfoBarIdentifier
TranslateInfoBarDelegate::GetIdentifier() const {
  return TRANSLATE_INFOBAR_DELEGATE_NON_AURA;
}

// static
void TranslateInfoBarDelegate::Create(
    bool replace_existing_infobar,
    const base::WeakPtr<TranslateManager>& translate_manager,
    infobars::InfoBarManager* infobar_manager,
    bool is_off_the_record,
    translate::TranslateStep step,
    const std::string& source_language,
    const std::string& target_language,
    TranslateErrors::Type error_type,
    bool triggered_from_menu) {
  DCHECK(translate_manager);
  DCHECK(infobar_manager);

  // Check preconditions.
  if (step != translate::TRANSLATE_STEP_TRANSLATE_ERROR) {
    DCHECK(TranslateDownloadManager::IsSupportedLanguage(target_language));
    if (!TranslateDownloadManager::IsSupportedLanguage(source_language)) {
      // If the detected source language experiment is active, then the source
      // language can be unknown at any translate step. If it is disabled, then
      // the source language can only be unknown for the "translating"
      // infobar, which is the case when the user started a translation from the
      // context menu.
      if (!base::FeatureList::IsEnabled(
              language::kDetectedSourceLanguageOption)) {
        DCHECK(step == translate::TRANSLATE_STEP_TRANSLATING ||
               step == translate::TRANSLATE_STEP_AFTER_TRANSLATE);
      }
      DCHECK_EQ(translate::kUnknownLanguageCode, source_language);
    }
  }

  // Find any existing translate infobar delegate.
  infobars::InfoBar* old_infobar = NULL;
  TranslateInfoBarDelegate* old_delegate = NULL;
  for (size_t i = 0; i < infobar_manager->infobar_count(); ++i) {
    old_infobar = infobar_manager->infobar_at(i);
    old_delegate = old_infobar->delegate()->AsTranslateInfoBarDelegate();
    if (old_delegate) {
      if (!replace_existing_infobar)
        return;
      break;
    }
  }

  // Try to reuse existing translate infobar delegate.
  if (old_delegate) {
    old_delegate->step_ = step;
    for (auto& observer : old_delegate->observers_) {
      observer.OnTargetLanguageChanged(target_language);
      observer.OnTranslateStepChanged(step, error_type);
    }
    return;
  }

  // Add the new delegate.
  TranslateClient* translate_client = translate_manager->translate_client();
  std::unique_ptr<infobars::InfoBar> infobar(translate_client->CreateInfoBar(
      base::WrapUnique(new TranslateInfoBarDelegate(
          translate_manager, is_off_the_record, step, source_language,
          target_language, error_type, triggered_from_menu))));
  infobar_manager->AddInfoBar(std::move(infobar));
}

size_t TranslateInfoBarDelegate::num_languages() const {
  return ui_delegate_.GetNumberOfLanguages();
}

std::string TranslateInfoBarDelegate::language_code_at(size_t index) const {
  return ui_delegate_.GetLanguageCodeAt(index);
}

std::u16string TranslateInfoBarDelegate::language_name_at(size_t index) const {
  return ui_delegate_.GetLanguageNameAt(index);
}

std::u16string TranslateInfoBarDelegate::source_language_name() const {
  return language_name_at(ui_delegate_.GetSourceLanguageIndex());
}

std::u16string TranslateInfoBarDelegate::target_language_name() const {
  return language_name_at(ui_delegate_.GetTargetLanguageIndex());
}

void TranslateInfoBarDelegate::GetLanguagesNames(
    std::vector<std::u16string>* languages) const {
  DCHECK(languages != nullptr);
  languages->clear();
  for (size_t i = 0; i < ui_delegate_.GetNumberOfLanguages(); ++i) {
    languages->push_back(ui_delegate_.GetLanguageNameAt(i));
  }
}
void TranslateInfoBarDelegate::GetLanguagesCodes(
    std::vector<std::string>* languages_codes) const {
  DCHECK(languages_codes != nullptr);
  languages_codes->clear();

  for (size_t i = 0; i < ui_delegate_.GetNumberOfLanguages(); ++i) {
    languages_codes->push_back(ui_delegate_.GetLanguageCodeAt(i));
  }
}

void TranslateInfoBarDelegate::UpdateSourceLanguage(
    const std::string& language_code) {
  ui_delegate_.UpdateSourceLanguage(language_code);
}

void TranslateInfoBarDelegate::UpdateTargetLanguage(
    const std::string& language_code) {
  ui_delegate_.UpdateTargetLanguage(language_code);
}

void TranslateInfoBarDelegate::OnErrorShown(TranslateErrors::Type error_type) {
  ui_delegate_.OnErrorShown(error_type);
}

void TranslateInfoBarDelegate::Translate() {
  ui_delegate_.Translate();
}

void TranslateInfoBarDelegate::RevertTranslation() {
  ui_delegate_.RevertTranslation();
  infobar()->RemoveSelf();
}

void TranslateInfoBarDelegate::RevertWithoutClosingInfobar() {
  ui_delegate_.RevertTranslation();
  step_ = TRANSLATE_STEP_BEFORE_TRANSLATE;
}

void TranslateInfoBarDelegate::ReportLanguageDetectionError() {
  if (translate_manager_)
    translate_manager_->ReportLanguageDetectionError();
}

void TranslateInfoBarDelegate::TranslationDeclined() {
  ui_delegate_.TranslationDeclined(true);
}

bool TranslateInfoBarDelegate::IsTranslatableLanguageByPrefs() const {
  TranslateClient* client = translate_manager_->translate_client();
  std::unique_ptr<TranslatePrefs> translate_prefs(client->GetTranslatePrefs());
  TranslateAcceptLanguages* accept_languages =
      client->GetTranslateAcceptLanguages();
  return translate_prefs->CanTranslateLanguage(accept_languages,
                                               source_language_code());
}

void TranslateInfoBarDelegate::ToggleTranslatableLanguageByPrefs() {
  ui_delegate_.SetLanguageBlocked(!ui_delegate_.IsLanguageBlocked());
}

bool TranslateInfoBarDelegate::IsSiteOnNeverPromptList() const {
  return ui_delegate_.IsSiteOnNeverPromptList();
}

void TranslateInfoBarDelegate::ToggleNeverPrompt() {
  ui_delegate_.SetNeverPrompt(!ui_delegate_.IsSiteOnNeverPromptList());
}

bool TranslateInfoBarDelegate::ShouldAlwaysTranslate() const {
  return ui_delegate_.ShouldAlwaysTranslate();
}

void TranslateInfoBarDelegate::ToggleAlwaysTranslate() {
  ui_delegate_.SetAlwaysTranslate(!ui_delegate_.ShouldAlwaysTranslate());
}

void TranslateInfoBarDelegate::AlwaysTranslatePageLanguage() {
  DCHECK(!ui_delegate_.ShouldAlwaysTranslate());
  ui_delegate_.SetAlwaysTranslate(true);
  Translate();
}

void TranslateInfoBarDelegate::NeverTranslatePageLanguage() {
  DCHECK(!ui_delegate_.IsLanguageBlocked());
  ui_delegate_.SetLanguageBlocked(true);
  infobar()->RemoveSelf();
}

std::u16string TranslateInfoBarDelegate::GetMessageInfoBarButtonText() {
  if (step_ != translate::TRANSLATE_STEP_TRANSLATE_ERROR) {
    DCHECK_EQ(translate::TRANSLATE_STEP_TRANSLATING, step_);
  } else if ((error_type_ != TranslateErrors::IDENTICAL_LANGUAGES) &&
             (error_type_ != TranslateErrors::UNKNOWN_LANGUAGE)) {
    return l10n_util::GetStringUTF16(
        (error_type_ == TranslateErrors::UNSUPPORTED_LANGUAGE)
            ? IDS_TRANSLATE_INFOBAR_REVERT
            : IDS_TRANSLATE_INFOBAR_RETRY);
  }
  return std::u16string();
}

void TranslateInfoBarDelegate::MessageInfoBarButtonPressed() {
  DCHECK_EQ(translate::TRANSLATE_STEP_TRANSLATE_ERROR, step_);
  if (error_type_ == TranslateErrors::UNSUPPORTED_LANGUAGE) {
    RevertTranslation();
    return;
  }
  // This is the "Try again..." case.
  DCHECK(translate_manager_);
  translate_manager_->TranslatePage(
      source_language_code(), target_language_code(), false,
      translate_manager_->GetActiveTranslateMetricsLogger()
          ->GetNextManualTranslationType());
}

bool TranslateInfoBarDelegate::ShouldShowMessageInfoBarButton() {
  return !GetMessageInfoBarButtonText().empty();
}

bool TranslateInfoBarDelegate::ShouldShowAlwaysTranslateShortcut() {
#if defined(OS_IOS)
  // On mobile, the option to always translate is shown after the translation.
  DCHECK_EQ(translate::TRANSLATE_STEP_AFTER_TRANSLATE, step_);
#else
  // On desktop, the option to always translate is shown before the translation.
  DCHECK_EQ(translate::TRANSLATE_STEP_BEFORE_TRANSLATE, step_);
#endif
  return ui_delegate_.ShouldShowAlwaysTranslateShortcut();
}

bool TranslateInfoBarDelegate::ShouldShowNeverTranslateShortcut() {
  DCHECK_EQ(translate::TRANSLATE_STEP_BEFORE_TRANSLATE, step_);
  return ui_delegate_.ShouldShowNeverTranslateShortcut();
}

#if defined(OS_IOS)
void TranslateInfoBarDelegate::ShowNeverTranslateInfobar() {
  // Return if the infobar is not owned.
  if (!infobar()->owner())
    return;

  Create(true, translate_manager_, infobar()->owner(), is_off_the_record_,
         translate::TRANSLATE_STEP_NEVER_TRANSLATE, source_language_code(),
         target_language_code(), TranslateErrors::NONE, false);
}
#endif

int TranslateInfoBarDelegate::GetTranslationAcceptedCount() {
  return prefs_->GetTranslationAcceptedCount(source_language_code());
}

int TranslateInfoBarDelegate::GetTranslationDeniedCount() {
  return prefs_->GetTranslationDeniedCount(source_language_code());
}

void TranslateInfoBarDelegate::ResetTranslationAcceptedCount() {
  prefs_->ResetTranslationAcceptedCount(source_language_code());
}

void TranslateInfoBarDelegate::ResetTranslationDeniedCount() {
  prefs_->ResetTranslationDeniedCount(source_language_code());
}

void TranslateInfoBarDelegate::GetContentLanguagesCodes(
    std::vector<std::string>* content_codes) const {
  ui_delegate_.GetContentLanguagesCodes(content_codes);
}

bool TranslateInfoBarDelegate::ShouldAutoAlwaysTranslate() {
  // Don't trigger if it's off the record or already set to always translate.
  if (is_off_the_record() || ShouldAlwaysTranslate()) {
    return false;
  }

  bool always_translate =
      (GetTranslationAcceptedCount() >= GetAutoAlwaysThreshold() &&
       GetTranslationAutoAlwaysCount() < GetMaximumNumberOfAutoAlways());

  if (always_translate) {
    // Auto-always will be triggered. Need to increment the auto-always
    // counter.
    IncrementTranslationAutoAlwaysCount();
    // Reset translateAcceptedCount so that auto-always could be triggered
    // again.
    ResetTranslationAcceptedCount();
  }
  return always_translate;
}

bool TranslateInfoBarDelegate::ShouldAutoNeverTranslate() {
  // Don't trigger if it's off the record or language already blocked.
  if (is_off_the_record() || !IsTranslatableLanguageByPrefs()) {
    return false;
  }

  int auto_never_count = GetTranslationAutoNeverCount();

  // At the beginning (auto_never_count == 0), deniedCount starts at 0 and is
  // off-by-one (because this checking is done before increment). However,
  // after auto-never is triggered once (auto_never_count > 0), deniedCount
  // starts at
  // 1.  So there is no off-by-one by then.
  int off_by_one = auto_never_count == 0 ? 1 : 0;

  bool never_translate =
      (GetTranslationDeniedCount() + off_by_one >= GetAutoNeverThreshold() &&
       auto_never_count < GetMaximumNumberOfAutoNever());
  if (never_translate) {
    // Auto-never will be triggered. Need to increment the auto-never counter.
    IncrementTranslationAutoNeverCount();
    // Reset translateDeniedCount so that auto-never could be triggered again.
    ResetTranslationDeniedCount();
  }
  return never_translate;
}

int TranslateInfoBarDelegate::GetTranslationAutoAlwaysCount() {
  return prefs_->GetTranslationAutoAlwaysCount(source_language_code());
}

int TranslateInfoBarDelegate::GetTranslationAutoNeverCount() {
  return prefs_->GetTranslationAutoNeverCount(source_language_code());
}

void TranslateInfoBarDelegate::IncrementTranslationAutoAlwaysCount() {
  prefs_->IncrementTranslationAutoAlwaysCount(source_language_code());
}

void TranslateInfoBarDelegate::IncrementTranslationAutoNeverCount() {
  prefs_->IncrementTranslationAutoNeverCount(source_language_code());
}

// static
void TranslateInfoBarDelegate::GetAfterTranslateStrings(
    std::vector<std::u16string>* strings,
    bool* swap_languages,
    bool autodetermined_source_language) {
  DCHECK(strings);

  if (autodetermined_source_language) {
    size_t offset;
    std::u16string text = l10n_util::GetStringFUTF16(
        IDS_TRANSLATE_INFOBAR_AFTER_MESSAGE_AUTODETERMINED_SOURCE_LANGUAGE,
        std::u16string(), &offset);

    strings->push_back(text.substr(0, offset));
    strings->push_back(text.substr(offset));
    return;
  }
  DCHECK(swap_languages);

  std::vector<size_t> offsets;
  std::u16string text =
      l10n_util::GetStringFUTF16(IDS_TRANSLATE_INFOBAR_AFTER_MESSAGE,
                                 std::u16string(), std::u16string(), &offsets);
  DCHECK_EQ(2U, offsets.size());

  *swap_languages = (offsets[0] > offsets[1]);
  if (*swap_languages)
    std::swap(offsets[0], offsets[1]);

  strings->push_back(text.substr(0, offsets[0]));
  strings->push_back(text.substr(offsets[0], offsets[1] - offsets[0]));
  strings->push_back(text.substr(offsets[1]));
}

TranslateDriver* TranslateInfoBarDelegate::GetTranslateDriver() {
  if (!translate_manager_)
    return NULL;

  return translate_manager_->translate_client()->GetTranslateDriver();
}

void TranslateInfoBarDelegate::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void TranslateInfoBarDelegate::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

TranslateInfoBarDelegate::TranslateInfoBarDelegate(
    const base::WeakPtr<TranslateManager>& translate_manager,
    bool is_off_the_record,
    translate::TranslateStep step,
    const std::string& source_language,
    const std::string& target_language,
    TranslateErrors::Type error_type,
    bool triggered_from_menu)
    : infobars::InfoBarDelegate(),
      is_off_the_record_(is_off_the_record),
      step_(step),
      ui_delegate_(translate_manager, source_language, target_language),
      translate_manager_(translate_manager),
      error_type_(error_type),
      prefs_(translate_manager->translate_client()->GetTranslatePrefs()),
      triggered_from_menu_(triggered_from_menu) {
  DCHECK_NE((step_ == translate::TRANSLATE_STEP_TRANSLATE_ERROR),
            (error_type_ == TranslateErrors::NONE));
  DCHECK(translate_manager_);
}

int TranslateInfoBarDelegate::GetIconId() const {
  return translate_manager_->translate_client()->GetInfobarIconID();
}

void TranslateInfoBarDelegate::InfoBarDismissed() {
  OnInfoBarClosedByUser();
  ReportUIInteraction(UIInteraction::kCloseUIExplicitly);

  bool declined = false;
  bool has_observer = false;
  for (auto& observer : observers_) {
    has_observer = true;
    if (observer.IsDeclinedByUser())
      declined = true;
  }

  if (!has_observer)
    declined = step_ == translate::TRANSLATE_STEP_BEFORE_TRANSLATE;

  if (declined) {
    // The user closed the infobar without clicking the translate button.
    TranslationDeclined();
    UMA_HISTOGRAM_BOOLEAN("Translate.DeclineTranslateCloseInfobar", true);
  }
}

TranslateInfoBarDelegate*
TranslateInfoBarDelegate::AsTranslateInfoBarDelegate() {
  return this;
}

int TranslateInfoBarDelegate::GetAutoAlwaysThreshold() {
  static constexpr base::FeatureParam<int> auto_always_threshold{
      &kTranslateAutoSnackbars, "AutoAlwaysThreshold", kAutoAlwaysThreshold};
  return auto_always_threshold.Get();
}

int TranslateInfoBarDelegate::GetAutoNeverThreshold() {
  static constexpr base::FeatureParam<int> auto_never_threshold{
      &kTranslateAutoSnackbars, "AutoNeverThreshold", kAutoNeverThreshold};
  return auto_never_threshold.Get();
}

int TranslateInfoBarDelegate::GetMaximumNumberOfAutoAlways() {
  static constexpr base::FeatureParam<int> auto_always_maximum{
      &kTranslateAutoSnackbars, "AutoAlwaysMaximum", kMaxNumberOfAutoAlways};
  return auto_always_maximum.Get();
}

int TranslateInfoBarDelegate::GetMaximumNumberOfAutoNever() {
  static constexpr base::FeatureParam<int> auto_never_maximum{
      &kTranslateAutoSnackbars, "AutoNeverMaximum", kMaxNumberOfAutoNever};
  return auto_never_maximum.Get();
}

void TranslateInfoBarDelegate::OnInfoBarClosedByUser() {
  ui_delegate_.OnUIClosedByUser();
}

void TranslateInfoBarDelegate::ReportUIInteraction(
    UIInteraction ui_interaction) {
  ui_delegate_.ReportUIInteraction(ui_interaction);
}

}  // namespace translate
