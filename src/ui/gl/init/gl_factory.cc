// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gl/init/gl_factory.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/trace_event/trace_event.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/gl_utils.h"
#include "ui/gl/gl_version_info.h"
#include "ui/gl/init/gl_initializer.h"

namespace gl {
namespace init {

namespace {

bool g_is_angle_enabled = true;

bool ShouldFallbackToSoftwareGL() {
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  std::string requested_implementation_gl_name =
      cmd->GetSwitchValueASCII(switches::kUseGL);

  if (cmd->HasSwitch(switches::kUseGL) &&
      requested_implementation_gl_name == "any") {
    return true;
  } else {
    return false;
  }
}

GLImplementationParts GetRequestedGLImplementation(
    bool* fallback_to_software_gl) {
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  std::string requested_implementation_gl_name =
      cmd->GetSwitchValueASCII(switches::kUseGL);
  std::string requested_implementation_angle_name =
      cmd->GetSwitchValueASCII(switches::kUseANGLE);
  if (requested_implementation_gl_name == kGLImplementationDisabledName) {
    return GLImplementationParts(kGLImplementationDisabled);
  }

  std::vector<GLImplementation> allowed_impls = GetAllowedGLImplementations();

  if (cmd->HasSwitch(switches::kDisableES3GLContext)) {
    auto iter = std::find(allowed_impls.begin(), allowed_impls.end(),
                          kGLImplementationDesktopGLCoreProfile);
    if (iter != allowed_impls.end())
      allowed_impls.erase(iter);
  }

  if (cmd->HasSwitch(switches::kDisableES3GLContextForTesting)) {
    GLVersionInfo::DisableES3ForTesting();
  }

  // If the passthrough command decoder is enabled, put ANGLE first if allowed
  if (g_is_angle_enabled && gl::UsePassthroughCommandDecoder(cmd)) {
    auto iter = std::find(allowed_impls.begin(), allowed_impls.end(),
                          kGLImplementationEGLANGLE);
    if (iter != allowed_impls.end()) {
      allowed_impls.erase(iter);
      allowed_impls.insert(allowed_impls.begin(), kGLImplementationEGLANGLE);
    }
  }

  if (allowed_impls.empty()) {
    LOG(ERROR) << "List of allowed GL implementations is empty.";
    return GLImplementationParts(kGLImplementationNone);
  }

  // The default implementation is always the first one in list.
  GLImplementationParts impl = GLImplementationParts(allowed_impls[0]);
  UMA_HISTOGRAM_ENUMERATION("GPU.PreferredGLImplementation", impl.gl);

  *fallback_to_software_gl = false;
  if (cmd->HasSwitch(switches::kOverrideUseSoftwareGLForTests)) {
    impl = GetSoftwareGLForTestsImplementation();
  } else if (cmd->HasSwitch(switches::kUseGL)) {
    if (requested_implementation_gl_name == "any") {
      *fallback_to_software_gl = true;
    } else if ((requested_implementation_gl_name ==
                kGLImplementationSwiftShaderName) ||
               (requested_implementation_gl_name ==
                kGLImplementationSwiftShaderForWebGLName)) {
      impl = GLImplementationParts(kGLImplementationSwiftShaderGL);
    } else {
      impl = GetNamedGLImplementation(requested_implementation_gl_name,
                                      requested_implementation_angle_name);
      if (!base::Contains(allowed_impls, impl.gl)) {
        LOG(ERROR) << "Requested GL implementation is not available.";
        UMA_HISTOGRAM_ENUMERATION("GPU.RequestedGLImplementation",
                                  kGLImplementationNone);
        return GLImplementationParts(kGLImplementationNone);
      }
    }
  }

  UMA_HISTOGRAM_ENUMERATION("GPU.RequestedGLImplementation", impl.gl);
  return impl;
}

bool InitializeGLOneOffPlatformHelper(bool init_extensions) {
  TRACE_EVENT1("gpu,startup", "gl::init::InitializeGLOneOffPlatformHelper",
               "init_extensions", init_extensions);

  bool fallback_to_software_gl = ShouldFallbackToSoftwareGL();
  const base::CommandLine* cmd = base::CommandLine::ForCurrentProcess();
  bool disable_gl_drawing = cmd->HasSwitch(switches::kDisableGLDrawingForTests);

  return InitializeGLOneOffPlatformImplementation(
      fallback_to_software_gl, disable_gl_drawing, init_extensions);
}

}  // namespace

bool InitializeGLOneOff() {
  TRACE_EVENT0("gpu,startup", "gl::init::InitializeOneOff");

  if (!InitializeStaticGLBindingsOneOff())
    return false;
  if (GetGLImplementation() == kGLImplementationDisabled)
    return true;

  return InitializeGLOneOffPlatformHelper(true);
}

bool InitializeGLNoExtensionsOneOff(bool init_bindings) {
  TRACE_EVENT1("gpu,startup", "gl::init::InitializeNoExtensionsOneOff",
               "init_bindings", init_bindings);
  if (init_bindings) {
    if (!InitializeStaticGLBindingsOneOff())
      return false;
    if (GetGLImplementation() == kGLImplementationDisabled)
      return true;
  }

  return InitializeGLOneOffPlatformHelper(false);
}

bool InitializeStaticGLBindingsOneOff() {
  DCHECK_EQ(kGLImplementationNone, GetGLImplementation());

  bool fallback_to_software_gl = false;
  GLImplementationParts impl =
      GetRequestedGLImplementation(&fallback_to_software_gl);
  if (impl.gl == gl::kGLImplementationDisabled) {
    gl::SetGLImplementation(gl::kGLImplementationDisabled);
    return true;
  } else if (impl.gl == gl::kGLImplementationNone) {
    return false;
  }

  return InitializeStaticGLBindingsImplementation(impl,
                                                  fallback_to_software_gl);
}

bool InitializeStaticGLBindingsImplementation(GLImplementationParts impl,
                                              bool fallback_to_software_gl) {
  if (IsSoftwareGLImplementation(impl))
    fallback_to_software_gl = false;

  bool initialized = InitializeStaticGLBindings(impl);
  if (!initialized && fallback_to_software_gl) {
    ShutdownGL(/*due_to_fallback*/ true);
    initialized =
        InitializeStaticGLBindings(GetLegacySoftwareGLImplementation());
  }
  if (!initialized) {
    ShutdownGL(/*due_to_fallback*/ false);
    return false;
  }
  return true;
}

bool InitializeGLOneOffPlatformImplementation(bool fallback_to_software_gl,
                                              bool disable_gl_drawing,
                                              bool init_extensions) {
  if (IsSoftwareGLImplementation(GetGLImplementationParts()))
    fallback_to_software_gl = false;

  bool initialized = InitializeGLOneOffPlatform();
  if (!initialized && fallback_to_software_gl) {
    ShutdownGL(/*due_to_fallback*/ true);
    initialized =
        InitializeStaticGLBindings(GetLegacySoftwareGLImplementation()) &&
        InitializeGLOneOffPlatform();
  }
  if (initialized && init_extensions) {
    initialized = InitializeExtensionSettingsOneOffPlatform();
  }

  if (!initialized)
    ShutdownGL(false);

  if (initialized) {
    DVLOG(1) << "Using "
             << GetGLImplementationGLName(GetGLImplementationParts())
             << " GL implementation.";
    if (disable_gl_drawing)
      InitializeNullDrawGLBindings();
  }
  return initialized;
}

void ShutdownGL(bool due_to_fallback) {
  ShutdownGLPlatform();

  UnloadGLNativeLibraries(due_to_fallback);
  SetGLImplementation(kGLImplementationNone);
}

scoped_refptr<GLSurface> CreateOffscreenGLSurface(const gfx::Size& size) {
  return CreateOffscreenGLSurfaceWithFormat(size, GLSurfaceFormat());
}

void DisableANGLE() {
  DCHECK_NE(GetGLImplementation(), kGLImplementationEGLANGLE);
  g_is_angle_enabled = false;
}

}  // namespace init
}  // namespace gl
