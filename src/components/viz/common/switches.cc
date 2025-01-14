// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/common/switches.h"

#include "base/command_line.h"
#include "base/strings/string_number_conversions.h"
#include "build/chromeos_buildflags.h"
#include "components/viz/common/constants.h"

namespace switches {

// Screen width is useful for debugging. Shipping implementations should detect
// this.
const char kDeJellyScreenWidth[] = "de-jelly-screen-width";

// The default number of the BeginFrames to wait to activate a surface with
// dependencies.
const char kDeadlineToSynchronizeSurfaces[] =
    "deadline-to-synchronize-surfaces";

// Disables begin frame limiting in both cc scheduler and display scheduler.
// Also implies --disable-gpu-vsync (see //ui/gl/gl_switches.h).
const char kDisableFrameRateLimit[] = "disable-frame-rate-limit";

// Sets the number of max pending frames in the GL buffer queue to 1.
const char kDoubleBufferCompositing[] = "double-buffer-compositing";

const char kDraw1Point12Ms[] = "1-pt-12ms";
const char kDraw2Points6Ms[] = "2-pt-6ms";
const char kDraw1Point6Ms[] = "1-pt-6ms";
const char kDraw2Points3Ms[] = "2-pt-3ms";
const char kDrawPredictedInkPoint[] = "draw-predicted-ink-point";

// Experimental de-jelly support.
const char kEnableDeJelly[] = "enable-de-jelly";

// Enable compositing individual elements via hardware overlays when
// permitted by device.
// Setting the flag to "single-fullscreen" will try to promote a single
// fullscreen overlay and use it as main framebuffer where possible.
const char kEnableHardwareOverlays[] = "enable-hardware-overlays";

// Enables inspecting Viz Display Compositor objects. Default port is 9229.
// For local inspection use chrome://inspect#other
const char kEnableVizDevTools[] = "enable-viz-devtools";

// Enables hit-test debug logging.
const char kEnableVizHitTestDebug[] = "enable-viz-hit-test-debug";

#if defined(OS_CHROMEOS)
// ChromeOS uses one of two VideoDecoder implementations based on SoC/board
// specific configurations that are signalled via this command line flag.
// TODO(b/159825227): remove when the "old" video decoder is fully launched.
const char kPlatformDisallowsChromeOSDirectVideoDecoder[] =
    "platform-disallows-chromeos-direct-video-decoder";
#endif

// Effectively disables pipelining of compositor frame production stages by
// waiting for each stage to finish before completing a frame.
const char kRunAllCompositorStagesBeforeDraw[] =
    "run-all-compositor-stages-before-draw";

// Adds a DebugBorderDrawQuad to the top of the root RenderPass showing the
// damage rect after surface aggregation. Note that when enabled this feature
// sets the entire output rect as damaged after adding the quad to highlight the
// real damage rect, which could hide damage rect problems.
const char kShowAggregatedDamage[] = "show-aggregated-damage";

// Show debug borders for DC layers - red for overlays and blue for underlays.
// The debug borders are offset from the layer rect by a few pixels for clarity.
const char kShowDCLayerDebugBorders[] = "show-dc-layer-debug-borders";

#if defined(USE_NEVA_APPRUNTIME)
// Enables viz based First Meaninful Paint detection with milliseconds to wait
// to activate display scheduling after FMP detection. Timeout is available
// because many apps are not really ready when Blink reports First Meaninful
// Paint. Giving timeout 0 will swap frame immediately after detection.
const char kUseVizFMPWithTimeout[] = "use-viz-fmp-with-timeout";
#endif

base::Optional<uint32_t> GetDeadlineToSynchronizeSurfaces() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kRunAllCompositorStagesBeforeDraw)) {
    // In full-pipeline mode, surface deadlines should always be unlimited.
    return base::nullopt;
  }
  std::string deadline_to_synchronize_surfaces_string =
      command_line->GetSwitchValueASCII(
          switches::kDeadlineToSynchronizeSurfaces);
  if (deadline_to_synchronize_surfaces_string.empty())
    return viz::kDefaultActivationDeadlineInFrames;

  uint32_t activation_deadline_in_frames;
  if (!base::StringToUint(deadline_to_synchronize_surfaces_string,
                          &activation_deadline_in_frames)) {
    return base::nullopt;
  }
  return activation_deadline_in_frames;
}

#if defined(USE_NEVA_APPRUNTIME)
bool UseVizFMPWithTimeout() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  return command_line->HasSwitch(switches::kUseVizFMPWithTimeout);
}

uint32_t GetVizFMPTimeout() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  std::string use_viz_fmp_with_timeout_string =
      command_line->GetSwitchValueASCII(switches::kUseVizFMPWithTimeout);
  if (use_viz_fmp_with_timeout_string.empty())
    return viz::kDefaultVizFMPTimeout;

  uint32_t use_viz_fmp_with_timeout;
  if (!base::StringToUint(use_viz_fmp_with_timeout_string,
                          &use_viz_fmp_with_timeout)) {
    return viz::kDefaultVizFMPTimeout;
  }
  return use_viz_fmp_with_timeout;
}
#endif

}  // namespace switches
