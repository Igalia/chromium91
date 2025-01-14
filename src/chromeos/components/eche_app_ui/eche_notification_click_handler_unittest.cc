// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/eche_app_ui/eche_notification_click_handler.h"

#include "ash/constants/ash_features.h"
#include "base/bind.h"
#include "base/test/scoped_feature_list.h"
#include "chromeos/components/eche_app_ui/fake_feature_status_provider.h"
#include "chromeos/components/phonehub/fake_phone_hub_manager.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace eche_app {

class EcheNotificationClickHandlerTest : public testing::Test {
 protected:
  EcheNotificationClickHandlerTest() = default;
  EcheNotificationClickHandlerTest(const EcheNotificationClickHandlerTest&) =
      delete;
  EcheNotificationClickHandlerTest& operator=(
      const EcheNotificationClickHandlerTest&) = delete;
  ~EcheNotificationClickHandlerTest() override = default;

  // testing::Test:
  void SetUp() override {
    fake_phone_hub_manager_.fake_feature_status_provider()->SetStatus(
        phonehub::FeatureStatus::kEnabledAndConnected);
    fake_feature_status_provider_.SetStatus(FeatureStatus::kIneligible);
    scoped_feature_list_.InitWithFeatures({features::kEcheSWA}, {});
    handler_ = std::make_unique<EcheNotificationClickHandler>(
        &fake_phone_hub_manager_, &fake_feature_status_provider_,
        base::BindRepeating(
            &EcheNotificationClickHandlerTest::FakeLaunchEcheAppFunction,
            base::Unretained(this)));
  }

  void FakeLaunchEcheAppFunction(int64_t notification_id) {
    // Do nothing.
  }

  void SetStatus(FeatureStatus status) {
    fake_feature_status_provider_.SetStatus(status);
  }

  size_t GetNumberOfClickHandlers() {
    return fake_phone_hub_manager_.fake_notification_interaction_handler()
        ->notification_click_handler_count();
  }

 private:
  phonehub::FakePhoneHubManager fake_phone_hub_manager_;
  base::test::ScopedFeatureList scoped_feature_list_;
  eche_app::FakeFeatureStatusProvider fake_feature_status_provider_;
  std::unique_ptr<EcheNotificationClickHandler> handler_;
};

TEST_F(EcheNotificationClickHandlerTest, StatusChangeTransitions) {
  EXPECT_EQ(0u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kDisabled);
  EXPECT_EQ(0u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kDisconnected);
  EXPECT_EQ(1u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kConnecting);
  EXPECT_EQ(1u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kConnected);
  EXPECT_EQ(1u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kDisconnected);
  EXPECT_EQ(1u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kIneligible);
  EXPECT_EQ(0u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kDisconnected);
  EXPECT_EQ(1u, GetNumberOfClickHandlers());
  SetStatus(FeatureStatus::kDisabled);
  EXPECT_EQ(0u, GetNumberOfClickHandlers());
}

}  // namespace eche_app
}  // namespace chromeos
