// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/codec/webrtc_video_encoder.h"

namespace remoting {

WebrtcVideoEncoder::EncodedFrame::EncodedFrame() = default;
WebrtcVideoEncoder::EncodedFrame::~EncodedFrame() = default;
WebrtcVideoEncoder::EncodedFrame::EncodedFrame(
    const WebrtcVideoEncoder::EncodedFrame&) = default;
WebrtcVideoEncoder::EncodedFrame& WebrtcVideoEncoder::EncodedFrame::operator=(
    const WebrtcVideoEncoder::EncodedFrame&) = default;

}  // namespace remoting
