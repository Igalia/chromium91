// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/omnibox/browser/clipboard_provider.h"

#include <memory>
#include <string>
#include <utility>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_provider_listener.h"
#include "components/omnibox/browser/mock_autocomplete_provider_client.h"
#include "components/omnibox/browser/test_scheme_classifier.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/open_from_clipboard/fake_clipboard_recent_content.h"
#include "components/search_engines/omnibox_focus_type.h"
#include "components/search_engines/template_url_service.h"
#include "components/search_engines/template_url_service_client.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_unittest_util.h"
#include "url/gurl.h"

namespace {

const char kCurrentURL[] = "http://example.com/current";
const char kClipboardURL[] = "http://example.com/clipboard";
const char kClipboardText[] = "Search for me";
const char kClipboardTitleText[] = "\"Search for me\"";

class CreateMatchWithContentCallbackWaiter {
 public:
  CreateMatchWithContentCallbackWaiter(
      scoped_refptr<ClipboardProvider> provider,
      AutocompleteMatch* match)
      : received_(false) {
    provider->UpdateClipboardMatchWithContent(
        match, base::BindOnce(&CreateMatchWithContentCallbackWaiter::OnComplete,
                              weak_ptr_factory_.GetWeakPtr()));
  }

  void WaitForMatchUpdated() {
    if (received_)
      return;

    base::RunLoop run_loop;
    quit_closure_ = run_loop.QuitClosure();
    run_loop.Run();
  }

 private:
  void OnComplete() {
    received_ = true;
    if (quit_closure_)
      std::move(quit_closure_).Run();
  }

  base::OnceClosure quit_closure_;
  bool received_;

  base::WeakPtrFactory<CreateMatchWithContentCallbackWaiter> weak_ptr_factory_{
      this};
};

}  // namespace

class ClipboardProviderTest : public testing::Test,
                              public AutocompleteProviderListener {
 public:
  ClipboardProviderTest()
      : client_(new MockAutocompleteProviderClient()),
        provider_(new ClipboardProvider(client_.get(),
                                        this,
                                        nullptr,
                                        &clipboard_content_)) {
    SetClipboardUrl(GURL(kClipboardURL));
  }

  ~ClipboardProviderTest() override {}

  void ClearClipboard() { clipboard_content_.SuppressClipboardContent(); }

  void SetClipboardUrl(const GURL& url) {
    clipboard_content_.SetClipboardURL(url, base::TimeDelta::FromMinutes(9));
  }

  void SetClipboardText(const std::u16string& text) {
    clipboard_content_.SetClipboardText(text, base::TimeDelta::FromMinutes(9));
  }

  void SetClipboardImage(const gfx::Image& image) {
    clipboard_content_.SetClipboardImage(image,
                                         base::TimeDelta::FromMinutes(9));
  }

  bool IsClipboardEmpty() {
    return clipboard_content_.GetRecentURLFromClipboard() == base::nullopt &&
           clipboard_content_.GetRecentTextFromClipboard() == base::nullopt &&
           !clipboard_content_.HasRecentImageFromClipboard();
  }

  AutocompleteInput CreateAutocompleteInput(OmniboxFocusType focus_type) {
    AutocompleteInput input(std::u16string(), metrics::OmniboxEventProto::OTHER,
                            classifier_);
    input.set_current_url(GURL(kCurrentURL));
    input.set_focus_type(focus_type);
    return input;
  }

  void MatchesImageCallback(base::Optional<AutocompleteMatch> match) {
    matches_image_match_ = match;
  }

 protected:
  // AutocompleteProviderListener:
  void OnProviderUpdate(bool updated_matches) override;

  TestSchemeClassifier classifier_;
  FakeClipboardRecentContent clipboard_content_;
  std::unique_ptr<MockAutocompleteProviderClient> client_;
  scoped_refptr<ClipboardProvider> provider_;
  base::Optional<AutocompleteMatch> matches_image_match_;

  base::test::TaskEnvironment task_environment_;
};

void ClipboardProviderTest::OnProviderUpdate(bool updated_matches) {
  // No action required.
}

TEST_F(ClipboardProviderTest, NotFromOmniboxFocus) {
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::DEFAULT), false);
  EXPECT_TRUE(provider_->matches().empty());
}

TEST_F(ClipboardProviderTest, EmptyClipboard) {
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  ClearClipboard();
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  EXPECT_TRUE(provider_->matches().empty());
}

#if !defined(OS_ANDROID)
// The following tests do not apply to Android.
// On Android, the Omnibox won't access the content of the system clipboard
// before users click the reveal button, so the clipboard suggestions will be
// empty on start.
TEST_F(ClipboardProviderTest, ClipboardIsCurrentURL) {
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  SetClipboardUrl(GURL(kCurrentURL));
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  EXPECT_TRUE(provider_->matches().empty());
}

TEST_F(ClipboardProviderTest, HasMultipleMatches) {
  EXPECT_CALL(*client_.get(), GetSchemeClassifier())
      .WillOnce(testing::ReturnRef(classifier_));
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  ASSERT_GE(provider_->matches().size(), 1U);
  EXPECT_EQ(GURL(kClipboardURL), provider_->matches().back().destination_url);
}

TEST_F(ClipboardProviderTest, MatchesUrl) {
  SetClipboardUrl(GURL(kClipboardURL));
  EXPECT_CALL(*client_.get(), GetSchemeClassifier())
      .WillOnce(testing::ReturnRef(classifier_));
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  ASSERT_GE(provider_->matches().size(), 1U);
  EXPECT_EQ(GURL(kClipboardURL), provider_->matches().back().destination_url);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_URL,
            provider_->matches().back().type);
}

TEST_F(ClipboardProviderTest, MatchesText) {
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  SetClipboardText(base::UTF8ToUTF16(kClipboardText));
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  ASSERT_GE(provider_->matches().size(), 1U);
  EXPECT_EQ(base::UTF8ToUTF16(kClipboardTitleText),
            provider_->matches().back().contents);
  EXPECT_EQ(base::UTF8ToUTF16(kClipboardText),
            provider_->matches().back().fill_into_edit);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_TEXT,
            provider_->matches().back().type);
}

TEST_F(ClipboardProviderTest, MatchesImage) {
  auto template_url_service =
      std::make_unique<TemplateURLService>(/*initializers=*/nullptr,
                                           /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));

  gfx::Image test_image = gfx::test::CreateImage(/*width=*/10, /*height=*/10);
  scoped_refptr<base::RefCountedMemory> image_bytes =
      provider_->EncodeClipboardImage(*test_image.ToImageSkia());
  ASSERT_TRUE(image_bytes);
  provider_->ConstructImageMatchCallback(
      base::BindOnce(&ClipboardProviderTest::MatchesImageCallback,
                     base::Unretained(this)),
      image_bytes);
  ASSERT_TRUE(matches_image_match_);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_IMAGE, matches_image_match_->type);
}
#endif  // !defined(OS_ANDROID)

TEST_F(ClipboardProviderTest, DeleteMatch) {
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  SetClipboardText(base::UTF8ToUTF16(kClipboardText));
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  ASSERT_EQ(provider_->matches().size(), 1U);

  provider_->DeleteMatch(provider_->matches().back());
  ASSERT_EQ(provider_->matches().size(), 0U);
  ASSERT_TRUE(IsClipboardEmpty());
}

TEST_F(ClipboardProviderTest, CreateBlankURLMatchOnStart) {
  base::test::ScopedFeatureList feature_list;
  base::Feature feature = omnibox::kClipboardSuggestionContentHidden;
  feature_list.InitAndEnableFeature(feature);

  SetClipboardUrl(GURL(kClipboardURL));
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  ASSERT_GE(provider_->matches().size(), 1U);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_URL,
            provider_->matches().back().type);

  // Check the match is empty.
  EXPECT_TRUE(provider_->matches().back().destination_url.is_empty());
}

TEST_F(ClipboardProviderTest, CreateBlankTextMatchOnStart) {
  base::test::ScopedFeatureList feature_list;
  base::Feature feature = omnibox::kClipboardSuggestionContentHidden;
  feature_list.InitAndEnableFeature(feature);

  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  SetClipboardText(base::UTF8ToUTF16(kClipboardText));
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  ASSERT_GE(provider_->matches().size(), 1U);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_TEXT,
            provider_->matches().back().type);

  // Check the match is empty.
  EXPECT_TRUE(provider_->matches().back().contents.empty());
  EXPECT_TRUE(provider_->matches().back().fill_into_edit.empty());
}

TEST_F(ClipboardProviderTest, CreateBlankImageMatchOnStart) {
  base::test::ScopedFeatureList feature_list;
  base::Feature feature = omnibox::kClipboardSuggestionContentHidden;
  feature_list.InitAndEnableFeature(feature);

  auto template_url_service =
      std::make_unique<TemplateURLService>(/*initializers=*/nullptr,
                                           /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));

  gfx::Image test_image = gfx::test::CreateImage(/*width=*/10, /*height=*/10);
  SetClipboardImage(test_image);
  provider_->Start(CreateAutocompleteInput(OmniboxFocusType::ON_FOCUS), false);
  ASSERT_GE(provider_->matches().size(), 1U);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_IMAGE,
            provider_->matches().back().type);
  EXPECT_FALSE(provider_->matches().back().post_content.get());
}

TEST_F(ClipboardProviderTest, CreateURLMatchWithContent) {
  SetClipboardUrl(GURL(kClipboardURL));
  EXPECT_CALL(*client_.get(), GetSchemeClassifier())
      .WillOnce(testing::ReturnRef(classifier_));
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  AutocompleteMatch match = provider_->NewBlankURLMatch();
  CreateMatchWithContentCallbackWaiter waiter(provider_, &match);
  waiter.WaitForMatchUpdated();

  EXPECT_EQ(GURL(kClipboardURL), match.destination_url);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_URL, match.type);
}

TEST_F(ClipboardProviderTest, CreateTextMatchWithContent) {
  SetClipboardText(base::UTF8ToUTF16(kClipboardText));
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  AutocompleteMatch match = provider_->NewBlankTextMatch();
  CreateMatchWithContentCallbackWaiter waiter(provider_, &match);
  waiter.WaitForMatchUpdated();

  EXPECT_EQ(base::UTF8ToUTF16(kClipboardTitleText), match.contents);
  EXPECT_EQ(base::UTF8ToUTF16(kClipboardText), match.fill_into_edit);
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_TEXT, match.type);
}

TEST_F(ClipboardProviderTest, CreateImageMatchWithContent) {
  gfx::Image test_image = gfx::test::CreateImage(/*width=*/10, /*height=*/10);
  SetClipboardImage(test_image);
  auto template_url_service = std::make_unique<TemplateURLService>(
      /*initializers=*/nullptr, /*count=*/0);
  client_->set_template_url_service(std::move(template_url_service));
  AutocompleteMatch match = provider_->NewBlankImageMatch();
  CreateMatchWithContentCallbackWaiter waiter(provider_, &match);
  waiter.WaitForMatchUpdated();

  EXPECT_FALSE(match.post_content->first.empty());
  EXPECT_FALSE(match.post_content->second.empty());
  EXPECT_EQ(AutocompleteMatchType::CLIPBOARD_IMAGE, match.type);
}
