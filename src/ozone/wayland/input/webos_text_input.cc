// Copyright 2017-2019 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "ozone/wayland/input/webos_text_input.h"

#include <curses.h>
#include <linux/input.h>
#include "wayland-text-client-protocol.h"

#include <string>

#include "base/logging.h"
#include "ozone/platform/webos_constants.h"
#include "ozone/wayland/display.h"
#include "ozone/wayland/input/keyboard.h"
#include "ozone/wayland/input/text_input_utils.h"
#include "ozone/wayland/seat.h"
#include "ozone/wayland/shell/shell_surface.h"
#include "ozone/wayland/window.h"
#include "ui/base/ime/neva/input_method_common.h"
#include "ui/base/ime/text_input_flags.h"
#include "ui/events/keycodes/keyboard_code_conversion.h"
#include "ui/events/keycodes/xkb_keysym.h"
#include "ui/base/ime/text_input_flags.h"

namespace ozonewayland {

uint32_t ContentHintFromInputContentType(ui::InputContentType content_type,
                                         int input_flags) {
  uint32_t wl_hint = (TEXT_MODEL_CONTENT_HINT_AUTO_COMPLETION |
                      TEXT_MODEL_CONTENT_HINT_AUTO_CAPITALIZATION);
  if (content_type == ui::InputContentType::kPassword)
    wl_hint |= TEXT_MODEL_CONTENT_HINT_PASSWORD;

  // hint from flags
  // TODO TEXT_INPUT_FLAG_SPELLCHECK_ON remains.
  //      The wayland-text-client doesn't offer the spellcheck yet.
  if (input_flags & ui::TEXT_INPUT_FLAG_SENSITIVE_ON)
    wl_hint |= TEXT_MODEL_CONTENT_HINT_SENSITIVE_DATA;
  if (input_flags & ui::TEXT_INPUT_FLAG_AUTOCOMPLETE_ON)
    wl_hint |= TEXT_MODEL_CONTENT_HINT_AUTO_COMPLETION;
  if (input_flags & ui::TEXT_INPUT_FLAG_AUTOCORRECT_ON)
    wl_hint |= TEXT_MODEL_CONTENT_HINT_AUTO_CORRECTION;

  return wl_hint;
}

uint32_t ContentPurposeFromInputContentType(ui::InputContentType content_type) {
  switch (content_type) {
    case ui::InputContentType::kPassword:
      return TEXT_MODEL_CONTENT_PURPOSE_PASSWORD;
    case ui::InputContentType::kEmail:
      return TEXT_MODEL_CONTENT_PURPOSE_EMAIL;
    case ui::InputContentType::kNumber:
      return TEXT_MODEL_CONTENT_PURPOSE_NUMBER;
    case ui::InputContentType::kTelephone:
      return TEXT_MODEL_CONTENT_PURPOSE_PHONE;
    case ui::InputContentType::kUrl:
      return TEXT_MODEL_CONTENT_PURPOSE_URL;
    case ui::InputContentType::kDate:
      return TEXT_MODEL_CONTENT_PURPOSE_DATE;
    case ui::InputContentType::kDateTime:
    case ui::InputContentType::kDateTimeLocal:
      return TEXT_MODEL_CONTENT_PURPOSE_DATETIME;
    case ui::InputContentType::kTime:
      return TEXT_MODEL_CONTENT_PURPOSE_TIME;
    default:
      return TEXT_MODEL_CONTENT_PURPOSE_NORMAL;
  }
}

const struct text_model_listener text_model_listener_ = {
  WaylandTextInput::OnCommitString,
  WaylandTextInput::OnPreeditString,
  WaylandTextInput::OnDeleteSurroundingText,
  WaylandTextInput::OnCursorPosition,
  WaylandTextInput::OnPreeditStyling,
  WaylandTextInput::OnPreeditCursor,
  WaylandTextInput::OnModifiersMap,
  WaylandTextInput::OnKeysym,
  WaylandTextInput::OnEnter,
  WaylandTextInput::OnLeave,
  WaylandTextInput::OnInputPanelState,
  WaylandTextInput::OnTextModelInputPanelRect
};

static uint32_t serial = 0;

WaylandTextInput::WaylandTextInput(WaylandSeat* seat)
    : input_panel_rect_(0, 0, 0, 0),
      activated_(false),
      state_(InputPanelUnknownState),
      input_content_type_(ui::InputContentType::kNone),
      text_input_flags_(0),
      active_window_(NULL),
      last_active_window_(NULL),
      seat_(seat) {}

WaylandTextInput::~WaylandTextInput() {
  DeactivateTextModel();
}

void WaylandTextInput::ResetIme() {
  if (!text_model_) {
    CreateTextModel();
  } else {
    text_model_reset(text_model_, serial);
  }
}

void WaylandTextInput::ActivateTextModel(WaylandWindow* active_window) {
  if (text_model_ && active_window && !activated_) {
    VLOG(1) << __func__ << " handle=" << active_window->Handle();
    text_model_activate(text_model_, serial, seat_->GetWLSeat(),
                        active_window->ShellSurface()->GetWLSurface());
  }
}

void WaylandTextInput::DeactivateTextModel() {
  if (text_model_ && activated_) {
    SetHiddenState();
    text_model_reset(text_model_, serial);
    VLOG(1) << __func__;
    text_model_deactivate(text_model_, seat_->GetWLSeat());
    text_model_destroy(text_model_);
    text_model_ = nullptr;
    activated_ = false;
    state_ = InputPanelUnknownState;
  }
}

void WaylandTextInput::CreateTextModel() {
  DCHECK(!text_model_);
  text_model_factory* factory =
      WaylandDisplay::GetInstance()->GetTextModelFactory();
  if (factory) {
    text_model_ = text_model_factory_create_text_model(factory);
    if (text_model_)
      text_model_add_listener(text_model_, &text_model_listener_, this);
  }
}

void WaylandTextInput::ShowInputPanel(wl_seat* input_seat, unsigned handle) {
  if (!text_model_)
    CreateTextModel();

  if (!text_model_)
    return;

  WaylandWindow* active_window{nullptr};
  if (active_window_ && active_window_->Handle() == handle)
    active_window = active_window_;
  else if (last_active_window_ && last_active_window_->Handle() == handle)
    active_window = last_active_window_;

  if (active_window) {
    if (activated_) {
      if (state_ == InputPanelShown)
        WaylandDisplay::GetInstance()->InputPanelStateChanged(
            handle, webos::InputPanelState::INPUT_PANEL_SHOWN);
      else
        text_model_show_input_panel(text_model_);
    } else {
      ActivateTextModel(active_window);
      UpdateTextModel();
    }
  }
}

void WaylandTextInput::HideInputPanel(wl_seat* input_seat,
                                      ui::ImeHiddenType hidden_type) {
  if (!text_model_)
    return;

  if (hidden_type == ui::ImeHiddenType::kDeactivate) {
    DeactivateTextModel();
  } else {
    SetHiddenState();
    text_model_hide_input_panel(text_model_);
  }
}

void WaylandTextInput::SetActiveWindow(WaylandWindow* window) {
  active_window_ = window;
  if (active_window_)
    last_active_window_ = active_window_;
}

void WaylandTextInput::SetHiddenState() {
  input_panel_rect_.SetRect(0, 0, 0, 0);
  if (last_active_window_) {
    WaylandDisplay::GetInstance()->InputPanelRectChanged(
        last_active_window_->Handle(), 0, 0, 0, 0);
    WaylandDisplay::GetInstance()->InputPanelStateChanged(
        last_active_window_->Handle(),
        webos::InputPanelState::INPUT_PANEL_HIDDEN);
  }
}

void WaylandTextInput::SetTextInputInfo(
    const ui::TextInputInfo& text_input_info,
    unsigned handle) {
  if (active_window_ && active_window_->Handle() == handle) {
    input_content_type_ = text_input_info.type;
    text_input_flags_ = text_input_info.flags;
    input_panel_rect_ = text_input_info.input_panel_rectangle;
    if (text_model_ && activated_)
      UpdateTextModel();
  }
}

void WaylandTextInput::UpdateTextModel() {
  if (text_model_) {
    if (input_panel_rect_.IsEmpty())
      text_model_reset_input_panel_rect(text_model_);
    else
      text_model_set_input_panel_rect(
          text_model_, input_panel_rect_.x(), input_panel_rect_.y(),
          input_panel_rect_.width(), input_panel_rect_.height());

    text_model_set_content_type(
        text_model_,
        ContentHintFromInputContentType(input_content_type_, text_input_flags_),
        ContentPurposeFromInputContentType(input_content_type_));
  }
}

void WaylandTextInput::SetSurroundingText(const std::string& text,
                                          size_t cursor_position,
                                          size_t anchor_position) {
  if (text_model_)
    text_model_set_surrounding_text(text_model_, text.c_str(), cursor_position,
                                    anchor_position);
}

void WaylandTextInput::OnWindowAboutToDestroy(unsigned windowhandle) {
  if (active_window_ && active_window_->Handle() == windowhandle)
    active_window_ = NULL;

  if (last_active_window_ && last_active_window_->Handle() == windowhandle)
    last_active_window_ = NULL;
}

void WaylandTextInput::OnCommitString(void* data,
                                      struct text_model* text_input,
                                      uint32_t serial,
                                      const char* text) {
  WaylandDisplay* dispatcher = WaylandDisplay::GetInstance();
  WaylandTextInput* instance = static_cast<WaylandTextInput*>(data);
  if (instance->last_active_window_)
    dispatcher->Commit(instance->last_active_window_->Handle(), std::string(text));
}

void WaylandTextInput::OnPreeditString(void* data,
                                       struct text_model* text_input,
                                       uint32_t serial,
                                       const char* text,
                                       const char* commit) {
  WaylandDisplay* dispatcher = WaylandDisplay::GetInstance();
  WaylandTextInput* instance = static_cast<WaylandTextInput*>(data);
  if (instance->last_active_window_)
    dispatcher->PreeditChanged(instance->last_active_window_->Handle(),
        std::string(text), std::string(commit));
}

void WaylandTextInput::OnDeleteSurroundingText(void* data,
                                               struct text_model* text_input,
                                               uint32_t serial,
                                               int32_t index,
                                               uint32_t length) {
  WaylandDisplay* dispatcher = WaylandDisplay::GetInstance();
  WaylandTextInput* instance = static_cast<WaylandTextInput*>(data);
  if (instance->last_active_window_)
    dispatcher->DeleteRange(instance->last_active_window_->Handle(), index, length);
}

void WaylandTextInput::OnCursorPosition(void* data,
                                        struct text_model* text_input,
                                        uint32_t serial,
                                        int32_t index,
                                        int32_t anchor) {}

void WaylandTextInput::OnPreeditStyling(void* data,
                                        struct text_model* text_input,
                                        uint32_t serial,
                                        uint32_t index,
                                        uint32_t length,
                                        uint32_t style) {}

void WaylandTextInput::OnPreeditCursor(void* data,
                                       struct text_model* text_input,
                                       uint32_t serial,
                                       int32_t index) {}

void WaylandTextInput::OnModifiersMap(void* data,
                                      struct text_model* text_input,
                                      struct wl_array* map) {}

void WaylandTextInput::OnKeysym(void* data,
                                struct text_model* text_input,
                                uint32_t serial,
                                uint32_t time,
                                uint32_t key,
                                uint32_t state,
                                uint32_t modifiers) {
  uint32_t key_code = KeyNumberFromKeySymCode(key, modifiers);
  if (key_code == KEY_UNKNOWN)
    return;

  // Copied from WaylandKeyboard::OnKeyNotify().
  ui::EventType type = ui::ET_KEY_PRESSED;
  WaylandDisplay* dispatcher = WaylandDisplay::GetInstance();
  dispatcher->SetSerial(serial);
  if (state == WL_KEYBOARD_KEY_STATE_RELEASED)
    type = ui::ET_KEY_RELEASED;
  const uint32_t device_id =
      wl_proxy_get_id(reinterpret_cast<wl_proxy*>(text_input));

  IMEModifierFlags flag = FLAG_ALT;
  while (flag) {
    dispatcher->TextInputModifier(
        state, GetModifierKey((IMEModifierFlags)(flag & modifiers)));
    flag = (IMEModifierFlags)(flag >> 1);
  }

  WaylandTextInput* wl_text_input = static_cast<WaylandTextInput*>(data);
  dispatcher->KeyNotify(type, key_code, device_id);

  bool hide_ime = false;
  if (key_code == KEY_PREVIOUS || key_code == KEY_UP || key_code == KEY_DOWN)
    if (wl_text_input->state_ == InputPanelHidden)
      hide_ime = true;

  if (state == WL_KEYBOARD_KEY_STATE_RELEASED &&
      (key_code == KEY_ENTER || key_code == KEY_KPENTER) &&
      (wl_text_input->input_content_type_ != ui::InputContentType::kTextArea) &&
      (wl_text_input->state_ == InputPanelShown))
    hide_ime = true;

  if (key_code == KEY_TAB)
    hide_ime = true;

  if (hide_ime)
    dispatcher->PrimarySeat()->HideInputPanel(ui::ImeHiddenType::kHide);
}

void WaylandTextInput::OnEnter(void* data,
                               struct text_model* text_input,
                               struct wl_surface* surface) {
  WaylandTextInput* instance = static_cast<WaylandTextInput*>(data);
  instance->activated_ = true;
}

void WaylandTextInput::OnLeave(void* data,
                               struct text_model* text_input) {
  WaylandTextInput* instance = static_cast<WaylandTextInput*>(data);
  instance->DeactivateTextModel();
}

void WaylandTextInput::OnInputPanelState(void* data,
                                         struct text_model* text_input,
                                         uint32_t state) {
  WaylandTextInput* instance = static_cast<WaylandTextInput*>(data);
  WaylandDisplay* dispatcher = WaylandDisplay::GetInstance();
  instance->state_ = static_cast<InputPanelState>(state);

  switch (state) {
    case InputPanelShown:
      if (instance->last_active_window_)
        dispatcher->InputPanelStateChanged(
            instance->last_active_window_->Handle(),
            webos::InputPanelState::INPUT_PANEL_SHOWN);
      break;
    case InputPanelHidden:
      instance->SetHiddenState();
      break;
    default:
      break;
  }
}

void WaylandTextInput::OnTextModelInputPanelRect(void* data,
                                                 struct text_model* text_model,
                                                 int32_t x,
                                                 int32_t y,
                                                 uint32_t width,
                                                 uint32_t height) {
  WaylandTextInput* instance = static_cast<WaylandTextInput*>(data);
  WaylandDisplay* dispatcher = WaylandDisplay::GetInstance();

  gfx::Rect oldRect(instance->input_panel_rect_);
  instance->input_panel_rect_.SetRect(x, y, width, height);

  if (instance->last_active_window_ && instance->input_panel_rect_ != oldRect)
    dispatcher->InputPanelRectChanged(instance->last_active_window_->Handle(),
                                      x, y, width, height);
}

}  // namespace ozonewayland
