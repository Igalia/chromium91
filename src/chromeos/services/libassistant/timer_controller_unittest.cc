// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/libassistant/timer_controller.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/task_environment.h"
#include "build/buildflag.h"
#include "chromeos/assistant/internal/buildflags.h"
#include "chromeos/assistant/internal/test_support/fake_alarm_timer_manager.h"
#include "chromeos/assistant/internal/test_support/fake_assistant_manager.h"
#include "chromeos/services/assistant/public/cpp/features.h"
#include "chromeos/services/libassistant/public/mojom/timer_controller.mojom-forward.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(BUILD_LIBASSISTANT_146S)
#include "libassistant/shared/internal_api/alarm_timer_types.h"
#endif  // BUILD_LIBASSISTANT_146S

#if BUILDFLAG(BUILD_LIBASSISTANT_152S)
#include "libassistant/shared/public/alarm_timer_types.h"
#endif  // BUILD_LIBASSISTANT_152S

namespace chromeos {
namespace libassistant {

namespace {

using ::chromeos::assistant::AssistantTimerState;
using ::testing::Invoke;

// Adds an AlarmTimerEvent of the given |type| to |events|.
void AddAlarmTimerEvent(std::vector<assistant_client::AlarmTimerEvent>* events,
                        assistant_client::AlarmTimerEvent::Type type) {
  events->push_back(assistant_client::AlarmTimerEvent());
  events->back().type = type;
}

// Adds an AlarmTimerEvent of type TIMER with the given |state| to |events|.
void AddTimerEvent(std::vector<assistant_client::AlarmTimerEvent>* events,
                   assistant_client::Timer::State state) {
  AddAlarmTimerEvent(events, assistant_client::AlarmTimerEvent::TIMER);
  events->back().timer_data.state = state;
}

class TimerDelegateMock : public mojom::TimerDelegate {
 public:
  TimerDelegateMock() = default;
  TimerDelegateMock(const TimerDelegateMock&) = delete;
  TimerDelegateMock& operator=(const TimerDelegateMock&) = delete;
  ~TimerDelegateMock() override = default;

  // mojom::TimerDelegate implementation:
  MOCK_METHOD(
      void,
      OnTimerStateChanged,
      (const std::vector<::chromeos::assistant::AssistantTimer>& timers));

  mojo::PendingRemote<mojom::TimerDelegate> BindNewPipeAndPassRemote() {
    return receiver_.BindNewPipeAndPassRemote();
  }

  void FlushForTesting() { receiver_.FlushForTesting(); }

 private:
  mojo::Receiver<mojom::TimerDelegate> receiver_{this};
};

}  // namespace

class AssistantTimerControllerTest : public ::testing::Test {
 public:
  AssistantTimerControllerTest() = default;
  AssistantTimerControllerTest(const AssistantTimerControllerTest&) = delete;
  AssistantTimerControllerTest& operator=(const AssistantTimerControllerTest&) =
      delete;
  ~AssistantTimerControllerTest() override = default;

  void SetUp() override {
    controller_.Bind(client_.BindNewPipeAndPassReceiver(),
                     delegate_.BindNewPipeAndPassRemote());
    assistant_manager_ = std::make_unique<assistant::FakeAssistantManager>();
  }

  void StartLibassistant() {
    if (!assistant_manager_)
      assistant_manager_ = std::make_unique<assistant::FakeAssistantManager>();
    controller_.OnAssistantManagerRunning(
        assistant_manager_.get(),
        &assistant_manager_->assistant_manager_internal());
  }

  void StopLibassistant() {
    controller_.OnDestroyingAssistantManager(
        assistant_manager_.get(),
        &assistant_manager_->assistant_manager_internal());

    // Delete the assistant manager so we crash on use-after-free.
    assistant_manager_.reset();
  }

  TimerDelegateMock& delegate() { return delegate_; }

  TimerController& controller() { return controller_; }

  assistant::FakeAlarmTimerManager& fake_alarm_timer_manager() {
    return *static_cast<assistant::FakeAlarmTimerManager*>(
        assistant_manager_->assistant_manager_internal()
            .GetAlarmTimerManager());
  }

 private:
  base::test::SingleThreadTaskEnvironment environment_;
  std::unique_ptr<assistant::FakeAssistantManager> assistant_manager_;
  mojo::Remote<mojom::TimerController> client_;
  testing::StrictMock<TimerDelegateMock> delegate_;
  TimerController controller_;
};

TEST_F(AssistantTimerControllerTest, ShouldNotifyDelegateOfAnyTimers) {
  // We expect OnTimerStateChanged() to be invoked when starting LibAssistant.
  EXPECT_CALL(delegate(), OnTimerStateChanged).Times(1);

  StartLibassistant();
  delegate().FlushForTesting();

  testing::Mock::VerifyAndClearExpectations(&delegate());

  EXPECT_CALL(delegate(), OnTimerStateChanged)
      .WillOnce(Invoke([](const auto& timers) {
        ASSERT_EQ(3u, timers.size());
        EXPECT_EQ(AssistantTimerState::kScheduled, timers[0].state);
        EXPECT_EQ(AssistantTimerState::kPaused, timers[1].state);
        EXPECT_EQ(AssistantTimerState::kFired, timers[2].state);
      }));

  std::vector<assistant_client::AlarmTimerEvent> events;

  // Ignore NONE and ALARMs.
  AddAlarmTimerEvent(&events, assistant_client::AlarmTimerEvent::Type::NONE);
  AddAlarmTimerEvent(&events, assistant_client::AlarmTimerEvent::Type::ALARM);

  // Accept SCHEDULED/PAUSED/FIRED timers.
  AddTimerEvent(&events, assistant_client::Timer::State::SCHEDULED);
  AddTimerEvent(&events, assistant_client::Timer::State::PAUSED);
  AddTimerEvent(&events, assistant_client::Timer::State::FIRED);

  fake_alarm_timer_manager().SetAllEvents(std::move(events));
  fake_alarm_timer_manager().NotifyRingingStateListeners();
  base::RunLoop().RunUntilIdle();
}

TEST_F(AssistantTimerControllerTest,
       ShouldNotifyDelegateOfTimersWhenStartingLibAssistant) {
  // Pre-populate the AlarmTimerManager with a single scheduled timer.
  std::vector<assistant_client::AlarmTimerEvent> events;
  AddTimerEvent(&events, assistant_client::Timer::State::SCHEDULED);
  fake_alarm_timer_manager().SetAllEvents(std::move(events));

  // Expect |timers| to be sent to AssistantDelegate.
  // Verify AssistantDelegate is notified of the scheduled timer.
  EXPECT_CALL(delegate(), OnTimerStateChanged)
      .WillOnce(Invoke([](const auto& timers) {
        ASSERT_EQ(1u, timers.size());
        EXPECT_EQ(AssistantTimerState::kScheduled, timers[0].state);
      }));

  StartLibassistant();
  delegate().FlushForTesting();
}

TEST_F(AssistantTimerControllerTest, ShouldNotCrashIfLibassistantIsNotStarted) {
  controller().AddTimeToTimer("timer-id", /*duration=*/base::TimeDelta());
  controller().PauseTimer("timer-id");
  controller().RemoveTimer("timer-id");
  controller().ResumeTimer("timer-id");
}

TEST_F(AssistantTimerControllerTest, ShouldNotCrashAfterStoppingLibassistant) {
  StartLibassistant();
  StopLibassistant();

  controller().AddTimeToTimer("timer-id", /*duration=*/base::TimeDelta());
  controller().PauseTimer("timer-id");
  controller().RemoveTimer("timer-id");
  controller().ResumeTimer("timer-id");
}

}  // namespace libassistant
}  // namespace chromeos
