// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gtk/input_method_context_impl_gtk.h"

#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <stddef.h>

#include "base/strings/utf_string_conversions.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/linux/composition_text_util_pango.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/geometry/dip_util.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gtk/gtk_compat.h"
#include "ui/gtk/gtk_ui.h"
#include "ui/gtk/gtk_ui_delegate.h"
#include "ui/gtk/gtk_util.h"
#include "ui/views/linux_ui/linux_ui.h"

namespace gtk {

namespace {

GdkEventKey* GdkEventToKey(GdkEvent* event) {
  DCHECK(!GtkCheckVersion(4));
  auto* key = reinterpret_cast<GdkEventKey*>(event);
  DCHECK(key->type == GDK_KEY_PRESS || key->type == GDK_KEY_RELEASE);
  return key;
}

// Get IME KeyEvent's target window. Assumes root aura::Window is set to
// Event::target(), otherwise returns null.
GdkWindow* GetTargetWindow(const ui::KeyEvent& key_event) {
  if (!key_event.target())
    return nullptr;

  aura::Window* window = static_cast<aura::Window*>(key_event.target());
  DCHECK(window) << "KeyEvent target window not set.";

  auto window_id = window->GetHost()->GetAcceleratedWidget();
  return GtkUi::GetDelegate()->GetGdkWindow(window_id);
}

// Translate IME ui::KeyEvent to a GdkEventKey.
GdkEvent* GdkEventFromImeKeyEvent(const ui::KeyEvent& key_event) {
  DCHECK(!GtkCheckVersion(4));
  GdkEvent* event = GdkEventFromKeyEvent(key_event);
  if (!event)
    return nullptr;

  GdkWindow* target_window = GetTargetWindow(key_event);
  if (!target_window) {
    gdk_event_free(event);
    return nullptr;
  }
  GdkEventToKey(event)->window = target_window;
  return event;
}

}  // namespace

InputMethodContextImplGtk::InputMethodContextImplGtk(
    ui::LinuxInputMethodContextDelegate* delegate,
    bool is_simple)
    : delegate_(delegate), is_simple_(is_simple) {
  CHECK(delegate_);

  gtk_context_ =
      is_simple ? gtk_im_context_simple_new() : gtk_im_multicontext_new();

  g_signal_connect(gtk_context_, "commit", G_CALLBACK(OnCommitThunk), this);
  g_signal_connect(gtk_context_, "preedit-changed",
                   G_CALLBACK(OnPreeditChangedThunk), this);
  g_signal_connect(gtk_context_, "preedit-end", G_CALLBACK(OnPreeditEndThunk),
                   this);
  g_signal_connect(gtk_context_, "preedit-start",
                   G_CALLBACK(OnPreeditStartThunk), this);
  // TODO(shuchen): Handle operations on surrounding text.
  // "delete-surrounding" and "retrieve-surrounding" signals should be
  // handled.

  if (GtkCheckVersion(4))
    gtk_im_context_set_client_widget(gtk_context_, GetDummyWindow());
}

InputMethodContextImplGtk::~InputMethodContextImplGtk() {
  if (gtk_context_) {
    g_object_unref(gtk_context_);
    gtk_context_ = nullptr;
  }
}

// Overridden from ui::LinuxInputMethodContext
bool InputMethodContextImplGtk::DispatchKeyEvent(
    const ui::KeyEvent& key_event) {
  if (!gtk_context_)
    return false;

  GdkEvent* event = nullptr;
  if (!GtkCheckVersion(4)) {
    event = GdkEventFromImeKeyEvent(key_event);
    if (!event) {
      LOG(ERROR) << "Cannot translate a Keyevent to a GdkEvent.";
      return false;
    }

    GdkWindow* target_window = GdkEventToKey(event)->window;
    if (!target_window) {
      LOG(ERROR) << "Cannot get target GdkWindow for KeyEvent.";
      return false;
    }

    SetContextClientWindow(target_window);
  }

  // Convert the last known caret bounds relative to the screen coordinates
  // to a GdkRectangle relative to the client window.
  aura::Window* window = static_cast<aura::Window*>(key_event.target());
  gint win_x = window->GetBoundsInScreen().x();
  gint win_y = window->GetBoundsInScreen().y();
  gint caret_x = last_caret_bounds_.x();
  gint caret_y = last_caret_bounds_.y();
  gint caret_w = last_caret_bounds_.width();
  gint caret_h = last_caret_bounds_.height();

  // Chrome's DIPs may be different from GTK's DIPs if
  // --force-device-scale-factor is used.
  float factor =
      GetDeviceScaleFactor() / gtk_widget_get_scale_factor(GetDummyWindow());
  GdkRectangle gdk_rect = {factor * (caret_x - win_x),
                           factor * (caret_y - win_y), factor * caret_w,
                           factor * caret_h};
  gtk_im_context_set_cursor_location(gtk_context_, &gdk_rect);

  if (!GtkCheckVersion(4)) {
    const bool handled =
        GtkImContextFilterKeypress(gtk_context_, GdkEventToKey(event));
    gdk_event_free(event);
    return handled;
  }
  // In GTK4, clients can no longer create or modify events.  This makes using
  // the gtk_im_context_filter_keypress() API impossible.  Fortunately, an
  // alternative API called gtk_im_context_filter_key() was added for clients
  // that would have needed to construct their own event.  The parameters to
  // the new API are just a deconstructed version of a KeyEvent.
  bool press = key_event.type() == ui::ET_KEY_PRESSED;
  auto* surface =
      gtk_native_get_surface(gtk_widget_get_native(GetDummyWindow()));
  auto* device = gdk_seat_get_keyboard(
      gdk_display_get_default_seat(gdk_display_get_default()));
  auto time = (key_event.time_stamp() - base::TimeTicks()).InMilliseconds();
  auto keycode = GetKeyEventProperty(key_event, ui::kPropertyKeyboardHwKeyCode);
  auto state = GetGdkKeyEventState(key_event);
  auto group = GetKeyEventProperty(key_event, ui::kPropertyKeyboardGroup);
  return gtk_im_context_filter_key(gtk_context_, press, surface, device, time,
                                   keycode, state, group);
}

void InputMethodContextImplGtk::Reset() {
  gtk_im_context_reset(gtk_context_);

  // Some input methods may not honour the reset call.
  // Focusing out/in the to make sure it gets reset correctly.
  if (!is_simple_ && has_focus_) {
    Blur();
    Focus();
  }
}

void InputMethodContextImplGtk::Focus() {
  gtk_im_context_focus_in(gtk_context_);
  has_focus_ = true;
}

void InputMethodContextImplGtk::Blur() {
  gtk_im_context_focus_out(gtk_context_);
  has_focus_ = false;
}

void InputMethodContextImplGtk::SetCursorLocation(const gfx::Rect& rect) {
  // Remember the caret bounds so that we can set the cursor location later.
  // gtk_im_context_set_cursor_location() takes the location relative to the
  // client window, which is unknown at this point.  So we'll call
  // gtk_im_context_set_cursor_location() later in DispatchKeyEvent() where
  // (and only where) we know the client window.
  last_caret_bounds_ = rect;
}

void InputMethodContextImplGtk::SetSurroundingText(
    const std::u16string& text,
    const gfx::Range& selection_range) {}

// private:

// GtkIMContext event handlers.

void InputMethodContextImplGtk::OnCommit(GtkIMContext* context, gchar* text) {
  if (context != gtk_context_)
    return;

  delegate_->OnCommit(base::UTF8ToUTF16(text));
}

void InputMethodContextImplGtk::OnPreeditChanged(GtkIMContext* context) {
  if (context != gtk_context_)
    return;

  gchar* str = nullptr;
  PangoAttrList* attrs = nullptr;
  gint cursor_pos = 0;
  gtk_im_context_get_preedit_string(context, &str, &attrs, &cursor_pos);
  ui::CompositionText composition_text;
  ui::ExtractCompositionTextFromGtkPreedit(str, attrs, cursor_pos,
                                           &composition_text);
  g_free(str);
  pango_attr_list_unref(attrs);

  delegate_->OnPreeditChanged(composition_text);
}

void InputMethodContextImplGtk::OnPreeditEnd(GtkIMContext* context) {
  if (context != gtk_context_)
    return;

  delegate_->OnPreeditEnd();
}

void InputMethodContextImplGtk::OnPreeditStart(GtkIMContext* context) {
  if (context != gtk_context_)
    return;

  delegate_->OnPreeditStart();
}

void InputMethodContextImplGtk::SetContextClientWindow(GdkWindow* window) {
  DCHECK(!GtkCheckVersion(4));
  if (window == gdk_last_set_client_window_)
    return;
  gtk_im_context_set_client_window(gtk_context_, window);

  // Prevent leaks when overriding last client window
  if (gdk_last_set_client_window_)
    g_object_unref(gdk_last_set_client_window_);
  gdk_last_set_client_window_ = window;
}

}  // namespace gtk
