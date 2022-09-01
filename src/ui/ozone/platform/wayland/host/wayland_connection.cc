// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/wayland/host/wayland_connection.h"

#include <extended-drag-unstable-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-shell-unstable-v6-client-protocol.h>

#include <algorithm>
#include <memory>
#include <vector>

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_util.h"
#include "base/task/current_thread.h"
#include "base/threading/thread_task_runner_handle.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/input_device.h"
#include "ui/events/ozone/layout/keyboard_layout_engine_manager.h"
#include "ui/gfx/geometry/point.h"
#include "ui/ozone/platform/wayland/common/wayland_object.h"
#include "ui/ozone/platform/wayland/host/gtk_primary_selection_device_manager.h"
#include "ui/ozone/platform/wayland/host/proxy/wayland_proxy_impl.h"
#include "ui/ozone/platform/wayland/host/wayland_buffer_manager_host.h"
#include "ui/ozone/platform/wayland/host/wayland_clipboard.h"
#include "ui/ozone/platform/wayland/host/wayland_cursor.h"
#include "ui/ozone/platform/wayland/host/wayland_cursor_position.h"
#include "ui/ozone/platform/wayland/host/wayland_data_device_manager.h"
#include "ui/ozone/platform/wayland/host/wayland_data_drag_controller.h"
#if !defined(OS_WEBOS)
#include "ui/ozone/platform/wayland/host/wayland_drm.h"
#endif  // !defined(OS_WEBOS)
#include "ui/ozone/platform/wayland/host/wayland_event_source.h"
#include "ui/ozone/platform/wayland/host/wayland_input_method_context.h"
#if !defined(USE_NEVA_APPRUNTIME)
#include "ui/ozone/platform/wayland/host/wayland_keyboard.h"
#endif  // !defined(USE_NEVA_APPRUNTIME)
#include "ui/ozone/platform/wayland/host/wayland_output_manager.h"
#if !defined(USE_NEVA_APPRUNTIME)
#include "ui/ozone/platform/wayland/host/wayland_pointer.h"
#endif  // !defined(USE_NEVA_APPRUNTIME)
#include "ui/ozone/platform/wayland/host/wayland_shm.h"
#if !defined(USE_NEVA_APPRUNTIME)
#include "ui/ozone/platform/wayland/host/wayland_touch.h"
#endif  // !defined(USE_NEVA_APPRUNTIME)
#include "ui/ozone/platform/wayland/host/wayland_window.h"
#include "ui/ozone/platform/wayland/host/wayland_window_drag_controller.h"
#include "ui/ozone/platform/wayland/host/wayland_zaura_shell.h"
#include "ui/ozone/platform/wayland/host/wayland_zcr_cursor_shapes.h"
#include "ui/ozone/platform/wayland/host/wayland_zwp_linux_dmabuf.h"
#include "ui/ozone/platform/wayland/host/xdg_foreign_wrapper.h"
#include "ui/ozone/platform/wayland/host/zwp_primary_selection_device_manager.h"
#include "ui/platform_window/common/platform_window_defaults.h"

#if defined(USE_LIBWAYLAND_STUBS)
#include <dlfcn.h>

#include "third_party/wayland/libwayland_stubs.h"  // nogncheck
#endif

///@name USE_NEVA_APPRUNTIME
///@{
#include "ui/ozone/platform/wayland/host/wayland_extensions.h"
///@}

namespace ui {

namespace {
// The maximum supported versions for a given interface.
// The version bound will be the minimum of the value and the version
// advertised by the server.
constexpr uint32_t kMaxAuraShellVersion = 16;
constexpr uint32_t kMaxCompositorVersion = 4;
constexpr uint32_t kMaxCursorShapesVersion = 1;
constexpr uint32_t kMaxGtkPrimarySelectionDeviceManagerVersion = 1;
constexpr uint32_t kMaxKeyboardExtensionVersion = 2;
constexpr uint32_t kMaxLinuxDmabufVersion = 3;
constexpr uint32_t kMaxSeatVersion = 5;
constexpr uint32_t kMaxShmVersion = 1;
constexpr uint32_t kMaxXdgShellVersion = 1;
constexpr uint32_t kMinDeviceManagerVersion = 1;
constexpr uint32_t kMaxDeviceManagerVersion = 3;
constexpr uint32_t kMaxWpPresentationVersion = 1;
constexpr uint32_t kMaxWpViewporterVersion = 1;
constexpr uint32_t kMaxTextInputManagerVersion = 1;
constexpr uint32_t kMaxExplicitSyncVersion = 2;
constexpr uint32_t kMaxXdgDecorationVersion = 1;
constexpr uint32_t kMaxExtendedDragVersion = 1;
constexpr uint32_t kMaxXdgExporterVersion = 1;
constexpr uint32_t kMaxWlDrmVersion = 2;
// The minimum required version for a given interface.
// Ensures that the version bound (advertised by server) is higher than this
// value.
constexpr uint32_t kMinAuraShellVersion = 1;
constexpr uint32_t kMinCompositorVersion = 1;
constexpr uint32_t kMinExplicitSyncVersion = 1;
constexpr uint32_t kMinKeyboardExtensionVersion = 1;
constexpr uint32_t kMinLinuxDmabufVersion = 1;
constexpr uint32_t kMinSeatVersion = 1;
constexpr uint32_t kMinWlDrmVersion = 2;
constexpr uint32_t kMinWlOutputVersion = 2;
}  // namespace

#if defined(USE_NEVA_APPRUNTIME)
WaylandConnection::WaylandConnection()
    : seat_manager_(std::make_unique<WaylandSeatManager>()) {}
#else   // defined(USE_NEVA_APPRUNTIME)
WaylandConnection::WaylandConnection() = default;
#endif  // !defined(USE_NEVA_APPRUNTIME)

WaylandConnection::~WaylandConnection() = default;

bool WaylandConnection::Initialize() {
#if defined(USE_LIBWAYLAND_STUBS)
  // Use RTLD_NOW to load all symbols, since the stubs will try to load all of
  // them anyway.  Use RTLD_GLOBAL to add the symbols to the global namespace.
  auto dlopen_flags = RTLD_NOW | RTLD_GLOBAL;
  if (void* libwayland_client =
          dlopen("libwayland-client.so.0", dlopen_flags)) {
    third_party_wayland::InitializeLibwaylandclient(libwayland_client);
  } else {
    LOG(ERROR) << "Failed to load wayland client libraries.";
    return false;
  }

#if defined(OS_WEBOS)
  // TODO(sergey.kipet@lge.com): we'd better avoid using libwayland-stubs to
  // prevent possible Wayland protocol incompatibility issues due to updating
  // of the Wayland protocols isn't (currently) supported in webOS.
  if (void* libwayland_egl = dlopen("libwayland-egl.so.0.1", dlopen_flags))
#else   // defined(OS_WEBOS)
  if (void* libwayland_egl = dlopen("libwayland-egl.so.1", dlopen_flags))
#endif  // !defined(OS_WEBOS)
    third_party_wayland::InitializeLibwaylandegl(libwayland_egl);

  // TODO(crbug.com/1081784): consider handling this in more flexible way.
  // libwayland-cursor is said to be part of the standard shipment of Wayland,
  // and it seems unlikely (although possible) that it would be unavailable
  // while libwayland-client was present.  To handle that gracefully, chrome can
  // fall back to the generic Ozone behaviour.
  if (void* libwayland_cursor =
          dlopen("libwayland-cursor.so.0", dlopen_flags)) {
    third_party_wayland::InitializeLibwaylandcursor(libwayland_cursor);
  } else {
    LOG(ERROR) << "Failed to load libwayland-cursor.so.0.";
    return false;
  }
#endif

  static const wl_registry_listener registry_listener = {
      &WaylandConnection::Global,
      &WaylandConnection::GlobalRemove,
  };

  display_.reset(wl_display_connect(nullptr));
  if (!display_) {
    LOG(ERROR) << "Failed to connect to Wayland display";
    return false;
  }

  registry_.reset(wl_display_get_registry(display_.get()));
  if (!registry_) {
    LOG(ERROR) << "Failed to get Wayland registry";
    return false;
  }

  ///@name USE_NEVA_APPRUNTIME
  ///@{
  if (!extensions_) {
    extensions_ = CreateWaylandExtensions(this);
  }
  ///@}

  // Now that the connection with the display server has been properly
  // estabilished, initialize the event source and input objects.
  DCHECK(!event_source_);
  event_source_ =
      std::make_unique<WaylandEventSource>(display(), wayland_window_manager());

  wl_registry_add_listener(registry_.get(), &registry_listener, this);
  while (!wayland_output_manager_ ||
         !wayland_output_manager_->IsOutputReady()) {
    wl_display_roundtrip(display_.get());
  }

  buffer_manager_host_ = std::make_unique<WaylandBufferManagerHost>(this);

  if (!compositor_) {
    LOG(ERROR) << "No wl_compositor object";
    return false;
  }
  if (!shm_) {
    LOG(ERROR) << "No wl_shm object";
    return false;
  }
  if (!shell_v6_ && !shell_
  ///@name USE_NEVA_APPRUNTIME
  ///@{
  && !(extensions_ && extensions_->HasShellObject())
  ///@}
  ) {
    LOG(ERROR) << "No Wayland shell found";
    return false;
  }

  // When we are running tests with weston in headless mode, the seat is not
  // announced.
#if defined(USE_NEVA_APPRUNTIME)
  if (!seat())
#else   // defined(USE_NEVA_APPRUNTIME)
  if (!seat_)
#endif  // !defined(USE_NEVA_APPRUNTIME)
    LOG(WARNING) << "No wl_seat object. The functionality may suffer.";

#if defined(USE_NEVA_MEDIA)
  video_window_provider_impl_ = std::make_unique<VideoWindowProviderImpl>();
  video_window_provider_impl_->SetDelegate(
      extensions_->GetVideoWindowProviderDelegate());
#endif  // defined(USE_NEVA_MEDIA)

  if (UseTestConfigForPlatformWindows())
    wayland_proxy_ = std::make_unique<wl::WaylandProxyImpl>(this);

  return true;
}

#if defined(USE_NEVA_MEDIA)
void WaylandConnection::BindVideoWindowProviderClient(
    mojo::Remote<mojom::VideoWindowProviderClient> remote) {
  video_window_controller_mojo_ = std::make_unique<VideoWindowControllerMojo>(
      video_window_provider_impl_.get(), std::move(remote));
  video_window_provider_impl_->SetVideoWindowController(
      video_window_controller_mojo_.get());
}
#endif  // defined(USE_NEVA_MEDIA)

void WaylandConnection::ScheduleFlush() {
  // When we are in tests, the message loop is set later when the
  // initialization of the OzonePlatform complete. Thus, just
  // flush directly. This doesn't happen in normal run.
  if (!base::CurrentUIThread::IsSet()) {
    Flush();
  } else if (!scheduled_flush_) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(&WaylandConnection::Flush, base::Unretained(this)));
    scheduled_flush_ = true;
  }
}

void WaylandConnection::SetShutdownCb(base::OnceCallback<void()> shutdown_cb) {
  event_source()->SetShutdownCb(std::move(shutdown_cb));
}

#if defined(USE_NEVA_APPRUNTIME)
wl_seat* WaylandConnection::seat() const {
  DCHECK(seat_manager_);
  if (seat_manager_->GetFirstSeat())
    return seat_manager_->GetFirstSeat()->seat();
  return nullptr;
}

WaylandCursor* WaylandConnection::cursor() const {
  DCHECK(seat_manager_);
  if (seat_manager_->GetFirstSeat())
    return seat_manager_->GetFirstSeat()->cursor();
  return nullptr;
}

WaylandTouch* WaylandConnection::touch() const {
  DCHECK(seat_manager_);
  if (seat_manager_->GetFirstSeat())
    return seat_manager_->GetFirstSeat()->touch();
  return nullptr;
}

WaylandPointer* WaylandConnection::pointer() const {
  DCHECK(seat_manager_);
  if (seat_manager_->GetFirstSeat())
    return seat_manager_->GetFirstSeat()->pointer();
  return nullptr;
}

WaylandKeyboard* WaylandConnection::keyboard() const {
  DCHECK(seat_manager_);
  if (seat_manager_->GetFirstSeat())
    return seat_manager_->GetFirstSeat()->keyboard();
  return nullptr;
}

WaylandCursorPosition* WaylandConnection::wayland_cursor_position() const {
  DCHECK(seat_manager_);
  if (seat_manager_->GetFirstSeat())
    return seat_manager_->GetFirstSeat()->cursor_position();
  return nullptr;
}
#endif  // defined(USE_NEVA_APPRUNTIME)

void WaylandConnection::SetPlatformCursor(wl_cursor* cursor_data,
                                          int buffer_scale) {
#if !defined(USE_NEVA_APPRUNTIME)
  if (!cursor_)
    return;
  cursor_->SetPlatformShape(cursor_data, buffer_scale);
#endif  // !defined(USE_NEVA_APPRUNTIME)
}

void WaylandConnection::SetCursorBufferListener(
    WaylandCursorBufferListener* listener) {
#if !defined(USE_NEVA_APPRUNTIME)
  listener_ = listener;
  if (!cursor_)
    return;
  cursor_->set_listener(listener_);
#endif  // !defined(USE_NEVA_APPRUNTIME)
}

void WaylandConnection::SetCursorBitmap(const std::vector<SkBitmap>& bitmaps,
                                        const gfx::Point& hotspot_in_dips,
                                        int buffer_scale) {
#if defined(USE_NEVA_APPRUNTIME)
  DCHECK(seat_manager_);
  seat_manager_->UpdateCursorBitmap(bitmaps, hotspot_in_dips, buffer_scale);
#else   // defined(USE_NEVA_APPRUNTIME)
  if (!cursor_)
    return;
  cursor_->UpdateBitmap(bitmaps, hotspot_in_dips, buffer_scale);
#endif  // !defined(USE_NEVA_APPRUNTIME)
}

bool WaylandConnection::IsDragInProgress() const {
  // |data_drag_controller_| can be null when running on headless weston.
  return data_drag_controller_ && data_drag_controller_->state() !=
                                      WaylandDataDragController::State::kIdle;
}

wl::Object<wl_surface> WaylandConnection::CreateSurface() {
  DCHECK(compositor_);
  return wl::Object<wl_surface>(
      wl_compositor_create_surface(compositor_.get()));
}

void WaylandConnection::Flush() {
  wl_display_flush(display_.get());
  scheduled_flush_ = false;
}

#if !defined(USE_NEVA_APPRUNTIME)
void WaylandConnection::UpdateInputDevices(wl_seat* seat,
                                           uint32_t capabilities) {
  DCHECK(seat);
  DCHECK(event_source_);
  auto has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
  auto has_keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
  auto has_touch = capabilities & WL_SEAT_CAPABILITY_TOUCH;

  // Container for devices. Can be empty.
  std::vector<InputDevice> devices;

  if (!has_pointer) {
    pointer_.reset();
    cursor_.reset();
    wayland_cursor_position_.reset();
  } else if (!pointer_) {
    if (wl_pointer* pointer = wl_seat_get_pointer(seat)) {
      pointer_ =
          std::make_unique<WaylandPointer>(pointer, this, event_source());
      cursor_ = std::make_unique<WaylandCursor>(pointer_.get(), this);
      cursor_->set_listener(listener_);
      wayland_cursor_position_ = std::make_unique<WaylandCursorPosition>();

      // Wayland doesn't expose InputDeviceType.
      devices.emplace_back(InputDevice(
          pointer_->id(), InputDeviceType::INPUT_DEVICE_UNKNOWN, "pointer"));
    } else {
      LOG(ERROR) << "Failed to get wl_pointer from seat";
    }
  }

  // Notify about mouse changes.
  GetHotplugEventObserver()->OnMouseDevicesUpdated(devices);

  // Clear the local container to store a keyboard device now.
  devices.clear();
  if (!has_keyboard) {
    keyboard_.reset();
  } else if (!keyboard_) {
    if (!CreateKeyboard()) {
      LOG(ERROR) << "Failed to create WaylandKeyboard";
    } else {
      // Wayland doesn't expose InputDeviceType.
      devices.emplace_back(InputDevice(
          keyboard_->id(), InputDeviceType::INPUT_DEVICE_UNKNOWN, "keyboard"));
    }
  }

  // Notify about mouse changes.
  GetHotplugEventObserver()->OnKeyboardDevicesUpdated(devices);

  // TODO(msisov): wl_touch doesn't expose the display it belongs to. Thus, it's
  // impossible to figure out the size of the touchscreen for TouchscreenDevice
  // struct that should be passed to a DeviceDataManager instance.
  if (!has_touch) {
    touch_.reset();
  } else if (!touch_) {
    if (wl_touch* touch = wl_seat_get_touch(seat)) {
      touch_ = std::make_unique<WaylandTouch>(touch, this, event_source());
    } else {
      LOG(ERROR) << "Failed to get wl_touch from seat";
    }
  }

  // Notify update completed.
  GetHotplugEventObserver()->OnDeviceListsComplete();
}

bool WaylandConnection::CreateKeyboard() {
  wl_keyboard* keyboard = wl_seat_get_keyboard(seat_.get());
  if (!keyboard)
    return false;

  auto* layout_engine = KeyboardLayoutEngineManager::GetKeyboardLayoutEngine();
  // Make sure to destroy the old WaylandKeyboard (if it exists) before creating
  // the new one.
  keyboard_.reset();
  keyboard_.reset(new WaylandKeyboard(keyboard, keyboard_extension_v1_.get(),
                                      this, layout_engine, event_source()));
  return true;
}
#endif  // !defined(USE_NEVA_APPRUNTIME)

DeviceHotplugEventObserver* WaylandConnection::GetHotplugEventObserver() {
  return DeviceDataManager::GetInstance();
}

void WaylandConnection::CreateDataObjectsIfReady() {
#if defined(USE_NEVA_APPRUNTIME)
  if (data_device_manager_ && seat()) {
#else   // defined(USE_NEVA_APPRUNTIME)
  if (data_device_manager_ && seat_) {
#endif  // !defined(USE_NEVA_APPRUNTIME)
    DCHECK(!data_drag_controller_);
    data_drag_controller_ = std::make_unique<WaylandDataDragController>(
        this, data_device_manager_.get());

    DCHECK(!window_drag_controller_);
    window_drag_controller_ = std::make_unique<WaylandWindowDragController>(
        this, data_device_manager_.get(), event_source());

    DCHECK(!clipboard_);
    clipboard_ =
        std::make_unique<WaylandClipboard>(this, data_device_manager_.get());
  }
}

// static
void WaylandConnection::Global(void* data,
                               wl_registry* registry,
                               uint32_t name,
                               const char* interface,
                               uint32_t version) {
#if !defined(USE_NEVA_APPRUNTIME)
  static const wl_seat_listener seat_listener = {
      &WaylandConnection::Capabilities,
      &WaylandConnection::Name,
  };
#endif  // !defined(USE_NEVA_APPRUNTIME)
  static const xdg_wm_base_listener shell_listener = {
      &WaylandConnection::Ping,
  };
  static const zxdg_shell_v6_listener shell_v6_listener = {
      &WaylandConnection::PingV6,
  };

  WaylandConnection* connection = static_cast<WaylandConnection*>(data);

  ///@name USE_NEVA_APPRUNTIME
  ///@{
  if (connection->extensions_->Bind(registry, name, interface, version)) {
    DVLOG(1) << "Successfully bound to " << interface;
  } else
  ///@}
  if (!connection->compositor_ && strcmp(interface, "wl_compositor") == 0 &&
      wl::CanBind(interface, version, kMinCompositorVersion,
                  kMaxCompositorVersion)) {
    connection->compositor_ = wl::Bind<wl_compositor>(
        registry, name, std::min(version, kMaxCompositorVersion));
    connection->compositor_version_ = version;
    if (!connection->compositor_)
      LOG(ERROR) << "Failed to bind to wl_compositor global";
  } else if (!connection->subcompositor_ &&
             strcmp(interface, "wl_subcompositor") == 0) {
    connection->subcompositor_ = wl::Bind<wl_subcompositor>(registry, name, 1);
    if (!connection->subcompositor_)
      LOG(ERROR) << "Failed to bind to wl_subcompositor global";
  } else if (!connection->shm_ && strcmp(interface, "wl_shm") == 0 &&
             wl::CanBind(interface, version, kMaxShmVersion, kMaxShmVersion)) {
    wl::Object<wl_shm> shm =
        wl::Bind<wl_shm>(registry, name, std::min(version, kMaxShmVersion));
    connection->shm_ = std::make_unique<WaylandShm>(shm.release(), connection);
    if (!connection->shm_)
      LOG(ERROR) << "Failed to bind to wl_shm global";
#if defined(USE_NEVA_APPRUNTIME)
  } else if (strcmp(interface, "wl_seat") == 0 &&
             wl::CanBind(interface, version, kMinSeatVersion,
                         kMaxSeatVersion)) {
    wl::Object<wl_seat> seat =
        wl::Bind<wl_seat>(registry, name, std::min(version, kMaxSeatVersion));
    if (!seat) {
      LOG(ERROR) << "Failed to bind to wl_seat global";
      return;
    }
    if (connection->seat_manager_)
      connection->seat_manager_->AddSeat(connection, name, seat.release());
#else   // defined(USE_NEVA_APPRUNTIME)
  } else if (!connection->seat_ && strcmp(interface, "wl_seat") == 0 &&
             wl::CanBind(interface, version, kMinSeatVersion,
                         kMaxSeatVersion)) {
    connection->seat_ =
        wl::Bind<wl_seat>(registry, name, std::min(version, kMaxSeatVersion));
    if (!connection->seat_) {
      LOG(ERROR) << "Failed to bind to wl_seat global";
      return;
    }
    wl_seat_add_listener(connection->seat_.get(), &seat_listener, connection);
#endif  // !defined(USE_NEVA_APPRUNTIME)
    connection->CreateDataObjectsIfReady();
  } else if (!connection->shell_v6_ &&
             strcmp(interface, "zxdg_shell_v6") == 0 &&
             wl::CanBind(interface, version, kMaxXdgShellVersion,
                         kMaxXdgShellVersion)) {
    // Check for zxdg_shell_v6 first.
    connection->shell_v6_ = wl::Bind<zxdg_shell_v6>(
        registry, name, std::min(version, kMaxXdgShellVersion));
    if (!connection->shell_v6_) {
      LOG(ERROR) << "Failed to bind to zxdg_shell_v6 global";
      return;
    }
    zxdg_shell_v6_add_listener(connection->shell_v6_.get(), &shell_v6_listener,
                               connection);
  } else if (!connection->shell_ && strcmp(interface, "xdg_wm_base") == 0 &&
             wl::CanBind(interface, version, kMaxXdgShellVersion,
                         kMaxXdgShellVersion)) {
    connection->shell_ = wl::Bind<xdg_wm_base>(
        registry, name, std::min(version, kMaxXdgShellVersion));
    if (!connection->shell_) {
      LOG(ERROR) << "Failed to bind to xdg_wm_base global";
      return;
    }
    xdg_wm_base_add_listener(connection->shell_.get(), &shell_listener,
                             connection);
  } else if (base::EqualsCaseInsensitiveASCII(interface, "wl_output") &&
             wl::CanBind(interface, version, kMinWlOutputVersion,
                         kMinWlOutputVersion)) {
    wl::Object<wl_output> output = wl::Bind<wl_output>(
        registry, name, std::min(version, kMinWlOutputVersion));
    if (!output) {
      LOG(ERROR) << "Failed to bind to wl_output global";
      return;
    }

    if (!connection->wayland_output_manager_) {
      connection->wayland_output_manager_ =
          std::make_unique<WaylandOutputManager>(connection);
    }
    connection->wayland_output_manager_->AddWaylandOutput(name,
                                                          output.release());
  } else if (!connection->data_device_manager_ &&
             strcmp(interface, "wl_data_device_manager") == 0 &&
             wl::CanBind(interface, version, kMinDeviceManagerVersion,
                         kMaxDeviceManagerVersion)) {
    wl::Object<wl_data_device_manager> data_device_manager =
        wl::Bind<wl_data_device_manager>(
            registry, name, std::min(version, kMaxDeviceManagerVersion));
    if (!data_device_manager) {
      LOG(ERROR) << "Failed to bind to wl_data_device_manager global";
      return;
    }
    connection->data_device_manager_ =
        std::make_unique<WaylandDataDeviceManager>(
            data_device_manager.release(), connection);
    connection->CreateDataObjectsIfReady();
  } else if (!connection->gtk_primary_selection_device_manager_ &&
             strcmp(interface, "gtk_primary_selection_device_manager") == 0 &&
             wl::CanBind(interface, version,
                         kMaxGtkPrimarySelectionDeviceManagerVersion,
                         kMaxGtkPrimarySelectionDeviceManagerVersion)) {
    wl::Object<::gtk_primary_selection_device_manager> manager =
        wl::Bind<::gtk_primary_selection_device_manager>(
            registry, name,
            std::min(version, kMaxGtkPrimarySelectionDeviceManagerVersion));
    connection->gtk_primary_selection_device_manager_ =
        std::make_unique<GtkPrimarySelectionDeviceManager>(manager.release(),
                                                           connection);
  } else if (!connection->zwp_primary_selection_device_manager_ &&
             strcmp(interface, "zwp_primary_selection_device_manager_v1") ==
                 0 &&
             wl::CanBind(interface, version,
                         kMaxGtkPrimarySelectionDeviceManagerVersion,
                         kMaxGtkPrimarySelectionDeviceManagerVersion)) {
    wl::Object<zwp_primary_selection_device_manager_v1> manager =
        wl::Bind<zwp_primary_selection_device_manager_v1>(
            registry, name,
            std::min(version, kMaxGtkPrimarySelectionDeviceManagerVersion));
    connection->zwp_primary_selection_device_manager_ =
        std::make_unique<ZwpPrimarySelectionDeviceManager>(manager.release(),
                                                           connection);
  } else if (!connection->linux_explicit_synchronization_ &&
             (strcmp(interface, "zwp_linux_explicit_synchronization_v1") ==
              0) &&
             wl::CanBind(interface, version, kMinExplicitSyncVersion,
                         kMaxExplicitSyncVersion)) {
    connection->linux_explicit_synchronization_ =
        wl::Bind<zwp_linux_explicit_synchronization_v1>(
            registry, name, std::min(version, kMaxExplicitSyncVersion));
  } else if (!connection->zwp_dmabuf_ &&
             (strcmp(interface, "zwp_linux_dmabuf_v1") == 0) &&
             wl::CanBind(interface, version, kMinLinuxDmabufVersion,
                         kMaxLinuxDmabufVersion)) {
    wl::Object<zwp_linux_dmabuf_v1> zwp_linux_dmabuf =
        wl::Bind<zwp_linux_dmabuf_v1>(
            registry, name, std::min(version, kMaxLinuxDmabufVersion));
    connection->zwp_dmabuf_ = std::make_unique<WaylandZwpLinuxDmabuf>(
        zwp_linux_dmabuf.release(), connection);
  } else if (!connection->presentation_ &&
             (strcmp(interface, "wp_presentation") == 0) &&
             wl::CanBind(interface, version, kMaxWpPresentationVersion,
                         kMaxWpPresentationVersion)) {
    connection->presentation_ = wl::Bind<wp_presentation>(
        registry, name, std::min(version, kMaxWpPresentationVersion));
  } else if (!connection->viewporter_ &&
             (strcmp(interface, "wp_viewporter") == 0) &&
             wl::CanBind(interface, version, kMaxWpViewporterVersion,
                         kMaxWpViewporterVersion)) {
    connection->viewporter_ = wl::Bind<wp_viewporter>(
        registry, name, std::min(version, kMaxWpViewporterVersion));
  } else if (!connection->zcr_cursor_shapes_ &&
             strcmp(interface, "zcr_cursor_shapes_v1") == 0 &&
             wl::CanBind(interface, version, kMaxCursorShapesVersion,
                         kMaxCursorShapesVersion)) {
    auto zcr_cursor_shapes = wl::Bind<zcr_cursor_shapes_v1>(
        registry, name, std::min(version, kMaxCursorShapesVersion));
    if (!zcr_cursor_shapes) {
      LOG(ERROR) << "Failed to bind zcr_cursor_shapes_v1";
      return;
    }
    connection->zcr_cursor_shapes_ = std::make_unique<WaylandZcrCursorShapes>(
        zcr_cursor_shapes.release(), connection);
  } else if (!connection->keyboard_extension_v1_ &&
             strcmp(interface, "zcr_keyboard_extension_v1") == 0 &&
             wl::CanBind(interface, version, kMinKeyboardExtensionVersion,
                         kMaxKeyboardExtensionVersion)) {
    connection->keyboard_extension_v1_ = wl::Bind<zcr_keyboard_extension_v1>(
        registry, name, std::min(version, kMaxKeyboardExtensionVersion));
    if (!connection->keyboard_extension_v1_) {
      LOG(ERROR) << "Failed to bind zcr_keyboard_extension_v1";
      return;
    }
    // CreateKeyboard may fail if we do not have keyboard seat capabilities yet.
    // We will create the keyboard when get them in that case.
#if defined(USE_NEVA_APPRUNTIME)
    if (connection->seat_manager_)
      connection->seat_manager_->CreateKeyboard();
#else   // defined(USE_NEVA_APPRUNTIME)
    connection->CreateKeyboard();
#endif  // !defined(USE_NEVA_APPRUNTIME)
  } else if (!connection->text_input_manager_v1_ &&
             strcmp(interface, "zwp_text_input_manager_v1") == 0 &&
             wl::CanBind(interface, version, kMaxTextInputManagerVersion,
                         kMaxTextInputManagerVersion)) {
    connection->text_input_manager_v1_ = wl::Bind<zwp_text_input_manager_v1>(
        registry, name, std::min(version, kMaxTextInputManagerVersion));
    if (!connection->text_input_manager_v1_) {
      LOG(ERROR) << "Failed to bind to zwp_text_input_manager_v1 global";
      return;
    }
  } else if (!connection->xdg_foreign_ &&
             strcmp(interface, "zxdg_exporter_v1") == 0 &&
             wl::CanBind(interface, version, kMaxXdgExporterVersion,
                         kMaxXdgExporterVersion)) {
    connection->xdg_foreign_ = std::make_unique<XdgForeignWrapper>(
        connection,
        wl::Bind<zxdg_exporter_v1>(registry, name,
                                   std::min(version, kMaxXdgExporterVersion)));
#if !defined(OS_WEBOS)
  } else if (!connection->drm_ && (strcmp(interface, "wl_drm") == 0) &&
             wl::CanBind(interface, version, kMinWlDrmVersion,
                         kMaxWlDrmVersion)) {
    auto wayland_drm = wl::Bind<struct wl_drm>(
        registry, name, std::min(version, kMaxWlDrmVersion));
    connection->drm_ =
        std::make_unique<WaylandDrm>(wayland_drm.release(), connection);
#endif  // !defined(OS_WEBOS)
  } else if (!connection->zaura_shell_ &&
             (strcmp(interface, "zaura_shell") == 0) &&
             wl::CanBind(interface, version, kMinAuraShellVersion,
                         kMaxAuraShellVersion)) {
    auto zaura_shell = wl::Bind<struct zaura_shell>(
        registry, name, std::min(version, kMaxAuraShellVersion));
    if (!zaura_shell) {
      LOG(ERROR) << "Failed to bind zaura_shell";
      return;
    }
    connection->zaura_shell_ =
        std::make_unique<WaylandZAuraShell>(zaura_shell.release(), connection);
  } else if (!connection->xdg_decoration_manager_ &&
             strcmp(interface, "zxdg_decoration_manager_v1") == 0 &&
             wl::CanBind(interface, version, kMaxXdgDecorationVersion,
                         kMaxXdgDecorationVersion)) {
    connection->xdg_decoration_manager_ =
        wl::Bind<struct zxdg_decoration_manager_v1>(
            registry, name, std::min(version, kMaxXdgDecorationVersion));
  } else if (!connection->extended_drag_v1_ &&
             strcmp(interface, "zcr_extended_drag_v1") == 0 &&
             wl::CanBind(interface, version, kMaxExtendedDragVersion,
                         kMaxExtendedDragVersion)) {
    connection->extended_drag_v1_ = wl::Bind<zcr_extended_drag_v1>(
        registry, name, std::min(version, kMaxExtendedDragVersion));
    if (!connection->extended_drag_v1_) {
      LOG(ERROR) << "Failed to bind to zcr_extended_drag_v1 global";
      return;
    }
  }

  connection->ScheduleFlush();
}

// static
void WaylandConnection::GlobalRemove(void* data,
                                     wl_registry* registry,
                                     uint32_t name) {
  WaylandConnection* connection = static_cast<WaylandConnection*>(data);
  // The Wayland protocol distinguishes global objects by unique numeric names,
  // which the WaylandOutputManager uses as unique output ids. But, it is only
  // possible to figure out, what global object is going to be removed on the
  // WaylandConnection::GlobalRemove call. Thus, whatever unique |name| comes,
  // it's forwarded to the WaylandOutputManager, which checks if such a global
  // output object exists and removes it.
  if (connection->wayland_output_manager_)
    connection->wayland_output_manager_->RemoveWaylandOutput(name);
}

#if !defined(USE_NEVA_APPRUNTIME)
// static
void WaylandConnection::Capabilities(void* data,
                                     wl_seat* seat,
                                     uint32_t capabilities) {
  WaylandConnection* self = static_cast<WaylandConnection*>(data);
  DCHECK(self);
  self->UpdateInputDevices(seat, capabilities);
  self->ScheduleFlush();
}

// static
void WaylandConnection::Name(void* data, wl_seat* seat, const char* name) {}
#endif  // !defined(USE_NEVA_APPRUNTIME)

// static
void WaylandConnection::PingV6(void* data,
                               zxdg_shell_v6* shell_v6,
                               uint32_t serial) {
  WaylandConnection* connection = static_cast<WaylandConnection*>(data);
  zxdg_shell_v6_pong(shell_v6, serial);
  connection->ScheduleFlush();
}

// static
void WaylandConnection::Ping(void* data, xdg_wm_base* shell, uint32_t serial) {
  WaylandConnection* connection = static_cast<WaylandConnection*>(data);
  xdg_wm_base_pong(shell, serial);
  connection->ScheduleFlush();
}

}  // namespace ui
