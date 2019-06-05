// Copyright 2019 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Similar to directfb/blitter_internal.h.

#ifndef STARBOARD_SHARED_BLITTERGLES_BLITTER_CONTEXT_H_
#define STARBOARD_SHARED_BLITTERGLES_BLITTER_CONTEXT_H_

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <memory>

#include "starboard/blitter.h"
#include "starboard/common/optional.h"
#include "starboard/shared/blittergles/blitter_internal.h"
#include "starboard/shared/blittergles/color_shader_program.h"
#include "starboard/shared/internal_only.h"

struct SbBlitterContextPrivate {
 public:
  explicit SbBlitterContextPrivate(SbBlitterDevice device);
  ~SbBlitterContextPrivate();

  // Store a reference to the current rendering target.
  SbBlitterRenderTargetPrivate* current_render_target;

  // Keep track of the device used to create this context.
  SbBlitterDevicePrivate* device;

  // Whether or not blending is enabled on this context.
  bool blending_enabled;

  // The current color, used to determine the color of fill rectangles and blit
  // call color modulation.
  SbBlitterColor current_color;

  // Whether or not blits should be modulated by the current color.
  bool modulate_blits_with_color;

  // The current scissor rectangle.
  SbBlitterRect scissor;

  // Creates the shader if it does not already exist.
  const starboard::shared::blittergles::ColorShaderProgram&
      GetColorShaderProgram();

  // Will call eglMakeCurrent() and glBindFramebuffer() for context's
  // current_render_target. Returns true on success, false on failure.
  bool MakeCurrent();

  // Returns false if an error occurred during initialization (indicating that
  // this object is invalid).
  bool IsValid() const { return !error_; }

  // Whether or not this context has been set to current or not.
  bool is_current;

  // Helper class to allow one to create a RAII object that acquires the
  // SbBlitterContext object upon construction and handles binding/unbinding of
  // the egl_context field, as well as initializing fields that have deferred
  // creation.
  class ScopedCurrentContext {
   public:
    explicit ScopedCurrentContext(SbBlitterContext context);
    ~ScopedCurrentContext();

    // Returns true if an error occurred during initialization (indicating that
    // this object is invalid).
    bool InitializationError() const { return error_; }

   private:
    SbBlitterContext context_;
    bool error_;

    // Keeps track of whether this context was current on the calling thread.
    bool was_current_;
  };

 private:
  bool EnsureEGLContextInitialized();

  bool EnsureDummySurfaceInitialized();

  starboard::optional<EGLSurface> GetEGLSurfaceFromRenderTarget();

  // If we don't have any information about the display window, this field will
  // be created with a best-guess EGLConfig.
  EGLContext egl_context_;

  // GL framebuffers can use a dummy EGLSurface if there isn't a surface bound
  // already.
  EGLSurface dummy_surface_;

  std::unique_ptr<starboard::shared::blittergles::ColorShaderProgram>
      color_shader_;

  bool error_;
};

#endif  // STARBOARD_SHARED_BLITTERGLES_BLITTER_CONTEXT_H_
