// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_VIDEO_CAPTURE_GPU_MEMORY_BUFFER_VIRTUAL_DEVICE_MOJO_ADAPTER_H_
#define SERVICES_VIDEO_CAPTURE_GPU_MEMORY_BUFFER_VIRTUAL_DEVICE_MOJO_ADAPTER_H_

#include <map>

#include "base/callback.h"
#include "base/sequence_checker.h"
#include "media/capture/video/video_capture_buffer_pool.h"
#include "media/capture/video_capture_types.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/video_capture/public/mojom/device.mojom.h"
#include "services/video_capture/public/mojom/producer.mojom.h"
#include "services/video_capture/public/mojom/video_frame_handler.mojom.h"
#include "services/video_capture/public/mojom/virtual_device.mojom.h"

namespace video_capture {

class GpuMemoryBufferVirtualDeviceMojoAdapter
    : public mojom::GpuMemoryBufferVirtualDevice,
      public mojom::Device {
 public:
  GpuMemoryBufferVirtualDeviceMojoAdapter();
  GpuMemoryBufferVirtualDeviceMojoAdapter(
      GpuMemoryBufferVirtualDeviceMojoAdapter&) = delete;
  GpuMemoryBufferVirtualDeviceMojoAdapter& operator=(
      GpuMemoryBufferVirtualDeviceMojoAdapter&) = delete;
  ~GpuMemoryBufferVirtualDeviceMojoAdapter() override;

  void SetReceiverDisconnectedCallback(base::OnceClosure callback);

  // mojom::GpuMemoryBufferVirtualDevice implementation.
  void OnNewGpuMemoryBufferHandle(
      int32_t buffer_id,
      gfx::GpuMemoryBufferHandle gmb_handle) override;
  void OnFrameReadyInBuffer(
      int32_t buffer_id,
      mojo::PendingRemote<mojom::ScopedAccessPermission> access_permission,
      media::mojom::VideoFrameInfoPtr frame_info) override;
  void OnBufferRetired(int buffer_id) override;

  // mojom::Device implementation.
  void Start(const media::VideoCaptureParams& requested_settings,
             mojo::PendingRemote<mojom::VideoFrameHandler> handler) override;
  void MaybeSuspend() override;
  void Resume() override;
  void GetPhotoState(GetPhotoStateCallback callback) override;
  void SetPhotoOptions(media::mojom::PhotoSettingsPtr settings,
                       SetPhotoOptionsCallback callback) override;
  void TakePhoto(TakePhotoCallback callback) override;
  void ProcessFeedback(const media::VideoCaptureFeedback& feedback) override;

  void Stop();

 private:
  void OnReceiverConnectionErrorOrClose();

  base::OnceClosure optional_receiver_disconnected_callback_;
  mojo::Remote<mojom::VideoFrameHandler> video_frame_handler_;
  std::map<int32_t, gfx::GpuMemoryBufferHandle> known_buffer_handles_;
  SEQUENCE_CHECKER(sequence_checker_);

  DISALLOW_COPY_AND_ASSIGN(GpuMemoryBufferVirtualDeviceMojoAdapter);
};

}  // namespace video_capture

#endif  // SERVICES_VIDEO_CAPTURE_GPU_MEMORY_BUFFER_VIRTUAL_DEVICE_MOJO_ADAPTER_H_
