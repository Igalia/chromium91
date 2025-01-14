// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/mirroring/service/video_capture_client.h"

#include "base/bind.h"
#include "base/no_destructor.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/video_frame.h"
#include "media/capture/mojom/video_capture_types.mojom.h"

namespace mirroring {

namespace {

// Required by mojom::VideoCaptureHost interface. Can be any nonzero value.
const base::UnguessableToken& DeviceId() {
  static const base::NoDestructor<base::UnguessableToken> device_id(
      base::UnguessableToken::Deserialize(1, 1));
  return *device_id;
}

// Required by mojom::VideoCaptureHost interface. Can be any nonzero value.
const base::UnguessableToken& SessionId() {
  static const base::NoDestructor<base::UnguessableToken> session_id(
      base::UnguessableToken::Deserialize(1, 1));
  return *session_id;
}

}  // namespace

VideoCaptureClient::VideoCaptureClient(
    const media::VideoCaptureParams& params,
    mojo::PendingRemote<media::mojom::VideoCaptureHost> host)
    : params_(params), video_capture_host_(std::move(host)) {
  DCHECK(video_capture_host_);
}

VideoCaptureClient::~VideoCaptureClient() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Stop();
}

void VideoCaptureClient::Start(FrameDeliverCallback deliver_callback,
                               base::OnceClosure error_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << __func__;
  DCHECK(!deliver_callback.is_null());
  frame_deliver_callback_ = std::move(deliver_callback);
  error_callback_ = std::move(error_callback);

  video_capture_host_->Start(DeviceId(), SessionId(), params_,
                             receiver_.BindNewPipeAndPassRemote());
}

void VideoCaptureClient::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(1) << __func__;
  video_capture_host_->Stop(DeviceId());
}

void VideoCaptureClient::Pause() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(2) << __func__;
  if (frame_deliver_callback_.is_null())
    return;
  frame_deliver_callback_.Reset();
  video_capture_host_->Pause(DeviceId());
}

void VideoCaptureClient::Resume(FrameDeliverCallback deliver_callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(2) << __func__;
  DCHECK(!deliver_callback.is_null());
  if (!frame_deliver_callback_.is_null()) {
    return;
  }
  frame_deliver_callback_ = std::move(deliver_callback);
  video_capture_host_->Resume(DeviceId(), SessionId(), params_);
}

void VideoCaptureClient::RequestRefreshFrame() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (frame_deliver_callback_.is_null())
    return;
  video_capture_host_->RequestRefreshFrame(DeviceId());
}

void VideoCaptureClient::OnStateChanged(media::mojom::VideoCaptureState state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(2) << __func__ << " state: " << state;

  switch (state) {
    case media::mojom::VideoCaptureState::STARTED:
      RequestRefreshFrame();
      break;
    case media::mojom::VideoCaptureState::FAILED:
      if (!error_callback_.is_null())
        std::move(error_callback_).Run();
      break;
    case media::mojom::VideoCaptureState::PAUSED:
    case media::mojom::VideoCaptureState::RESUMED:
      break;
    case media::mojom::VideoCaptureState::STOPPED:
    case media::mojom::VideoCaptureState::ENDED:
      client_buffers_.clear();
      mapped_buffers_.clear();
      weak_factory_.InvalidateWeakPtrs();
      error_callback_.Reset();
      frame_deliver_callback_.Reset();
      receiver_.reset();
      break;
  }
}

void VideoCaptureClient::OnNewBuffer(
    int32_t buffer_id,
    media::mojom::VideoBufferHandlePtr buffer_handle) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(3) << __func__ << ": buffer_id=" << buffer_id;

  if (!buffer_handle->is_read_only_shmem_region() &&
      !buffer_handle->is_shared_buffer_handle()) {
#if defined(OS_MAC)
    if (!buffer_handle->is_gpu_memory_buffer_handle()) {
      NOTIMPLEMENTED();
      return;
    }
#else
    NOTIMPLEMENTED();
    return;
#endif
  }
  const auto insert_result = client_buffers_.emplace(
      std::make_pair(buffer_id, std::move(buffer_handle)));
  DCHECK(insert_result.second);
}

void VideoCaptureClient::OnBufferReady(
    media::mojom::ReadyBufferPtr buffer,
    std::vector<media::mojom::ReadyBufferPtr> scaled_buffers) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(3) << __func__ << ": buffer_id=" << buffer->buffer_id;

  // Scaled buffers are currently ignored by VideoCaptureClient.
  for (media::mojom::ReadyBufferPtr& scaled_buffer : scaled_buffers) {
    video_capture_host_->ReleaseBuffer(DeviceId(), scaled_buffer->buffer_id,
                                       media::VideoCaptureFeedback());
  }
  scaled_buffers.clear();

  bool consume_buffer = !frame_deliver_callback_.is_null();
  if (buffer->info->pixel_format != media::PIXEL_FORMAT_NV12 &&
      buffer->info->pixel_format != media::PIXEL_FORMAT_I420 &&
      buffer->info->pixel_format != media::PIXEL_FORMAT_Y16) {
    consume_buffer = false;
    LOG(DFATAL) << "Wrong pixel format, got pixel format:"
                << VideoPixelFormatToString(buffer->info->pixel_format);
  }
  if (!consume_buffer) {
    video_capture_host_->ReleaseBuffer(DeviceId(), buffer->buffer_id,
                                       media::VideoCaptureFeedback());
    return;
  }

  base::TimeTicks reference_time = *buffer->info->metadata.reference_time;

  if (first_frame_ref_time_.is_null())
    first_frame_ref_time_ = reference_time;

  // If the timestamp is not prepared, we use reference time to make a rough
  // estimate. e.g. ThreadSafeCaptureOracle::DidCaptureFrame().
  // TODO(crbug.com/618407): Fix upstream capturers to always set timestamp and
  // reference time.
  if (buffer->info->timestamp.is_zero())
    buffer->info->timestamp = reference_time - first_frame_ref_time_;

  // Used by chrome/browser/media/cast_mirroring_performance_browsertest.cc
  TRACE_EVENT_INSTANT2("cast_perf_test", "OnBufferReceived",
                       TRACE_EVENT_SCOPE_THREAD, "timestamp",
                       (reference_time - base::TimeTicks()).InMicroseconds(),
                       "time_delta", buffer->info->timestamp.InMicroseconds());

  const auto& buffer_iter = client_buffers_.find(buffer->buffer_id);
  if (buffer_iter == client_buffers_.end()) {
    LOG(DFATAL) << "Ignoring OnBufferReady() for unknown buffer.";
    return;
  }
  scoped_refptr<media::VideoFrame> frame;
  BufferFinishedCallback buffer_finished_callback;
  if (buffer_iter->second->is_gpu_memory_buffer_handle()) {
#if defined(OS_MAC)
    frame = media::VideoFrame::WrapUnacceleratedIOSurface(
        buffer_iter->second->get_gpu_memory_buffer_handle().Clone(),
        buffer->info->visible_rect, buffer->info->timestamp);
    buffer_finished_callback = media::BindToCurrentLoop(base::BindOnce(
        &VideoCaptureClient::OnClientBufferFinished, weak_factory_.GetWeakPtr(),
        buffer->buffer_id, base::ReadOnlySharedMemoryMapping()));
#else
    NOTREACHED();
#endif
  } else if (buffer_iter->second->is_shared_buffer_handle()) {
    // TODO(crbug.com/843117): Remove this case after migrating
    // media::VideoCaptureDeviceClient to the new shared memory API.
    auto mapping_iter = mapped_buffers_.find(buffer->buffer_id);
    const size_t buffer_size = media::VideoFrame::AllocationSize(
        buffer->info->pixel_format, buffer->info->coded_size);
    if (mapping_iter != mapped_buffers_.end() &&
        buffer_size > mapping_iter->second.second) {
      // Unmaps shared memory for too-small region.
      mapped_buffers_.erase(mapping_iter);
      mapping_iter = mapped_buffers_.end();
    }
    if (mapping_iter == mapped_buffers_.end()) {
      mojo::ScopedSharedBufferMapping mapping =
          buffer_iter->second->get_shared_buffer_handle()->Map(buffer_size);
      if (!mapping) {
        video_capture_host_->ReleaseBuffer(DeviceId(), buffer->buffer_id,
                                           media::VideoCaptureFeedback());
        return;
      }
      mapping_iter = mapped_buffers_
                         .emplace(std::make_pair(
                             buffer->buffer_id,
                             MappingAndSize(std::move(mapping), buffer_size)))
                         .first;
    }
    const auto& buffer_it = mapping_iter->second;
    frame = media::VideoFrame::WrapExternalData(
        buffer->info->pixel_format, buffer->info->coded_size,
        buffer->info->visible_rect, buffer->info->visible_rect.size(),
        reinterpret_cast<uint8_t*>(buffer_it.first.get()), buffer_it.second,
        buffer->info->timestamp);
    buffer_finished_callback = media::BindToCurrentLoop(base::BindOnce(
        &VideoCaptureClient::OnClientBufferFinished, weak_factory_.GetWeakPtr(),
        buffer->buffer_id, base::ReadOnlySharedMemoryMapping()));
  } else {
    base::ReadOnlySharedMemoryMapping mapping =
        buffer_iter->second->get_read_only_shmem_region().Map();
    const size_t frame_allocation_size = media::VideoFrame::AllocationSize(
        buffer->info->pixel_format, buffer->info->coded_size);
    if (mapping.IsValid() && mapping.size() >= frame_allocation_size) {
      frame = media::VideoFrame::WrapExternalData(
          buffer->info->pixel_format, buffer->info->coded_size,
          buffer->info->visible_rect, buffer->info->visible_rect.size(),
          const_cast<uint8_t*>(static_cast<const uint8_t*>(mapping.memory())),
          frame_allocation_size, buffer->info->timestamp);
    }
    buffer_finished_callback = media::BindToCurrentLoop(base::BindOnce(
        &VideoCaptureClient::OnClientBufferFinished, weak_factory_.GetWeakPtr(),
        buffer->buffer_id, std::move(mapping)));
  }

  if (!frame) {
    LOG(DFATAL) << "Unable to wrap shared memory mapping.";
    video_capture_host_->ReleaseBuffer(DeviceId(), buffer->buffer_id,
                                       media::VideoCaptureFeedback());
    OnStateChanged(media::mojom::VideoCaptureState::FAILED);
    return;
  }
  frame->AddDestructionObserver(
      base::BindOnce(&VideoCaptureClient::DidFinishConsumingFrame,
                     std::move(buffer_finished_callback)));

  frame->set_metadata(buffer->info->metadata);
  if (buffer->info->color_space.has_value())
    frame->set_color_space(buffer->info->color_space.value());

  frame_deliver_callback_.Run(frame);
}

void VideoCaptureClient::OnBufferDestroyed(int32_t buffer_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(3) << __func__ << ": buffer_id=" << buffer_id;

  const auto& buffer_iter = client_buffers_.find(buffer_id);
  if (buffer_iter != client_buffers_.end())
    client_buffers_.erase(buffer_iter);
  const auto& mapping_iter = mapped_buffers_.find(buffer_id);
  if (mapping_iter != mapped_buffers_.end())
    mapped_buffers_.erase(mapping_iter);
}

void VideoCaptureClient::OnClientBufferFinished(
    int buffer_id,
    base::ReadOnlySharedMemoryMapping mapping) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DVLOG(3) << __func__ << ": buffer_id=" << buffer_id;

  // Buffer was already destroyed.
  if (client_buffers_.find(buffer_id) == client_buffers_.end()) {
    DCHECK(mapped_buffers_.find(buffer_id) == mapped_buffers_.end());
    return;
  }

  video_capture_host_->ReleaseBuffer(DeviceId(), buffer_id, feedback_);
  feedback_ = media::VideoCaptureFeedback();
}

// static
void VideoCaptureClient::DidFinishConsumingFrame(
    BufferFinishedCallback callback) {
  // Note: This function may be called on any thread by the VideoFrame
  // destructor.
  DCHECK(!callback.is_null());
  std::move(callback).Run();
}

void VideoCaptureClient::ProcessFeedback(
    const media::VideoCaptureFeedback& feedback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  feedback_ = feedback;
}

}  // namespace mirroring
