// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/core/browser/ui/payments/card_expiration_date_fix_flow_controller_impl.h"

#include <stddef.h>
#include <memory>

#include "base/bind.h"
#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/data_model/credit_card.h"
#include "components/autofill/core/browser/ui/payments/card_expiration_date_fix_flow_view.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {

class TestCardExpirationDateFixFlowView : public CardExpirationDateFixFlowView {
 public:
  void Show() override {}
  void ControllerGone() override {}
};

class CardExpirationDateFixFlowControllerImplGenericTest {
 public:
  CardExpirationDateFixFlowControllerImplGenericTest() {}

  void ShowPrompt(CreditCard credit_card = CreditCard()) {
    controller_->Show(
        test_card_expiration_date_fix_flow_view_.get(), credit_card,
        base::BindOnce(
            &CardExpirationDateFixFlowControllerImplGenericTest::OnAccepted,
            weak_ptr_factory_.GetWeakPtr()));
  }

 protected:
  std::unique_ptr<TestCardExpirationDateFixFlowView>
      test_card_expiration_date_fix_flow_view_;
  std::unique_ptr<CardExpirationDateFixFlowControllerImpl> controller_;
  std::u16string accepted_month_;
  std::u16string accepted_year_;
  base::WeakPtrFactory<CardExpirationDateFixFlowControllerImplGenericTest>
      weak_ptr_factory_{this};

 private:
  void OnAccepted(const std::u16string& month, const std::u16string& year) {
    accepted_month_ = month;
    accepted_year_ = year;
  }

  DISALLOW_COPY_AND_ASSIGN(CardExpirationDateFixFlowControllerImplGenericTest);
};

class CardExpirationDateFixFlowControllerImplTest
    : public CardExpirationDateFixFlowControllerImplGenericTest,
      public testing::Test {
 public:
  CardExpirationDateFixFlowControllerImplTest() {}
  ~CardExpirationDateFixFlowControllerImplTest() override {}

  void SetUp() override {
    test_card_expiration_date_fix_flow_view_.reset(
        new TestCardExpirationDateFixFlowView());
    controller_.reset(new CardExpirationDateFixFlowControllerImpl());
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(CardExpirationDateFixFlowControllerImplTest);
};

TEST_F(CardExpirationDateFixFlowControllerImplTest, LogShown) {
  base::HistogramTester histogram_tester;
  ShowPrompt();

  histogram_tester.ExpectBucketCount(
      "Autofill.ExpirationDateFixFlowPromptShown", true, 1);
}

TEST_F(CardExpirationDateFixFlowControllerImplTest, LogAccepted) {
  base::HistogramTester histogram_tester;
  ShowPrompt();
  controller_->OnAccepted(u"11", u"30");

  ASSERT_EQ(accepted_month_, u"11");
  ASSERT_EQ(accepted_year_, u"30");
  histogram_tester.ExpectBucketCount(
      "Autofill.ExpirationDateFixFlowPrompt.Events",
      AutofillMetrics::ExpirationDateFixFlowPromptEvent::
          EXPIRATION_DATE_FIX_FLOW_PROMPT_ACCEPTED,
      1);
}

TEST_F(CardExpirationDateFixFlowControllerImplTest, LogDismissed) {
  base::HistogramTester histogram_tester;
  ShowPrompt();
  controller_->OnDismissed();

  histogram_tester.ExpectBucketCount(
      "Autofill.ExpirationDateFixFlowPrompt.Events",
      AutofillMetrics::ExpirationDateFixFlowPromptEvent::
          EXPIRATION_DATE_FIX_FLOW_PROMPT_DISMISSED,
      1);
}

TEST_F(CardExpirationDateFixFlowControllerImplTest, CardIdentifierString) {
  CreditCard card = test::GetCreditCard();
  card.SetNickname(u"nickname");
  ShowPrompt(card);

  EXPECT_EQ(controller_->GetCardLabel(),
            card.NicknameAndLastFourDigitsForTesting());
}

}  // namespace autofill
