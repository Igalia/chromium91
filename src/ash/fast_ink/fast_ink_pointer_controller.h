// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_FAST_INK_FAST_INK_POINTER_CONTROLLER_H_
#define ASH_FAST_INK_FAST_INK_POINTER_CONTROLLER_H_

#include <set>

#include "base/macros.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "ui/aura/window_tracker.h"
#include "ui/events/devices/device_data_manager.h"
#include "ui/events/devices/input_device_event_observer.h"
#include "ui/events/event_handler.h"

class PrefChangeRegistrar;

namespace aura {
class Window;
}  // namespace aura

namespace ui {
class LocatedEvent;
}  // namespace ui

namespace views {
class View;
}  // namespace views

namespace fast_ink {

// Base class for a fast ink based pointer controller. Enables/disables
// the pointer, receives points and passes them off to be rendered.
class FastInkPointerController : public ui::EventHandler,
                                 public ui::InputDeviceEventObserver {
 public:
  FastInkPointerController();
  ~FastInkPointerController() override;

  bool is_enabled() const { return enabled_; }

  // Enables/disables the pointer. The user still has to press to see
  // the pointer.
  virtual void SetEnabled(bool enabled);

  // Add window that should be excluded from handling events.
  void AddExcludedWindow(aura::Window* window);

 protected:
  // Whether the controller is ready to start handling a new gesture.
  virtual bool CanStartNewGesture(ui::LocatedEvent* event);
  // Whether the event should be processed and stop propagation.
  virtual bool ShouldProcessEvent(ui::LocatedEvent* event);

  bool IsEnabledForMouseEvent() const;

  // Return true if the location of the event is in one of the excluded windows.
  bool IsPointerInExcludedWindows(ui::LocatedEvent* event);

 private:
  // Creates new pointer view if `can_start_new_gesture` is true. Otherwise, try
  // to re-use existing one. Ends the current pointer session if the pointer
  // widget is no longer valid. Returns true if there is a pointer view
  // available.
  bool MaybeCreatePointerView(ui::LocatedEvent* event,
                              bool can_start_new_gesture);

  // ui::EventHandler:
  void OnTouchEvent(ui::TouchEvent* event) override;
  void OnMouseEvent(ui::MouseEvent* event) override;

  // ui::InputDeviceEventObserver:
  void OnDeviceListsComplete() override;

  void OnHasSeenStylusPrefChanged();
  void UpdateEnabledForMouseEvent();

  // Returns the pointer view.
  virtual views::View* GetPointerView() const = 0;

  // Creates the pointer view.
  virtual void CreatePointerView(base::TimeDelta presentation_delay,
                                 aura::Window* root_window) = 0;

  // Updates the pointer view.
  virtual void UpdatePointerView(ui::TouchEvent* event) = 0;
  virtual void UpdatePointerView(ui::MouseEvent* event) {}

  // Destroys the pointer view if it exists.
  virtual void DestroyPointerView() = 0;

  // The presentation delay used for pointer location prediction.
  const base::TimeDelta presentation_delay_;

  bool enabled_ = false;
  bool has_stylus_ = false;
  bool has_seen_stylus_ = false;

  // Set of touch ids.
  std::set<int> touch_ids_;

  // If the pointer event is in the bound of any of the |excluded_windows_|.
  // Skip processing the event.
  aura::WindowTracker excluded_windows_;

  std::unique_ptr<PrefChangeRegistrar> pref_change_registrar_local_;

  base::ScopedObservation<ui::DeviceDataManager, ui::InputDeviceEventObserver>
      input_device_event_observation_{this};

  DISALLOW_COPY_AND_ASSIGN(FastInkPointerController);
};

}  // namespace fast_ink

#endif  // ASH_FAST_INK_FAST_INK_POINTER_CONTROLLER_H_
