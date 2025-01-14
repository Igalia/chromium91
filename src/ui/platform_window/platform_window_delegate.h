// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_PLATFORM_WINDOW_PLATFORM_WINDOW_DELEGATE_H_
#define UI_PLATFORM_WINDOW_PLATFORM_WINDOW_DELEGATE_H_

#include "base/component_export.h"
#include "base/optional.h"
#include "build/build_config.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_widget_types.h"

#if defined(OS_FUCHSIA)
#include "ui/gfx/geometry/insets.h"
#endif  // defined(OS_FUCHSIA)

///@name USE_NEVA_APPRUNTIME
///@{
#include "ui/platform_window/neva/platform_window_delegate.h"
///@}

namespace gfx {
class Rect;
class Size;
}  // namespace gfx

class SkPath;

namespace ui {

class Event;

///@name USE_NEVA_APPRUNTIME
///@{
class LinuxInputMethodContext;
///@}

enum class PlatformWindowState {
  kUnknown,
  kMaximized,
  kMinimized,
  kNormal,
  kFullScreen,
};

class COMPONENT_EXPORT(PLATFORM_WINDOW) PlatformWindowDelegate
    ///@name USE_NEVA_APPRUNTIME
    ///@{
    : public neva::PlatformWindowDelegate
    ///@}
{
 public:
  struct COMPONENT_EXPORT(PLATFORM_WINDOW) BoundsChange {
    BoundsChange();
    BoundsChange(const gfx::Rect& bounds);
    ~BoundsChange();

    // The dimensions of the window, in physical window coordinates.
    gfx::Rect bounds;

#if defined(OS_FUCHSIA)
    // The widths of border regions which are obscured by overlapping
    // platform UI elements like onscreen keyboards.
    //
    // As an example, the overlap from an onscreen keyboard covering
    // the bottom of the Window would be represented like this:
    //
    // +------------------------+                ---
    // |                        |                 |
    // |        content         |                 |
    // |                        |                 | window
    // +------------------------+  ---            |
    // |    onscreen keyboard   |   |  overlap    |
    // +------------------------+  ---           ---
    gfx::Insets system_ui_overlap;
#endif  // defined(OS_FUCHSIA)
  };

  PlatformWindowDelegate();
  virtual ~PlatformWindowDelegate();

  virtual void OnBoundsChanged(const BoundsChange& change) = 0;

  // Note that |damaged_region| is in the platform-window's coordinates, in
  // physical pixels.
  virtual void OnDamageRect(const gfx::Rect& damaged_region) = 0;

  virtual void DispatchEvent(Event* event) = 0;

  virtual void OnCloseRequest() = 0;
  virtual void OnClosed() = 0;

  virtual void OnWindowStateChanged(PlatformWindowState new_state) = 0;

  virtual void OnLostCapture() = 0;

  virtual void OnAcceleratedWidgetAvailable(gfx::AcceleratedWidget widget) = 0;

  // Notifies the delegate that the widget is about to be destroyed.
  virtual void OnWillDestroyAcceleratedWidget() = 0;

  // Notifies the delegate that the widget cannot be used anymore until
  // a new widget is made available through OnAcceleratedWidgetAvailable().
  // Must not be called when the PlatformWindow is being destroyed.
  virtual void OnAcceleratedWidgetDestroyed() = 0;

  virtual void OnActivationChanged(bool active) = 0;

  // Requests size constraints for the PlatformWindow.
  virtual base::Optional<gfx::Size> GetMinimumSizeForWindow();
  virtual base::Optional<gfx::Size> GetMaximumSizeForWindow();

  ///@name USE_NEVA_APPRUNTIME
  ///@{
  virtual LinuxInputMethodContext* GetInputMethodContext();
  ///@}

  // Returns a mask to be used to clip the window for the size of
  // |WindowTreeHost::GetBoundsInPixels|.
  // This is used to create the non-rectangular window shape.
  virtual SkPath GetWindowMaskForWindowShapeInPixels();

  // Called while dragging maximized window when SurfaceFrame associated with
  // this window is locked to normal state or unlocked from previously locked
  // state. This function is used by chromeos for syncing
  // `chromeos::kFrameRestoreLookKey` window property
  // with lacros-chrome.
  virtual void OnSurfaceFrameLockingChanged(bool lock);

  // Called when the location of mouse pointer entered the window.  This is
  // different from ui::ET_MOUSE_ENTERED which may not be generated when mouse
  // is captured either by implicitly or explicitly.
  virtual void OnMouseEnter() = 0;
};

}  // namespace ui

#endif  // UI_PLATFORM_WINDOW_PLATFORM_WINDOW_DELEGATE_H_
