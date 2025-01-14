// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_UTILITY_OCCLUSION_TRACKER_PAUSER_H_
#define ASH_UTILITY_OCCLUSION_TRACKER_PAUSER_H_

#include <memory>

#include "ash/ash_export.h"
#include "base/containers/flat_map.h"
#include "base/scoped_multi_source_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/aura/window_occlusion_tracker.h"
#include "ui/compositor/compositor_observer.h"

namespace ash {

// A utility class to pause aura's WindowOcclusionTracker until animations are
// finished on all compositors.
class ASH_EXPORT OcclusionTrackerPauser : public ui::CompositorObserver {
 public:
  OcclusionTrackerPauser();
  OcclusionTrackerPauser(const OcclusionTrackerPauser&) = delete;
  OcclusionTrackerPauser& operator=(const OcclusionTrackerPauser&) = delete;
  ~OcclusionTrackerPauser() override;

  // Pause the occlusion tracker until all new animations added after this
  // are finished. If non zero 'extra_pause_duration' is specified, it'll wait
  // then unpause the tracker.
  void PauseUntilAnimationsEnd(
      const base::TimeDelta& extra_pause_duration = base::TimeDelta());

  // ui::CompositorObserver:
  void OnFirstAnimationStarted(ui::Compositor* compositor) override {}
  void OnLastAnimationEnded(ui::Compositor* compositor) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;

 private:
  void Pause(ui::Compositor* compositor,
             const base::TimeDelta& extra_pause_duration);
  void OnFinish(ui::Compositor* compositor);
  void Unpause();

  base::OneShotTimer timer_;
  base::TimeDelta extra_pause_duration_;
  base::ScopedMultiSourceObservation<ui::Compositor, ui::CompositorObserver>
      observations_{this};

  std::unique_ptr<aura::WindowOcclusionTracker::ScopedPause> scoped_pause_;
};

}  // namespace ash

#endif  // ASH_UTILITY_OCCLUSION_TRACKER_PAUSER_H_
