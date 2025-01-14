// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_X11_X11_UTILS_H_
#define UI_OZONE_PLATFORM_X11_X11_UTILS_H_

#include "ui/ozone/public/platform_utils.h"

namespace ui {

class X11Utils : public PlatformUtils {
 public:
  X11Utils();
  X11Utils(const X11Utils&) = delete;
  X11Utils& operator=(const X11Utils&) = delete;
  ~X11Utils() override;

  gfx::ImageSkia GetNativeWindowIcon(intptr_t target_window_id) override;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_X11_X11_UTILS_H_
