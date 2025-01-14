// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process.h"

#include <utility>

#include "base/at_exit.h"
#include "base/debug/invalid_access_win.h"
#include "base/process/kill.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "base/threading/platform_thread.h"
#include "base/threading/thread_local.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

#if defined(OS_WIN)
#include "base/win/base_win_buildflags.h"
#include "base/win/windows_version.h"
#endif

namespace {

#if defined(OS_WIN)
constexpr int kExpectedStillRunningExitCode = 0x102;
#else
constexpr int kExpectedStillRunningExitCode = 0;
#endif

constexpr int kDummyExitCode = 42;

#if defined(OS_APPLE)
// Fake port provider that returns the calling process's
// task port, ignoring its argument.
class FakePortProvider : public base::PortProvider {
  mach_port_t TaskForPid(base::ProcessHandle process) const override {
    return mach_task_self();
  }
};
#endif

}  // namespace

namespace base {

class ProcessTest : public MultiProcessTest {
};

TEST_F(ProcessTest, Create) {
  Process process(SpawnChild("SimpleChildProcess"));
  ASSERT_TRUE(process.IsValid());
  ASSERT_FALSE(process.is_current());
  EXPECT_NE(process.Pid(), kNullProcessId);
  process.Close();
  ASSERT_FALSE(process.IsValid());
}

TEST_F(ProcessTest, CreateCurrent) {
  Process process = Process::Current();
  ASSERT_TRUE(process.IsValid());
  ASSERT_TRUE(process.is_current());
  EXPECT_NE(process.Pid(), kNullProcessId);
  process.Close();
  ASSERT_FALSE(process.IsValid());
}

TEST_F(ProcessTest, Move) {
  Process process1(SpawnChild("SimpleChildProcess"));
  EXPECT_TRUE(process1.IsValid());

  Process process2;
  EXPECT_FALSE(process2.IsValid());

  process2 = std::move(process1);
  EXPECT_TRUE(process2.IsValid());
  EXPECT_FALSE(process1.IsValid());
  EXPECT_FALSE(process2.is_current());

  Process process3 = Process::Current();
  process2 = std::move(process3);
  EXPECT_TRUE(process2.is_current());
  EXPECT_TRUE(process2.IsValid());
  EXPECT_FALSE(process3.IsValid());
}

TEST_F(ProcessTest, Duplicate) {
  Process process1(SpawnChild("SimpleChildProcess"));
  ASSERT_TRUE(process1.IsValid());

  Process process2 = process1.Duplicate();
  ASSERT_TRUE(process1.IsValid());
  ASSERT_TRUE(process2.IsValid());
  EXPECT_EQ(process1.Pid(), process2.Pid());
  EXPECT_FALSE(process1.is_current());
  EXPECT_FALSE(process2.is_current());

  process1.Close();
  ASSERT_TRUE(process2.IsValid());
}

TEST_F(ProcessTest, DuplicateCurrent) {
  Process process1 = Process::Current();
  ASSERT_TRUE(process1.IsValid());

  Process process2 = process1.Duplicate();
  ASSERT_TRUE(process1.IsValid());
  ASSERT_TRUE(process2.IsValid());
  EXPECT_EQ(process1.Pid(), process2.Pid());
  EXPECT_TRUE(process1.is_current());
  EXPECT_TRUE(process2.is_current());

  process1.Close();
  ASSERT_TRUE(process2.IsValid());
}

MULTIPROCESS_TEST_MAIN(SleepyChildProcess) {
  PlatformThread::Sleep(TestTimeouts::action_max_timeout());
  return 0;
}

// TODO(https://crbug.com/726484): Enable these tests on Fuchsia when
// CreationTime() is implemented.
#if !defined(OS_FUCHSIA)
TEST_F(ProcessTest, CreationTimeCurrentProcess) {
  // The current process creation time should be less than or equal to the
  // current time.
  EXPECT_FALSE(Process::Current().CreationTime().is_null());
  EXPECT_LE(Process::Current().CreationTime(), Time::Now());
}

#if !defined(OS_ANDROID)  // Cannot read other processes' creation time on
                          // Android.
TEST_F(ProcessTest, CreationTimeOtherProcess) {
  // The creation time of a process should be between a time recorded before it
  // was spawned and a time recorded after it was spawned. However, since the
  // base::Time and process creation clocks don't match, tolerate some error.
  constexpr base::TimeDelta kTolerance =
#if defined(OS_LINUX) || defined(OS_CHROMEOS)
      // On Linux, process creation time is relative to boot time which has a
      // 1-second resolution. Tolerate 1 second for the imprecise boot time and
      // 100 ms for the imprecise clock.
      TimeDelta::FromMilliseconds(1100);
#elif defined(OS_WIN)
      // On Windows, process creation time is based on the system clock while
      // Time::Now() is a combination of system clock and
      // QueryPerformanceCounter(). Tolerate 100 ms for the clock mismatch.
      TimeDelta::FromMilliseconds(100);
#elif defined(OS_APPLE)
      // On Mac, process creation time should be very precise.
      TimeDelta::FromMilliseconds(0);
#else
#error Unsupported platform
#endif
  const Time before_creation = Time::Now();
  Process process(SpawnChild("SleepyChildProcess"));
  const Time after_creation = Time::Now();
  const Time creation = process.CreationTime();
  EXPECT_LE(before_creation - kTolerance, creation);
  EXPECT_LE(creation, after_creation + kTolerance);
  EXPECT_TRUE(process.Terminate(kDummyExitCode, true));
}
#endif  // !defined(OS_ANDROID)
#endif  // !defined(OS_FUCHSIA)

TEST_F(ProcessTest, Terminate) {
  Process process(SpawnChild("SleepyChildProcess"));
  ASSERT_TRUE(process.IsValid());

  int exit_code = kDummyExitCode;
  EXPECT_EQ(TERMINATION_STATUS_STILL_RUNNING,
            GetTerminationStatus(process.Handle(), &exit_code));
  EXPECT_EQ(kExpectedStillRunningExitCode, exit_code);

  exit_code = kDummyExitCode;
  int kExpectedExitCode = 250;
  process.Terminate(kExpectedExitCode, false);
  process.WaitForExitWithTimeout(TestTimeouts::action_max_timeout(),
                                 &exit_code);

  EXPECT_NE(TERMINATION_STATUS_STILL_RUNNING,
            GetTerminationStatus(process.Handle(), &exit_code));
#if defined(OS_WIN)
  // Only Windows propagates the |exit_code| set in Terminate().
  EXPECT_EQ(kExpectedExitCode, exit_code);
#endif
}

void AtExitHandler(void*) {
  // At-exit handler should not be called at
  // Process::TerminateCurrentProcessImmediately.
  DCHECK(false);
}

class ThreadLocalObject {
  ~ThreadLocalObject() {
    // Thread-local storage should not be destructed at
    // Process::TerminateCurrentProcessImmediately.
    DCHECK(false);
  }
};

MULTIPROCESS_TEST_MAIN(TerminateCurrentProcessImmediatelyWithCode0) {
  base::ThreadLocalPointer<ThreadLocalObject> object;
  base::AtExitManager::RegisterCallback(&AtExitHandler, nullptr);
  Process::TerminateCurrentProcessImmediately(0);
}

TEST_F(ProcessTest, TerminateCurrentProcessImmediatelyWithZeroExitCode) {
  Process process(SpawnChild("TerminateCurrentProcessImmediatelyWithCode0"));
  ASSERT_TRUE(process.IsValid());
  int exit_code = 42;
  ASSERT_TRUE(process.WaitForExitWithTimeout(TestTimeouts::action_max_timeout(),
                                             &exit_code));
  EXPECT_EQ(0, exit_code);
}

MULTIPROCESS_TEST_MAIN(TerminateCurrentProcessImmediatelyWithCode250) {
  Process::TerminateCurrentProcessImmediately(250);
}

TEST_F(ProcessTest, TerminateCurrentProcessImmediatelyWithNonZeroExitCode) {
  Process process(SpawnChild("TerminateCurrentProcessImmediatelyWithCode250"));
  ASSERT_TRUE(process.IsValid());
  int exit_code = 42;
  ASSERT_TRUE(process.WaitForExitWithTimeout(TestTimeouts::action_max_timeout(),
                                             &exit_code));
  EXPECT_EQ(250, exit_code);
}

MULTIPROCESS_TEST_MAIN(FastSleepyChildProcess) {
  PlatformThread::Sleep(TestTimeouts::tiny_timeout() * 10);
  return 0;
}

TEST_F(ProcessTest, WaitForExit) {
  Process process(SpawnChild("FastSleepyChildProcess"));
  ASSERT_TRUE(process.IsValid());

  int exit_code = kDummyExitCode;
  EXPECT_TRUE(process.WaitForExit(&exit_code));
  EXPECT_EQ(0, exit_code);
}

TEST_F(ProcessTest, WaitForExitWithTimeout) {
  Process process(SpawnChild("SleepyChildProcess"));
  ASSERT_TRUE(process.IsValid());

  int exit_code = kDummyExitCode;
  TimeDelta timeout = TestTimeouts::tiny_timeout();
  EXPECT_FALSE(process.WaitForExitWithTimeout(timeout, &exit_code));
  EXPECT_EQ(kDummyExitCode, exit_code);

  process.Terminate(kDummyExitCode, false);
}

#if defined(OS_WIN)
TEST_F(ProcessTest, WaitForExitOrEventWithProcessExit) {
  Process process(SpawnChild("FastSleepyChildProcess"));
  ASSERT_TRUE(process.IsValid());

  base::win::ScopedHandle stop_watching_handle(
      CreateEvent(nullptr, TRUE, FALSE, nullptr));

  int exit_code = kDummyExitCode;
  EXPECT_EQ(process.WaitForExitOrEvent(stop_watching_handle, &exit_code),
            base::Process::WaitExitStatus::PROCESS_EXITED);
  EXPECT_EQ(0, exit_code);
}

TEST_F(ProcessTest, WaitForExitOrEventWithEventSet) {
  Process process(SpawnChild("SleepyChildProcess"));
  ASSERT_TRUE(process.IsValid());

  base::win::ScopedHandle stop_watching_handle(
      CreateEvent(nullptr, TRUE, TRUE, nullptr));

  int exit_code = kDummyExitCode;
  EXPECT_EQ(process.WaitForExitOrEvent(stop_watching_handle, &exit_code),
            base::Process::WaitExitStatus::STOP_EVENT_SIGNALED);
  EXPECT_EQ(kDummyExitCode, exit_code);

  process.Terminate(kDummyExitCode, false);
}
#endif  // OS_WIN

// Ensure that the priority of a process is restored correctly after
// backgrounding and restoring.
// Note: a platform may not be willing or able to lower the priority of
// a process. The calls to SetProcessBackground should be noops then.
TEST_F(ProcessTest, SetProcessBackgrounded) {
  if (!Process::CanBackgroundProcesses())
    return;
  Process process(SpawnChild("SimpleChildProcess"));
  int old_priority = process.GetPriority();
#if defined(OS_APPLE)
  // On the Mac, backgrounding a process requires a port to that process.
  // In the browser it's available through the MachBroker class, which is not
  // part of base. Additionally, there is an indefinite amount of time between
  // spawning a process and receiving its port. Because this test just checks
  // the ability to background/foreground a process, we can use the current
  // process's port instead.
  FakePortProvider provider;
  EXPECT_TRUE(process.SetProcessBackgrounded(&provider, true));
  EXPECT_TRUE(process.IsProcessBackgrounded(&provider));
  EXPECT_TRUE(process.SetProcessBackgrounded(&provider, false));
  EXPECT_FALSE(process.IsProcessBackgrounded(&provider));

#else
  EXPECT_TRUE(process.SetProcessBackgrounded(true));
  EXPECT_TRUE(process.IsProcessBackgrounded());
  EXPECT_TRUE(process.SetProcessBackgrounded(false));
  EXPECT_FALSE(process.IsProcessBackgrounded());
#endif
  int new_priority = process.GetPriority();
  EXPECT_EQ(old_priority, new_priority);
}

// Same as SetProcessBackgrounded but to this very process. It uses
// a different code path at least for Windows.
TEST_F(ProcessTest, SetProcessBackgroundedSelf) {
  if (!Process::CanBackgroundProcesses())
    return;
  Process process = Process::Current();
  int old_priority = process.GetPriority();
#if defined(OS_WIN)
  EXPECT_TRUE(process.SetProcessBackgrounded(true));
  EXPECT_TRUE(process.IsProcessBackgrounded());
  EXPECT_TRUE(process.SetProcessBackgrounded(false));
  EXPECT_FALSE(process.IsProcessBackgrounded());
#elif defined(OS_APPLE)
  FakePortProvider provider;
  EXPECT_TRUE(process.SetProcessBackgrounded(&provider, true));
  EXPECT_TRUE(process.IsProcessBackgrounded(&provider));
  EXPECT_TRUE(process.SetProcessBackgrounded(&provider, false));
  EXPECT_FALSE(process.IsProcessBackgrounded(&provider));
#else
  process.SetProcessBackgrounded(true);
  process.SetProcessBackgrounded(false);
#endif
  int new_priority = process.GetPriority();
  EXPECT_EQ(old_priority, new_priority);
}

// Consumers can use WaitForExitWithTimeout(base::TimeDelta(), nullptr) to check
// whether the process is still running. This may not be safe because of the
// potential reusing of the process id. So we won't export Process::IsRunning()
// on all platforms. But for the controllable scenario in the test cases, the
// behavior should be guaranteed.
TEST_F(ProcessTest, CurrentProcessIsRunning) {
  EXPECT_FALSE(Process::Current().WaitForExitWithTimeout(
      base::TimeDelta(), nullptr));
}

#if defined(OS_APPLE)
// On Mac OSX, we can detect whether a non-child process is running.
TEST_F(ProcessTest, PredefinedProcessIsRunning) {
  // Process 1 is the /sbin/launchd, it should be always running.
  EXPECT_FALSE(Process::Open(1).WaitForExitWithTimeout(
      base::TimeDelta(), nullptr));
}
#endif

// Test is disabled on Windows AMR64 because
// TerminateWithHeapCorruption() isn't expected to work there.
// See: https://crbug.com/1054423
#if defined(OS_WIN)
#if defined(ARCH_CPU_ARM64)
#define MAYBE_HeapCorruption DISABLED_HeapCorruption
#else
#define MAYBE_HeapCorruption HeapCorruption
#endif
TEST_F(ProcessTest, MAYBE_HeapCorruption) {
  EXPECT_EXIT(base::debug::win::TerminateWithHeapCorruption(),
              ::testing::ExitedWithCode(STATUS_HEAP_CORRUPTION), "");
}

#if BUILDFLAG(WIN_ENABLE_CFG_GUARDS)
#define MAYBE_ControlFlowViolation ControlFlowViolation
#else
#define MAYBE_ControlFlowViolation DISABLED_ControlFlowViolation
#endif
TEST_F(ProcessTest, MAYBE_ControlFlowViolation) {
  // CFG is only supported on Windows 8.1 or greater.
  if (base::win::GetVersion() < base::win::Version::WIN8_1)
    return;
  // CFG causes ntdll!RtlFailFast2 to be called resulting in uncatchable
  // 0xC0000409 (STATUS_STACK_BUFFER_OVERRUN) exception.
  EXPECT_EXIT(base::debug::win::TerminateWithControlFlowViolation(),
              ::testing::ExitedWithCode(STATUS_STACK_BUFFER_OVERRUN), "");
}

#endif  // OS_WIN

TEST_F(ProcessTest, ChildProcessIsRunning) {
  Process process(SpawnChild("SleepyChildProcess"));
  EXPECT_FALSE(process.WaitForExitWithTimeout(
      base::TimeDelta(), nullptr));
  process.Terminate(0, true);
  EXPECT_TRUE(process.WaitForExitWithTimeout(
      base::TimeDelta(), nullptr));
}

#if BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)

// Tests that the function IsProcessBackgroundedCGroup() can parse the contents
// of the /proc/<pid>/cgroup file successfully.
TEST_F(ProcessTest, TestIsProcessBackgroundedCGroup) {
  const char kNotBackgrounded[] = "5:cpuacct,cpu,cpuset:/daemons\n";
  const char kBackgrounded[] =
      "2:freezer:/chrome_renderers/to_be_frozen\n"
      "1:cpu:/chrome_renderers/background\n";

  EXPECT_FALSE(IsProcessBackgroundedCGroup(kNotBackgrounded));
  EXPECT_TRUE(IsProcessBackgroundedCGroup(kBackgrounded));
}

#endif  // BUILDFLAG(IS_CHROMEOS_ASH) || BUILDFLAG(IS_CHROMEOS_LACROS)

}  // namespace base
