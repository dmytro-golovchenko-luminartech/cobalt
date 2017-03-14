// Copyright 2017 Google Inc. All Rights Reserved.
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

#ifndef COBALT_RENDERER_RASTERIZER_EGL_DRAW_OBJECT_MANAGER_H_
#define COBALT_RENDERER_RASTERIZER_EGL_DRAW_OBJECT_MANAGER_H_

#include <vector>

#include "base/memory/scoped_ptr.h"
#include "base/memory/scoped_vector.h"
#include "cobalt/math/rect_f.h"
#include "cobalt/renderer/rasterizer/egl/draw_object.h"
#include "cobalt/renderer/rasterizer/egl/graphics_state.h"
#include "cobalt/renderer/rasterizer/egl/shader_program_manager.h"

namespace cobalt {
namespace renderer {
namespace rasterizer {
namespace egl {

// Helper class to manage a set of draw objects. This facilitates sorting the
// objects to minimize GPU state changes.
class DrawObjectManager {
 public:
  enum DrawType {
    kDrawRectTexture = 0,
    kDrawRectColorTexture,
    kDrawPolyColor,
    kDrawTransparent,
    kDrawCount,
  };

  void AddOpaqueDraw(scoped_ptr<DrawObject> object, DrawType type);
  void AddTransparentDraw(scoped_ptr<DrawObject> object, DrawType type,
                          const math::RectF& bounds);

  void ExecuteUpdateVertexBuffer(GraphicsState* graphics_state,
      ShaderProgramManager* program_manager);
  void ExecuteRasterizeOffscreen(GraphicsState* graphics_state,
      ShaderProgramManager* program_manager);
  void ExecuteRasterizeNormal(GraphicsState* graphics_state,
      ShaderProgramManager* program_manager);

 private:
  struct TransparentObjectInfo {
    TransparentObjectInfo(DrawType object_type,
                          const math::RectF& object_bounds)
        : bounds(object_bounds),
          type(object_type) {}
    math::RectF bounds;
    DrawType type;
  };

  ScopedVector<DrawObject> draw_objects_[kDrawCount];
  std::vector<TransparentObjectInfo> transparent_object_info_;
};

}  // namespace egl
}  // namespace rasterizer
}  // namespace renderer
}  // namespace cobalt

#endif  // COBALT_RENDERER_RASTERIZER_EGL_DRAW_OBJECT_MANAGER_H_
