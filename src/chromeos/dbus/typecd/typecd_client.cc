// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/typecd/typecd_client.h"

#include "base/logging.h"
#include "base/memory/weak_ptr.h"
#include "chromeos/dbus/typecd/fake_typecd_client.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "dbus/object_proxy.h"
#include "third_party/cros_system_api/dbus/typecd/dbus-constants.h"

namespace chromeos {

namespace {
TypecdClient* g_instance = nullptr;
}  // namespace

class TypecdClientImpl : public TypecdClient {
 public:
  TypecdClientImpl() = default;
  TypecdClientImpl(const TypecdClientImpl&) = delete;
  TypecdClientImpl& operator=(const TypecdClientImpl&) = delete;
  ~TypecdClientImpl() override = default;

  void Init(dbus::Bus* bus);

 private:
  void ThunderboltDeviceConnectedReceived(dbus::Signal* signal);
  void OnSignalConnected(const std::string& interface_name,
                         const std::string& signal_name,
                         bool success);

  dbus::ObjectProxy* typecd_proxy_ = nullptr;
  base::WeakPtrFactory<TypecdClientImpl> weak_ptr_factory_{this};
};

// TypecdClientImpl
void TypecdClientImpl::Init(dbus::Bus* bus) {
  typecd_proxy_ = bus->GetObjectProxy(
      typecd::kTypecdServiceName, dbus::ObjectPath(typecd::kTypecdServicePath));

  // Listen to D-Bus signals emitted by typecd.
  typedef void (TypecdClientImpl::*SignalMethod)(dbus::Signal*);
  const std::pair<const char*, SignalMethod> kSignalMethods[] = {
      {typecd::kTypecdDeviceConnected,
       &TypecdClientImpl::ThunderboltDeviceConnectedReceived}};

  auto on_connected_callback = base::BindRepeating(
      &TypecdClientImpl::OnSignalConnected, weak_ptr_factory_.GetWeakPtr());

  for (const auto& signal : kSignalMethods) {
    typecd_proxy_->ConnectToSignal(
        typecd::kTypecdServiceInterface, signal.first,
        base::BindRepeating(signal.second, weak_ptr_factory_.GetWeakPtr()),
        on_connected_callback);
  }
}

void TypecdClientImpl::ThunderboltDeviceConnectedReceived(
    dbus::Signal* signal) {
  dbus::MessageReader reader(signal);
  uint32_t device_connected_type = 0u;
  if (!reader.PopUint32(&device_connected_type)) {
    LOG(ERROR) << "Typecd: Unable to decode connected device type from"
               << typecd::kTypecdDeviceConnected << " signal.";
    return;
  }

  NotifyOnThunderboltDeviceConnected(
      device_connected_type ==
      static_cast<uint32_t>(typecd::DeviceConnectedType::kThunderboltOnly));
}

void TypecdClientImpl::OnSignalConnected(const std::string& interface_name,
                                         const std::string& signal_name,
                                         bool success) {
  if (!success)
    LOG(ERROR) << "Typecd: Failed to connect to signal " << signal_name << ".";
}

// TypecdClient
void TypecdClient::AddObserver(TypecdClient::Observer* observer) {
  observer_list_.AddObserver(observer);
}

void TypecdClient::RemoveObserver(TypecdClient::Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void TypecdClient::NotifyOnThunderboltDeviceConnected(
    bool is_thunderbolt_only) {
  for (auto& observer : observer_list_)
    observer.OnThunderboltDeviceConnected(is_thunderbolt_only);
}

TypecdClient::TypecdClient() {
  CHECK(!g_instance);
  g_instance = this;
}

TypecdClient::~TypecdClient() {
  CHECK_EQ(this, g_instance);
  g_instance = nullptr;
}

// static
void TypecdClient::Initialize(dbus::Bus* bus) {
  CHECK(bus);
  (new TypecdClientImpl())->Init(bus);
}

// static
void TypecdClient::InitializeFake() {
  new FakeTypecdClient();
}

// static
void TypecdClient::Shutdown() {
  CHECK(g_instance);
  delete g_instance;
}

// static
TypecdClient* TypecdClient::Get() {
  return g_instance;
}

}  // namespace chromeos
