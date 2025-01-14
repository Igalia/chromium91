// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "components/autofill/ios/browser/autofill_agent.h"

#include "base/mac/bundle_locations.h"
#include "base/strings/utf_string_conversions.h"
#import "base/test/ios/wait_util.h"
#include "base/test/scoped_feature_list.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/browser/ui/popup_item_ids.h"
#include "components/autofill/core/browser/ui/suggestion.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_prefs.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/ios/browser/autofill_driver_ios.h"
#import "components/autofill/ios/browser/autofill_java_script_feature.h"
#include "components/autofill/ios/form_util/unique_id_data_tab_helper.h"
#include "components/prefs/pref_service.h"
#include "ios/web/public/js_messaging/web_frame_util.h"
#include "ios/web/public/test/fakes/fake_browser_state.h"
#include "ios/web/public/test/fakes/fake_web_frame.h"
#import "ios/web/public/test/fakes/fake_web_frames_manager.h"
#import "ios/web/public/test/fakes/fake_web_state.h"
#include "ios/web/public/test/web_test.h"
#include "services/metrics/public/cpp/ukm_builders.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#import "testing/gtest_mac.h"
#import "third_party/ocmock/OCMock/OCMock.h"
#include "third_party/ocmock/gtest_support.h"
#include "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using autofill::POPUP_ITEM_ID_CLEAR_FORM;
using autofill::POPUP_ITEM_ID_SHOW_ACCOUNT_CARDS;
using autofill::FormRendererId;
using autofill::FieldDataManager;
using autofill::FieldRendererId;
using base::test::ios::WaitUntilCondition;

@interface AutofillAgent (Testing)
- (void)updateFieldManagerWithFillingResults:(NSString*)jsonString;
@end

// Test fixture for AutofillAgent testing.
class AutofillAgentTests : public web::WebTest {
 public:
  AutofillAgentTests() {}

  void AddWebFrame(std::unique_ptr<web::WebFrame> frame) {
    web::WebFrame* frame_ptr = frame.get();
    fake_web_frames_manager_->AddWebFrame(std::move(frame));
    fake_web_state_.OnWebFrameDidBecomeAvailable(frame_ptr);
  }

  void RemoveWebFrame(const std::string& frame_id) {
    web::WebFrame* frame_ptr =
        fake_web_frames_manager_->GetFrameWithId(frame_id);
    fake_web_state_.OnWebFrameWillBecomeUnavailable(frame_ptr);
    fake_web_frames_manager_->RemoveWebFrame(frame_id);
  }

  void SetUp() override {
    web::WebTest::SetUp();

    OverrideJavaScriptFeatures(
        {autofill::AutofillJavaScriptFeature::GetInstance()});

    fake_web_state_.SetBrowserState(GetBrowserState());
    fake_web_state_.SetContentIsHTML(true);
    auto frames_manager = std::make_unique<web::FakeWebFramesManager>();
    fake_web_frames_manager_ = frames_manager.get();
    fake_web_state_.SetWebFramesManager(std::move(frames_manager));

    GURL url("https://example.com");
    fake_web_state_.SetCurrentURL(url);
    auto main_frame = web::FakeWebFrame::Create("frameID", true, url);
    main_frame->set_browser_state(GetBrowserState());
    fake_main_frame_ = main_frame.get();
    AddWebFrame(std::move(main_frame));

    prefs_ = autofill::test::PrefServiceForTesting();
    autofill::prefs::SetAutofillProfileEnabled(prefs_.get(), true);
    autofill::prefs::SetAutofillCreditCardEnabled(prefs_.get(), true);
    UniqueIDDataTabHelper::CreateForWebState(&fake_web_state_);
    autofill_agent_ =
        [[AutofillAgent alloc] initWithPrefService:prefs_.get()
                                          webState:&fake_web_state_];
  }

  std::unique_ptr<web::FakeWebFrame> CreateMainWebFrame() {
    std::unique_ptr<web::FakeWebFrame> frame =
        web::FakeWebFrame::CreateMainWebFrame(GURL());
    frame->set_browser_state(GetBrowserState());
    return frame;
  }

  std::unique_ptr<web::FakeWebFrame> CreateChildWebFrame() {
    std::unique_ptr<web::FakeWebFrame> frame =
        web::FakeWebFrame::CreateChildWebFrame(GURL());
    frame->set_browser_state(GetBrowserState());
    return frame;
  }

  web::FakeWebState fake_web_state_;
  web::FakeWebFrame* fake_main_frame_ = nullptr;
  web::FakeWebFramesManager* fake_web_frames_manager_ = nullptr;
  autofill::TestAutofillClient client_;
  std::unique_ptr<PrefService> prefs_;
  AutofillAgent* autofill_agent_;

  DISALLOW_COPY_AND_ASSIGN(AutofillAgentTests);
};

// Tests that form's name and fields' identifiers, values, and whether they are
// autofilled are sent to the JS. Fields with empty values and those that are
// not autofilled are skipped.
// TODO(crbug/1131038): Remove once using only renderer IDs is launched.
TEST_F(AutofillAgentTests, OnFormDataFilledTestWithFrameMessaging) {
  base::test::ScopedFeatureList scoped_feature_list;
  std::vector<base::Feature> disabled_features;
  disabled_features.push_back(
      autofill::features::kAutofillUseUniqueRendererIDsOnIOS);
  scoped_feature_list.InitWithFeatures({}, disabled_features);

  std::string locale("en");
  autofill::AutofillDriverIOS::PrepareForWebStateWebFrameAndDelegate(
      &fake_web_state_, &client_, nil, locale,
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);

  autofill::FormData form;
  form.url = GURL("https://myform.com");
  form.action = GURL("https://myform.com/submit");
  form.name = u"CC form";
  form.unique_renderer_id = FormRendererId(1);

  autofill::FormFieldData field;
  field.form_control_type = "text";
  field.label = u"Card number";
  field.name = u"number";
  field.name_attribute = field.name;
  field.id_attribute = u"number";
  field.unique_id = field.id_attribute;
  field.value = u"number_value";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(2);
  form.fields.push_back(field);
  field.label = u"Name on Card";
  field.name = u"name";
  field.name_attribute = field.name;
  field.id_attribute = u"name";
  field.unique_id = field.id_attribute;
  field.value = u"name_value";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(3);
  form.fields.push_back(field);
  field.label = u"Expiry Month";
  field.name = u"expiry_month";
  field.name_attribute = field.name;
  field.id_attribute = u"expiry_month";
  field.unique_id = field.id_attribute;
  field.value = u"01";
  field.is_autofilled = false;
  field.unique_renderer_id = FieldRendererId(4);
  form.fields.push_back(field);
  field.label = u"Unknown field";
  field.name = u"unknown";
  field.name_attribute = field.name;
  field.id_attribute = u"unknown";
  field.unique_id = field.id_attribute;
  field.value = u"";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(5);
  form.fields.push_back(field);
  [autofill_agent_
      fillFormData:form
           inFrame:fake_web_state_.GetWebFramesManager()->GetMainWebFrame()];
  fake_web_state_.WasShown();
  EXPECT_EQ(
      "__gCrWeb.autofill.fillForm({\"fields\":{\"name\":{\"section\":\"\","
      "\"value\":\"name_value\"},"
      "\"number\":{\"section\":\"\",\"value\":\"number_value\"}},"
      "\"formName\":\"CC form\",\"formRendererID\":1}, \"\", 0, false);",
      fake_main_frame_->GetLastJavaScriptCall());
}

// Tests that form's name and fields' identifiers, values, and whether they are
// autofilled are sent to the JS. Fields with empty values and those that are
// not autofilled are skipped. Tests logic based on renderer ids usage.
TEST_F(AutofillAgentTests,
       OnFormDataFilledTestWithFrameMessagingUsingRendererIDs) {
  base::test::ScopedFeatureList scoped_feature_list;
  std::vector<base::Feature> enabled_features;
  enabled_features.push_back(
      autofill::features::kAutofillUseUniqueRendererIDsOnIOS);
  scoped_feature_list.InitWithFeatures(enabled_features, {});

  std::string locale("en");
  autofill::AutofillDriverIOS::PrepareForWebStateWebFrameAndDelegate(
      &fake_web_state_, &client_, nil, locale,
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);

  autofill::FormData form;
  form.url = GURL("https://myform.com");
  form.action = GURL("https://myform.com/submit");
  form.name = u"CC form";
  form.unique_renderer_id = FormRendererId(1);

  autofill::FormFieldData field;
  field.form_control_type = "text";
  field.label = u"Card number";
  field.name = u"number";
  field.name_attribute = field.name;
  field.id_attribute = u"number";
  field.unique_id = field.id_attribute;
  field.value = u"number_value";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(2);
  form.fields.push_back(field);
  field.label = u"Name on Card";
  field.name = u"name";
  field.name_attribute = field.name;
  field.id_attribute = u"name";
  field.unique_id = field.id_attribute;
  field.value = u"name_value";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(3);
  form.fields.push_back(field);
  field.label = u"Expiry Month";
  field.name = u"expiry_month";
  field.name_attribute = field.name;
  field.id_attribute = u"expiry_month";
  field.unique_id = field.id_attribute;
  field.value = u"01";
  field.is_autofilled = false;
  field.unique_renderer_id = FieldRendererId(4);
  form.fields.push_back(field);
  field.label = u"Unknown field";
  field.name = u"unknown";
  field.name_attribute = field.name;
  field.id_attribute = u"unknown";
  field.unique_id = field.id_attribute;
  field.value = u"";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(5);
  form.fields.push_back(field);
  [autofill_agent_
      fillFormData:form
           inFrame:fake_web_state_.GetWebFramesManager()->GetMainWebFrame()];
  fake_web_state_.WasShown();
  EXPECT_EQ("__gCrWeb.autofill.fillForm({\"fields\":{\"2\":{\"section\":\"\","
            "\"value\":\"number_value\"},"
            "\"3\":{\"section\":\"\",\"value\":\"name_value\"}},"
            "\"formName\":\"CC form\",\"formRendererID\":1}, \"\", 0, true);",
            fake_main_frame_->GetLastJavaScriptCall());
}

// Tests that in the case of conflict in fields' identifiers, the last seen
// value of a given field is used.
// TODO(crbug/1131038): Remove once using only renderer IDs is launched.
TEST_F(AutofillAgentTests,
       OnFormDataFilledWithNameCollisionTestFrameMessaging) {
  base::test::ScopedFeatureList scoped_feature_list;
  std::vector<base::Feature> disabled_features;
  disabled_features.push_back(
      autofill::features::kAutofillUseUniqueRendererIDsOnIOS);
  scoped_feature_list.InitWithFeatures({}, disabled_features);

  std::string locale("en");
  autofill::AutofillDriverIOS::PrepareForWebStateWebFrameAndDelegate(
      &fake_web_state_, &client_, nil, locale,
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);

  autofill::FormData form;
  form.url = GURL("https://myform.com");
  form.action = GURL("https://myform.com/submit");
  form.unique_renderer_id = FormRendererId(1);

  autofill::FormFieldData field;
  field.form_control_type = "text";
  field.label = u"State";
  field.name = u"region";
  field.name_attribute = field.name;
  field.id_attribute = u"region";
  field.unique_id = field.id_attribute;
  field.value = u"California";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(2);
  form.fields.push_back(field);
  field.label = u"Other field";
  field.name = u"field1";
  field.name_attribute = field.name;
  field.id_attribute = u"field1";
  field.unique_id = field.id_attribute;
  field.value = u"value 1";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(3);
  form.fields.push_back(field);
  field.label = u"Other field";
  field.name = u"field1";
  field.name_attribute = field.name;
  field.id_attribute = u"field1";
  field.unique_id = field.id_attribute;
  field.value = u"value 2";
  field.is_autofilled = true;
  field.unique_renderer_id = FieldRendererId(4);
  form.fields.push_back(field);
  // Fields are in alphabetical order.
  [autofill_agent_
      fillFormData:form
           inFrame:fake_web_state_.GetWebFramesManager()->GetMainWebFrame()];
  fake_web_state_.WasShown();
  EXPECT_EQ("__gCrWeb.autofill.fillForm({\"fields\":{\"field1\":{\"section\":"
            "\"\",\"value\":\"value "
            "2\"},\"region\":{\"section\":\"\",\"value\":\"California\"}},"
            "\"formName\":\"\",\"formRendererID\":1}, \"\", 0, false);",
            fake_main_frame_->GetLastJavaScriptCall());
}

// Tests that when a non user initiated form activity is registered the
// completion callback passed to the call to check if suggestions are available
// is invoked with no suggestions.
TEST_F(AutofillAgentTests,
       CheckIfSuggestionsAvailable_NonUserInitiatedActivity) {
  __block BOOL completion_handler_success = NO;
  __block BOOL completion_handler_called = NO;

  FormSuggestionProviderQuery* form_query =
      [[FormSuggestionProviderQuery alloc] initWithFormName:@"form"
                                               uniqueFormID:FormRendererId(1)
                                            fieldIdentifier:@"address"
                                              uniqueFieldID:FieldRendererId(2)
                                                  fieldType:@"text"
                                                       type:@"focus"
                                                 typedValue:@""
                                                    frameID:@"frameID"];
  [autofill_agent_ checkIfSuggestionsAvailableForForm:form_query
                                          isMainFrame:YES
                                       hasUserGesture:NO
                                             webState:&fake_web_state_
                                    completionHandler:^(BOOL success) {
                                      completion_handler_success = success;
                                      completion_handler_called = YES;
                                    }];
  fake_web_state_.WasShown();

  // Wait until the expected handler is called.
  WaitUntilCondition(^bool() {
    return completion_handler_called;
  });
  EXPECT_FALSE(completion_handler_success);
}

// Tests that "Show credit cards from account" opt-in is shown.
TEST_F(AutofillAgentTests, onSuggestionsReady_ShowAccountCards) {
  __block NSArray<FormSuggestion*>* completion_handler_suggestions = nil;
  __block BOOL completion_handler_called = NO;

  // Make the suggestions available to AutofillAgent.
  std::vector<autofill::Suggestion> suggestions;
  suggestions.push_back(
      autofill::Suggestion("", "", "", POPUP_ITEM_ID_SHOW_ACCOUNT_CARDS));
  [autofill_agent_
      showAutofillPopup:suggestions
          popupDelegate:base::WeakPtr<autofill::AutofillPopupDelegate>()];

  // Retrieves the suggestions.
  auto completionHandler = ^(NSArray<FormSuggestion*>* suggestions,
                             id<FormSuggestionProvider> delegate) {
    completion_handler_suggestions = [suggestions copy];
    completion_handler_called = YES;
  };
  FormSuggestionProviderQuery* form_query =
      [[FormSuggestionProviderQuery alloc] initWithFormName:@"form"
                                               uniqueFormID:FormRendererId(1)
                                            fieldIdentifier:@"address"
                                              uniqueFieldID:FieldRendererId(2)
                                                  fieldType:@"text"
                                                       type:@"focus"
                                                 typedValue:@""
                                                    frameID:@"frameID"];
  [autofill_agent_ retrieveSuggestionsForForm:form_query
                                     webState:&fake_web_state_
                            completionHandler:completionHandler];
  fake_web_state_.WasShown();

  // Wait until the expected handler is called.
  WaitUntilCondition(^bool() {
    return completion_handler_called;
  });

  // "Show credit cards from account" should be the only suggestion.
  EXPECT_EQ(1U, completion_handler_suggestions.count);
  EXPECT_EQ(POPUP_ITEM_ID_SHOW_ACCOUNT_CARDS,
            completion_handler_suggestions[0].identifier);
}

// Tests that when Autofill suggestions are made available to AutofillAgent
// "Clear Form" is moved to the start of the list and the order of other
// suggestions remains unchanged.
TEST_F(AutofillAgentTests, onSuggestionsReady_ClearForm) {
  __block NSArray<FormSuggestion*>* completion_handler_suggestions = nil;
  __block BOOL completion_handler_called = NO;

  // Make the suggestions available to AutofillAgent.
  std::vector<autofill::Suggestion> suggestions;
  suggestions.push_back(autofill::Suggestion("", "", "", 123));
  suggestions.push_back(autofill::Suggestion("", "", "", 321));
  suggestions.push_back(
      autofill::Suggestion("", "", "", POPUP_ITEM_ID_CLEAR_FORM));
  [autofill_agent_
      showAutofillPopup:suggestions
          popupDelegate:base::WeakPtr<autofill::AutofillPopupDelegate>()];

  // Retrieves the suggestions.
  auto completionHandler = ^(NSArray<FormSuggestion*>* suggestions,
                             id<FormSuggestionProvider> delegate) {
    completion_handler_suggestions = [suggestions copy];
    completion_handler_called = YES;
  };
  FormSuggestionProviderQuery* form_query =
      [[FormSuggestionProviderQuery alloc] initWithFormName:@"form"
                                               uniqueFormID:FormRendererId(1)
                                            fieldIdentifier:@"address"
                                              uniqueFieldID:FieldRendererId(2)
                                                  fieldType:@"text"
                                                       type:@"focus"
                                                 typedValue:@""
                                                    frameID:@"frameID"];
  [autofill_agent_ retrieveSuggestionsForForm:form_query
                                     webState:&fake_web_state_
                            completionHandler:completionHandler];
  fake_web_state_.WasShown();

  // Wait until the expected handler is called.
  WaitUntilCondition(^bool() {
    return completion_handler_called;
  });

  // "Clear Form" should appear as the first suggestion. Otherwise, the order of
  // suggestions should not change.
  EXPECT_EQ(3U, completion_handler_suggestions.count);
  EXPECT_EQ(POPUP_ITEM_ID_CLEAR_FORM,
            completion_handler_suggestions[0].identifier);
  EXPECT_EQ(123, completion_handler_suggestions[1].identifier);
  EXPECT_EQ(321, completion_handler_suggestions[2].identifier);
}

// Tests that when Autofill suggestions are made available to AutofillAgent
// GPay icon remains as the first suggestion.
TEST_F(AutofillAgentTests, onSuggestionsReady_ClearFormWithGPay) {
  __block NSArray<FormSuggestion*>* completion_handler_suggestions = nil;
  __block BOOL completion_handler_called = NO;

  // Make the suggestions available to AutofillAgent.
  std::vector<autofill::Suggestion> suggestions;
  suggestions.push_back(autofill::Suggestion("", "", "", 123));
  suggestions.push_back(autofill::Suggestion("", "", "", 321));
  suggestions.push_back(
      autofill::Suggestion("", "", "", POPUP_ITEM_ID_CLEAR_FORM));
  [autofill_agent_
      showAutofillPopup:suggestions
          popupDelegate:base::WeakPtr<autofill::AutofillPopupDelegate>()];

  // Retrieves the suggestions.
  auto completionHandler = ^(NSArray<FormSuggestion*>* suggestions,
                             id<FormSuggestionProvider> delegate) {
    completion_handler_suggestions = [suggestions copy];
    completion_handler_called = YES;
  };
  FormSuggestionProviderQuery* form_query =
      [[FormSuggestionProviderQuery alloc] initWithFormName:@"form"
                                               uniqueFormID:FormRendererId(1)
                                            fieldIdentifier:@"address"
                                              uniqueFieldID:FieldRendererId(2)
                                                  fieldType:@"text"
                                                       type:@"focus"
                                                 typedValue:@""
                                                    frameID:@"frameID"];
  [autofill_agent_ retrieveSuggestionsForForm:form_query
                                     webState:&fake_web_state_
                            completionHandler:completionHandler];
  fake_web_state_.WasShown();

  // Wait until the expected handler is called.
  WaitUntilCondition(^bool() {
    return completion_handler_called;
  });

  EXPECT_EQ(3U, completion_handler_suggestions.count);
  EXPECT_EQ(POPUP_ITEM_ID_CLEAR_FORM,
            completion_handler_suggestions[0].identifier);
  EXPECT_EQ(123, completion_handler_suggestions[1].identifier);
  EXPECT_EQ(321, completion_handler_suggestions[2].identifier);
}

// Test that every frames are processed whatever is the order of pageloading
// callbacks. The main frame should always be processed first.
TEST_F(AutofillAgentTests, FrameInitializationOrderFrames) {
  std::string locale("en");
  autofill::AutofillDriverIOS::PrepareForWebStateWebFrameAndDelegate(
      &fake_web_state_, &client_, nil, locale,
      autofill::AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER);

  // Remove the current main frame.
  RemoveWebFrame(fake_main_frame_->GetFrameId());

  // Both frames available, then page loaded.
  fake_web_state_.SetLoading(true);
  auto main_frame_unique = CreateMainWebFrame();
  web::FakeWebFrame* main_frame = main_frame_unique.get();
  AddWebFrame(std::move(main_frame_unique));
  autofill::AutofillDriverIOS* main_frame_driver =
      autofill::AutofillDriverIOS::FromWebStateAndWebFrame(&fake_web_state_,
                                                           main_frame);
  EXPECT_TRUE(main_frame_driver->IsInMainFrame());
  auto iframe_unique = CreateChildWebFrame();
  iframe_unique->set_call_java_script_function_callback(base::BindRepeating(^{
    EXPECT_TRUE(main_frame_driver->is_processed());
  }));
  web::FakeWebFrame* iframe = iframe_unique.get();
  AddWebFrame(std::move(iframe_unique));
  autofill::AutofillDriverIOS* iframe_driver =
      autofill::AutofillDriverIOS::FromWebStateAndWebFrame(&fake_web_state_,
                                                           iframe);
  EXPECT_FALSE(iframe_driver->IsInMainFrame());
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  fake_web_state_.SetLoading(false);
  fake_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  RemoveWebFrame(main_frame->GetFrameId());
  RemoveWebFrame(iframe->GetFrameId());

  // Main frame available, then page loaded, then iframe available
  main_frame_unique = CreateMainWebFrame();
  main_frame = main_frame_unique.get();
  main_frame_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &fake_web_state_, main_frame);
  iframe_unique = CreateChildWebFrame();
  iframe_unique->set_call_java_script_function_callback(base::BindRepeating(^{
    EXPECT_TRUE(main_frame_driver->is_processed());
  }));
  iframe = iframe_unique.get();
  iframe_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &fake_web_state_, iframe);
  fake_web_state_.SetLoading(true);
  AddWebFrame(std::move(main_frame_unique));
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  fake_web_state_.SetLoading(false);
  fake_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  AddWebFrame(std::move(iframe_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  RemoveWebFrame(main_frame->GetFrameId());
  RemoveWebFrame(iframe->GetFrameId());

  // Page loaded, then main frame, then iframe
  main_frame_unique = CreateMainWebFrame();
  main_frame = main_frame_unique.get();
  main_frame_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &fake_web_state_, main_frame);
  iframe_unique = CreateChildWebFrame();
  iframe_unique->set_call_java_script_function_callback(base::BindRepeating(^{
    EXPECT_TRUE(main_frame_driver->is_processed());
  }));
  iframe = iframe_unique.get();
  iframe_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &fake_web_state_, iframe);
  fake_web_state_.SetLoading(true);
  fake_web_state_.SetLoading(false);
  fake_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  AddWebFrame(std::move(main_frame_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  AddWebFrame(std::move(iframe_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  RemoveWebFrame(main_frame->GetFrameId());
  RemoveWebFrame(iframe->GetFrameId());

  // Page loaded, then iframe, then main frame
  main_frame_unique = CreateMainWebFrame();
  main_frame = main_frame_unique.get();
  main_frame_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &fake_web_state_, main_frame);
  iframe_unique = CreateChildWebFrame();
  iframe_unique->set_call_java_script_function_callback(base::BindRepeating(^{
    EXPECT_TRUE(main_frame_driver->is_processed());
  }));
  iframe = iframe_unique.get();
  iframe_driver = autofill::AutofillDriverIOS::FromWebStateAndWebFrame(
      &fake_web_state_, iframe);
  fake_web_state_.SetLoading(true);
  fake_web_state_.SetLoading(false);
  fake_web_state_.OnPageLoaded(web::PageLoadCompletionStatus::SUCCESS);
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  AddWebFrame(std::move(iframe_unique));
  EXPECT_FALSE(main_frame_driver->is_processed());
  EXPECT_FALSE(iframe_driver->is_processed());
  AddWebFrame(std::move(main_frame_unique));
  EXPECT_TRUE(main_frame_driver->is_processed());
  EXPECT_TRUE(iframe_driver->is_processed());
  RemoveWebFrame(main_frame->GetFrameId());
  RemoveWebFrame(iframe->GetFrameId());
}

TEST_F(AutofillAgentTests, UpdateFieldManagerWithFillingResults) {
  auto test_recorder = std::make_unique<ukm::TestAutoSetUkmRecorder>();

  [autofill_agent_ updateFieldManagerWithFillingResults:@"{\"2\":\"Val1\"}"];

  // Check recorded FieldDataManager data.
  UniqueIDDataTabHelper* uniqueIDDataTabHelper =
      UniqueIDDataTabHelper::FromWebState(&fake_web_state_);
  scoped_refptr<FieldDataManager> fieldDataManager =
      uniqueIDDataTabHelper->GetFieldDataManager();
  EXPECT_TRUE(fieldDataManager->WasAutofilledOnUserTrigger(FieldRendererId(2)));

  // Check recorded UKM.
  auto entries = test_recorder->GetEntriesByName(
      ukm::builders::Autofill_FormFillSuccessIOS::kEntryName);
  // Expect one recorded metric.
  ASSERT_EQ(1u, entries.size());
  test_recorder->ExpectEntryMetric(entries[0], "FormFillSuccess", true);
}
