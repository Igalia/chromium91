// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/network/cellular_connection_handler.h"

#include <memory>

#include "base/bind.h"
#include "base/callback.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chromeos/network/cellular_inhibitor.h"
#include "chromeos/network/fake_stub_cellular_networks_provider.h"
#include "chromeos/network/network_connection_handler.h"
#include "chromeos/network/network_state_handler.h"
#include "chromeos/network/network_state_test_helper.h"
#include "chromeos/network/test_cellular_esim_profile_handler.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/cros_system_api/dbus/shill/dbus-constants.h"

namespace chromeos {
namespace {

const char kTestCellularDevicePath[] = "cellular_path";
const char kTestCellularDeviceName[] = "cellular_name";

const char kTestBaseProfilePath[] = "profile_path_";
const char kTestBaseServicePath[] = "service_path_";
const char kTestBaseGuid[] = "guid_";
const char kTestBaseName[] = "name_";

const char kTestBaseIccid[] = "1234567890123456789";
const char kTestEuiccBasePath[] = "/org/chromium/Hermes/Euicc/";
const char kTestBaseEid[] = "12345678901234567890123456789012";

std::string CreateTestServicePath(int profile_num) {
  return base::StringPrintf("%s%d", kTestBaseServicePath, profile_num);
}

std::string CreateTestProfilePath(int profile_num) {
  return base::StringPrintf("%s%d", kTestBaseProfilePath, profile_num);
}

std::string CreateTestGuid(int profile_num) {
  return base::StringPrintf("%s%d", kTestBaseGuid, profile_num);
}

std::string CreateTestName(int profile_num) {
  return base::StringPrintf("%s%d", kTestBaseName, profile_num);
}

std::string CreateTestIccid(int profile_num) {
  return base::StringPrintf("%s%d", kTestBaseIccid, profile_num);
}

std::string CreateTestEuiccPath(int euicc_num) {
  return base::StringPrintf("%s%d", kTestEuiccBasePath, euicc_num);
}

std::string CreateTestEid(int euicc_num) {
  return base::StringPrintf("%s%d", kTestBaseEid, euicc_num);
}

}  // namespace

class CellularConnectionHandlerTest : public testing::Test {
 protected:
  CellularConnectionHandlerTest()
      : helper_(/*use_default_devices_and_services=*/false) {}
  ~CellularConnectionHandlerTest() override = default;

  // testing::Test:
  void SetUp() override {
    helper_.network_state_handler()->set_stub_cellular_networks_provider(
        &fake_stubs_provider_);
    inhibitor_.Init(helper_.network_state_handler(),
                    helper_.network_device_handler());
    profile_handler_.Init(helper_.network_state_handler(), &inhibitor_);
    handler_.Init(helper_.network_state_handler(), &inhibitor_,
                  &profile_handler_);
  }

  void CallPrepareExistingCellularNetworkForConnection(int profile_num) {
    handler_.PrepareExistingCellularNetworkForConnection(
        CreateTestIccid(profile_num),
        base::BindOnce(&CellularConnectionHandlerTest::OnSuccess,
                       base::Unretained(this)),
        base::BindOnce(&CellularConnectionHandlerTest::OnFailure,
                       base::Unretained(this)));
  }

  void CallPrepareNewlyInstalledCellularNetworkForConnection(int profile_num,
                                                             int euicc_num) {
    handler_.PrepareNewlyInstalledCellularNetworkForConnection(
        dbus::ObjectPath(CreateTestEuiccPath(euicc_num)),
        dbus::ObjectPath(CreateTestProfilePath(profile_num)), InhibitCellular(),
        base::BindOnce(&CellularConnectionHandlerTest::OnSuccess,
                       base::Unretained(this)),
        base::BindOnce(&CellularConnectionHandlerTest::OnFailure,
                       base::Unretained(this)));
  }

  void AddCellularService(int profile_num) {
    helper_.service_test()->AddService(
        CreateTestServicePath(profile_num), CreateTestGuid(profile_num),
        CreateTestName(profile_num), shill::kTypeCellular, shill::kStateIdle,
        /*visible=*/true);
    base::RunLoop().RunUntilIdle();
  }

  void ExpectServiceConnectable(int profile_num) {
    const NetworkState* network_state =
        helper_.network_state_handler()->GetNetworkState(
            CreateTestServicePath(profile_num));
    EXPECT_TRUE(network_state->connectable());
  }

  void SetServiceConnectable(int profile_num) {
    helper_.service_test()->SetServiceProperty(
        CreateTestServicePath(profile_num), shill::kConnectableProperty,
        base::Value(true));
    base::RunLoop().RunUntilIdle();
  }

  void SetServiceEid(int profile_num, int euicc_num) {
    helper_.service_test()->SetServiceProperty(
        CreateTestServicePath(profile_num), shill::kEidProperty,
        base::Value(CreateTestEid(euicc_num)));
    base::RunLoop().RunUntilIdle();
  }

  void SetServiceIccid(int profile_num) {
    helper_.service_test()->SetServiceProperty(
        CreateTestServicePath(profile_num), shill::kIccidProperty,
        base::Value(CreateTestIccid(profile_num)));
    base::RunLoop().RunUntilIdle();
  }

  void AddCellularDevice() {
    helper_.device_test()->AddDevice(
        kTestCellularDevicePath, shill::kTypeCellular, kTestCellularDeviceName);
    base::RunLoop().RunUntilIdle();
  }

  void QueueEuiccErrorStatus() {
    helper_.hermes_euicc_test()->QueueHermesErrorStatus(
        HermesResponseStatus::kErrorUnknown);
  }

  void AddEuicc(int euicc_num) {
    helper_.hermes_manager_test()->AddEuicc(
        dbus::ObjectPath(CreateTestEuiccPath(euicc_num)),
        CreateTestEid(euicc_num), /*is_active=*/true, /*physical_slot=*/0);
    base::RunLoop().RunUntilIdle();
  }

  void AddProfile(int profile_num, int euicc_num, bool add_service = true) {
    auto add_profile_behavior =
        add_service
            ? HermesEuiccClient::TestInterface::AddCarrierProfileBehavior::
                  kAddProfileWithService
            : HermesEuiccClient::TestInterface::AddCarrierProfileBehavior::
                  kAddDelayedProfileWithoutService;
    std::string iccid = CreateTestIccid(profile_num);

    helper_.hermes_euicc_test()->AddCarrierProfile(
        dbus::ObjectPath(CreateTestProfilePath(profile_num)),
        dbus::ObjectPath(CreateTestEuiccPath(euicc_num)), iccid,
        CreateTestName(profile_num), "service_provider", "activation_code",
        CreateTestServicePath(profile_num), hermes::profile::State::kInactive,
        hermes::profile::ProfileClass::kOperational, add_profile_behavior);

    if (!add_service) {
      fake_stubs_provider_.AddStub(iccid, CreateTestEid(euicc_num));
      helper_.network_state_handler()->SyncStubCellularNetworks();
    }

    base::RunLoop().RunUntilIdle();
  }

  void AdvanceClock(base::TimeDelta time_delta) {
    task_environment_.FastForwardBy(time_delta);
  }

  void ExpectSuccess(const std::string& expected_service_path,
                     base::RunLoop* run_loop) {
    expected_service_path_ = expected_service_path;
    on_success_callback_ = run_loop->QuitClosure();
  }

  void ExpectFailure(const std::string& expected_service_path,
                     const std::string& expected_error_name,
                     base::RunLoop* run_loop) {
    expected_service_path_ = expected_service_path;
    expected_error_name_ = expected_error_name;
    on_failure_callback_ = run_loop->QuitClosure();
  }

  std::unique_ptr<CellularInhibitor::InhibitLock> InhibitCellular() {
    base::RunLoop run_loop;
    std::unique_ptr<CellularInhibitor::InhibitLock> inhibit_lock;
    inhibitor_.InhibitCellularScanning(
        CellularInhibitor::InhibitReason::kRemovingProfile,
        base::BindLambdaForTesting(
            [&](std::unique_ptr<CellularInhibitor::InhibitLock>
                    new_inhibit_lock) {
              inhibit_lock = std::move(new_inhibit_lock);
              run_loop.Quit();
            }));
    run_loop.Run();
    return inhibit_lock;
  }

 private:
  void OnSuccess(const std::string& service_path) {
    EXPECT_EQ(expected_service_path_, service_path);
    std::move(on_success_callback_).Run();
  }

  void OnFailure(const std::string& service_path,
                 const std::string& error_name) {
    EXPECT_EQ(expected_service_path_, service_path);
    EXPECT_EQ(expected_error_name_, error_name);
    std::move(on_failure_callback_).Run();
  }

  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  NetworkStateTestHelper helper_;
  FakeStubCellularNetworksProvider fake_stubs_provider_;
  CellularInhibitor inhibitor_;
  TestCellularESimProfileHandler profile_handler_;
  CellularConnectionHandler handler_;

  base::OnceClosure on_success_callback_;
  base::OnceClosure on_failure_callback_;
  std::string expected_service_path_;
  std::string expected_error_name_;
};

TEST_F(CellularConnectionHandlerTest, NoService) {
  // Note: No cellular service added.

  base::RunLoop run_loop;
  ExpectFailure(/*service_path=*/std::string(),
                NetworkConnectionHandler::kErrorNotFound, &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  run_loop.Run();
}

TEST_F(CellularConnectionHandlerTest, ServiceAlreadyConnectable) {
  AddCellularDevice();
  AddCellularService(/*profile_num=*/1);
  SetServiceIccid(/*profile_num=*/1);
  SetServiceConnectable(/*profile_num=*/1);

  base::RunLoop run_loop;
  ExpectSuccess(CreateTestServicePath(/*profile_num=*/1), &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  run_loop.Run();
}

TEST_F(CellularConnectionHandlerTest, FailsInhibiting) {
  // Note: No cellular device added. This causes the inhibit operation to fail.

  AddCellularService(/*profile_num=*/1);
  SetServiceEid(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceIccid(/*profile_num=*/1);

  base::RunLoop run_loop;
  ExpectFailure(CreateTestServicePath(/*profile_num=*/1),
                NetworkConnectionHandler::kErrorCellularInhibitFailure,
                &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  run_loop.Run();
}

TEST_F(CellularConnectionHandlerTest, NoRelevantEuicc) {
  AddCellularDevice();
  AddCellularService(/*profile_num=*/1);
  SetServiceIccid(/*profile_num=*/1);

  base::RunLoop run_loop;
  ExpectFailure(CreateTestServicePath(/*profile_num=*/1),
                NetworkConnectionHandler::kErrorESimProfileIssue, &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  run_loop.Run();
}

TEST_F(CellularConnectionHandlerTest, FailsRequestingInstalledProfiles) {
  AddCellularDevice();
  AddEuicc(/*euicc_num=*/1);
  AddProfile(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceEid(/*profile_num=*/1, /*euicc_num=*/1);

  QueueEuiccErrorStatus();

  base::RunLoop run_loop;
  ExpectFailure(CreateTestServicePath(/*profile_num=*/1),
                NetworkConnectionHandler::kErrorESimProfileIssue, &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  run_loop.Run();
}

TEST_F(CellularConnectionHandlerTest, TimeoutWaitingForConnectable) {
  const base::TimeDelta kWaitingForConnectableTimeout =
      base::TimeDelta::FromSeconds(30);

  AddCellularDevice();
  AddEuicc(/*euicc_num=*/1);
  AddProfile(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceEid(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceIccid(/*profile_num=*/1);

  base::RunLoop run_loop;
  ExpectSuccess(CreateTestServicePath(/*profile_num=*/1), &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  ExpectFailure(CreateTestServicePath(/*profile_num=*/1),
                NetworkConnectionHandler::kErrorESimProfileIssue, &run_loop);

  // Let all operations run, then wait for the timeout to occur.
  base::RunLoop().RunUntilIdle();
  AdvanceClock(kWaitingForConnectableTimeout);

  run_loop.Run();
}

TEST_F(CellularConnectionHandlerTest, Success) {
  AddCellularDevice();
  AddEuicc(/*euicc_num=*/1);
  AddProfile(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceEid(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceIccid(/*profile_num=*/1);

  base::RunLoop run_loop;
  ExpectSuccess(CreateTestServicePath(/*profile_num=*/1), &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  run_loop.Run();
  ExpectServiceConnectable(/*profile_num=*/1);
}

TEST_F(CellularConnectionHandlerTest, ConnectToStub) {
  AddCellularDevice();
  AddEuicc(/*euicc_num=*/1);
  // Do not add a service; instead, this will cause a fake stub network to be
  // created.
  AddProfile(/*profile_num=*/1, /*euicc_num=*/1, /*add_service=*/false);

  base::RunLoop run_loop;
  // Expect that by the end, we will connect to a "real" (i.e., non-stub)
  // service path.
  ExpectSuccess(CreateTestServicePath(/*profile_num=*/1), &run_loop);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  base::RunLoop().RunUntilIdle();

  // A connection has started to a stub. Because the profile gets enabled,
  // Shill exposes a service and makes it connectable.
  AddCellularService(/*profile_num=*/1);
  SetServiceEid(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceIccid(/*profile_num=*/1);
  SetServiceConnectable(/*profile_num=*/1);

  run_loop.Run();
  ExpectServiceConnectable(/*profile_num=*/1);
}

TEST_F(CellularConnectionHandlerTest, MultipleRequests) {
  AddCellularDevice();
  AddEuicc(/*euicc_num=*/1);
  AddProfile(/*profile_num=*/1, /*euicc_num=*/1);
  AddProfile(/*profile_num=*/2, /*euicc_num=*/1);
  SetServiceEid(/*profile_num=*/1, /*euicc_num=*/1);
  SetServiceEid(/*profile_num=*/2, /*euicc_num=*/1);
  SetServiceIccid(/*profile_num=*/1);
  SetServiceIccid(/*profile_num=*/2);

  base::RunLoop run_loop1;
  ExpectSuccess(CreateTestServicePath(/*profile_num=*/1), &run_loop1);

  // Start both operations.
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/1);
  CallPrepareExistingCellularNetworkForConnection(/*profile_num=*/2);

  // Verify that the first service becomes connectable.
  run_loop1.Run();
  ExpectServiceConnectable(/*profile_num=*/1);

  base::RunLoop run_loop2;
  ExpectSuccess(CreateTestServicePath(/*profile_num=*/2), &run_loop2);

  // Verify that the second service becomes connectable.
  run_loop2.Run();
  ExpectServiceConnectable(/*profile_num=*/2);
}

TEST_F(CellularConnectionHandlerTest, NewProfile) {
  AddCellularDevice();
  AddEuicc(/*euicc_num=*/1);
  AddProfile(/*profile_num=*/1, /*euicc_num=*/1);

  base::RunLoop run_loop;
  ExpectSuccess(CreateTestServicePath(/*profile_num=*/1), &run_loop);
  CallPrepareNewlyInstalledCellularNetworkForConnection(/*euicc_num=*/1,
                                                        /*profile_num=*/1);

  // Verify that service corresponding to new profile becomes
  // connectable.
  run_loop.Run();
  ExpectServiceConnectable(/*profile_num=*/1);
}

}  // namespace chromeos
