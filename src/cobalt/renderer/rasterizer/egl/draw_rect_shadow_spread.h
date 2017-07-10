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

#ifndef COBALT_RENDERER_RASTERIZER_EGL_DRAW_RECT_SHADOW_SPREAD_H_
#define COBALT_RENDERER_RASTERIZER_EGL_DRAW_RECT_SHADOW_SPREAD_H_

#include <vector>

#include "cobalt/math/rect_f.h"
#include "cobalt/render_tree/color_rgba.h"
#include "cobalt/renderer/rasterizer/egl/draw_object.h"

namespace cobalt {
namespace renderer {
namespace rasterizer {
namespace egl {

// Example CSS box shadow (outset):
//   +-------------------------------------+
//   | Box shadow "blur" region            |
//   |   +-----------------------------+   |
//   |   | Box shadow "spread" region  |   |
//   |   |   +---------------------+   |   |
//   |   |   | Box shadow rect     |   |   |
//   |   |   | (exclude geometry)  |   |   |
//   |   |   +---------------------+   |   |
//   |   |                             |   |
//   |   +-----------------------------+   |
//   | (include scissor)                   |
//   +-------------------------------------+

// Handles drawing the solid "spread" portion of a box shadow. The
// |include_scissor| specifies which pixels can be touched.
class DrawRectShadowSpread : public DrawObject {
 public:
  // Fill the area between |inner_rect| and |outer_rect| with the specified
  // color.
  DrawRectShadowSpread(GraphicsState* graphics_state,
                       const BaseState& base_state,
                       const math::RectF& inner_rect,
                       const math::RectF& outer_rect,
                       const render_tree::ColorRGBA& color,
                       const math::RectF& include_scissor);

  void ExecuteUpdateVertexBuffer(GraphicsState* graphics_state,
      ShaderProgramManager* program_manager) OVERRIDE;
  void ExecuteRasterize(GraphicsState* graphics_state,
      ShaderProgramManager* program_manager) OVERRIDE;
  base::TypeId GetTypeId() const OVERRIDE;

 private:
  struct VertexAttributes {
    float position[2];
    float offset[2];
    uint32_t color;
  };

  void SetVertex(VertexAttributes* vertex, float x, float y);

  math::RectF inner_rect_;
  math::RectF outer_rect_;
  math::RectF include_scissor_;
  uint32_t color_;

  uint8_t* vertex_buffer_;
};

}  // namespace egl
}  // namespace rasterizer
}  // namespace renderer
}  // namespace cobalt

#endif  // COBALT_RENDERER_RASTERIZER_EGL_DRAW_RECT_SHADOW_SPREAD_H_
