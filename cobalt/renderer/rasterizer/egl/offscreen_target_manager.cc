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

#include "cobalt/renderer/rasterizer/egl/offscreen_target_manager.h"

#include <algorithm>

#include "base/hash_tables.h"
#include "cobalt/renderer/rasterizer/egl/rect_allocator.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/core/SkSurfaceProps.h"
#include "third_party/skia/include/gpu/GrRenderTarget.h"
#include "third_party/skia/include/gpu/GrTypes.h"

namespace {
// Structure describing the key for render target allocations in a given
// offscreen target atlas.
struct AllocationKey {
  AllocationKey(const cobalt::render_tree::Node* tree_node,
                const cobalt::math::SizeF& alloc_size)
      : node(tree_node),
        size(alloc_size) {}

  bool operator==(const AllocationKey& other) const {
    return node == other.node && size == other.size;
  }

  bool operator!=(const AllocationKey& other) const {
    return node != other.node || size != other.size;
  }

  bool operator<(const AllocationKey& rhs) const {
    return (node < rhs.node) ||
           (node == rhs.node &&
               (size.width() < rhs.size.width() ||
               (size.width() == rhs.size.width() &&
                   size.height() < rhs.size.height())));
  }

  const void* node;
  cobalt::math::SizeF size;
};
}  // namespace

namespace BASE_HASH_NAMESPACE {
#if defined(BASE_HASH_USE_HASH_STRUCT)
template <>
struct hash<AllocationKey> {
  size_t operator()(const AllocationKey& key) const {
    return reinterpret_cast<size_t>(key.node);
  }
};
#else
template <>
inline size_t hash_value<AllocationKey>(const AllocationKey& key) {
  return reinterpret_cast<size_t>(key.node);
}
#endif
}  // namespace BASE_HASH_NAMESPACE

namespace cobalt {
namespace renderer {
namespace rasterizer {
namespace egl {

namespace {

typedef base::hash_map<AllocationKey, math::Rect> AllocationMap;

int32_t NextPowerOf2(int32_t num) {
  // Return the smallest power of 2 that is greater than or equal to num.
  // This flips on all bits <= num, then num+1 will be the next power of 2.
  --num;
  num |= num >> 1;
  num |= num >> 2;
  num |= num >> 4;
  num |= num >> 8;
  num |= num >> 16;
  return num + 1;
}

bool IsPowerOf2(int32_t num) {
  return (num & (num - 1)) == 0;
}

}  // namespace

struct OffscreenTargetManager::OffscreenAtlas {
  explicit OffscreenAtlas(const math::Size& size)
      : allocator(size),
        allocations_used(0),
        needs_flush(false) {}

  RectAllocator allocator;
  AllocationMap allocation_map;
  size_t allocations_used;
  scoped_ptr<backend::FramebufferEGL> framebuffer;
  SkAutoTUnref<SkSurface> skia_surface;
  bool needs_flush;
};

OffscreenTargetManager::OffscreenTargetManager(
    backend::GraphicsContextEGL* graphics_context, GrContext* skia_context)
    : graphics_context_(graphics_context),
      skia_context_(skia_context),
      offscreen_atlas_size_(0, 0),
      offscreen_target_size_mask_(0, 0) {
}

OffscreenTargetManager::~OffscreenTargetManager() {
}

void OffscreenTargetManager::Update(const math::Size& frame_size) {
  // Set initial characteristics for offscreen target handling.
  if (offscreen_atlas_size_.IsEmpty()) {
    if (frame_size.width() >= 64 && frame_size.height() >= 64) {
      offscreen_atlas_size_.SetSize(
          NextPowerOf2(frame_size.width() / 16),
          NextPowerOf2(frame_size.height() / 16));
      offscreen_target_size_mask_.SetSize(
          NextPowerOf2(frame_size.width() / 64) - 1,
          NextPowerOf2(frame_size.height() / 64) - 1);
    } else {
      offscreen_atlas_size_.SetSize(16, 16);
      offscreen_target_size_mask_.SetSize(0, 0);
    }
    offscreen_atlas_size_max_.SetSize(
        NextPowerOf2(frame_size.width()),
        NextPowerOf2(frame_size.height()));

    offscreen_atlases_.push_back(CreateOffscreenAtlas(offscreen_atlas_size_));
    offscreen_cache_.reset(CreateOffscreenAtlas(offscreen_atlas_size_));
  }

  // Keep only the largest offscreen target atlas unless they are at the max
  // atlas size -- in which case, all those atlases are likely needed.
  for (size_t index = 0; index < offscreen_atlases_.size() - 1;) {
    const math::Size& size = offscreen_atlases_[index]->framebuffer->GetSize();
    if (size.width() < offscreen_atlas_size_max_.width() ||
        size.height() < offscreen_atlas_size_max_.height()) {
      offscreen_atlases_.erase(offscreen_atlases_.begin() + index);
    } else {
      ++index;
    }
  }

  // If any of the current atlases has more allocations used than the
  // current cache, then use that as the new cache.
  size_t most_used_atlas_index = 0;
  for (size_t index = 1; index < offscreen_atlases_.size(); ++index) {
    if (offscreen_atlases_[most_used_atlas_index]->allocations_used <
        offscreen_atlases_[index]->allocations_used) {
      most_used_atlas_index = index;
    }
  }

  OffscreenAtlas* most_used_atlas = offscreen_atlases_[most_used_atlas_index];
  if (offscreen_cache_->allocations_used < most_used_atlas->allocations_used) {
    // Just swap the current atlas with the cache if they are the same size.
    // Otherwise, delete the old atlas and create a new atlas as its
    // replacement.
    OffscreenAtlas* new_atlas;
    if (offscreen_cache_->framebuffer->GetSize() ==
        most_used_atlas->framebuffer->GetSize()) {
      new_atlas = offscreen_cache_.release();
    } else {
      new_atlas = CreateOffscreenAtlas(offscreen_atlas_size_);
    }
    offscreen_cache_.reset(offscreen_atlases_[most_used_atlas_index]);
    offscreen_atlases_.weak_erase(offscreen_atlases_.begin() +
                                  most_used_atlas_index);
    offscreen_atlases_.push_back(new_atlas);
  }
  offscreen_cache_->allocations_used = 0;

  // Reset all current atlases for use this frame.
  for (size_t index = 0; index < offscreen_atlases_.size(); ++index) {
    OffscreenAtlas* atlas = offscreen_atlases_[index];
    atlas->allocator.Reset();
    atlas->allocation_map.clear();
    atlas->allocations_used = 0;
  }
}

void OffscreenTargetManager::Flush() {
  if (offscreen_cache_->needs_flush) {
    offscreen_cache_->needs_flush = false;
    offscreen_cache_->skia_surface->getCanvas()->flush();
  }
  for (size_t index = 0; index < offscreen_atlases_.size(); ++index) {
    if (offscreen_atlases_[index]->needs_flush) {
      offscreen_atlases_[index]->needs_flush = false;
      offscreen_atlases_[index]->skia_surface->getCanvas()->flush();
    }
  }
}

bool OffscreenTargetManager::GetCachedOffscreenTarget(
    const render_tree::Node* node, const math::SizeF& size,
    backend::FramebufferEGL** out_framebuffer, SkCanvas** out_skia_canvas,
    math::RectF* out_target_rect) {
  AllocationMap::iterator iter = offscreen_cache_->allocation_map.find(
      AllocationKey(node, size));
  if (iter != offscreen_cache_->allocation_map.end()) {
    offscreen_cache_->allocations_used += 1;
    *out_framebuffer = offscreen_cache_->framebuffer.get();
    *out_skia_canvas = offscreen_cache_->skia_surface->getCanvas();
    *out_target_rect = iter->second;
    return true;
  }
  return false;
}

void OffscreenTargetManager::AllocateOffscreenTarget(
    const render_tree::Node* node, const math::SizeF& size,
    backend::FramebufferEGL** out_framebuffer, SkCanvas** out_skia_canvas,
    math::RectF* out_target_rect) {
  // Get an offscreen target for rendering. Align up the requested target size
  // to improve usage of the atlas (since more requests will have the same
  // aligned width or height).
  DCHECK(IsPowerOf2(offscreen_target_size_mask_.width() + 1));
  DCHECK(IsPowerOf2(offscreen_target_size_mask_.height() + 1));
  math::Size ideal_size(
      (std::max(static_cast<int>(size.width() + 0.5f), 1) +
          offscreen_target_size_mask_.width()) &
          ~offscreen_target_size_mask_.width(),
      (std::max(static_cast<int>(size.height() + 0.5f), 1) +
          offscreen_target_size_mask_.height()) &
          ~offscreen_target_size_mask_.height());
  math::Size target_size(
      std::min(ideal_size.width(), offscreen_atlas_size_max_.width()),
      std::min(ideal_size.height(), offscreen_atlas_size_max_.height()));
  math::Rect target_rect(0, 0, 0, 0);
  OffscreenAtlas* atlas = NULL;

  // See if there's room in the offscreen cache for additional targets.
  atlas = offscreen_cache_.get();
  target_rect = atlas->allocator.Allocate(target_size);

  if (target_rect.IsEmpty()) {
    // See if there's room in the most recently created offscreen target
    // atlases.
    for (size_t index = offscreen_atlases_.size(); index > 0;) {
      atlas = offscreen_atlases_[--index];
      target_rect = atlas->allocator.Allocate(target_size);
      if (!target_rect.IsEmpty()) {
        break;
      }
    }
  }

  if (target_rect.IsEmpty()) {
    // Create a new offscreen atlas, bigger than the previous, so that
    // eventually only one offscreen atlas is needed per frame.
    bool grew = false;
    if (offscreen_atlas_size_.width() < target_size.width()) {
      offscreen_atlas_size_.set_width(NextPowerOf2(target_size.width()));
      grew = true;
    }
    if (offscreen_atlas_size_.height() < target_size.height()) {
      offscreen_atlas_size_.set_height(NextPowerOf2(target_size.height()));
      grew = true;
    }
    if (!grew) {
      // Grow the offscreen atlas while keeping it square-ish.
      if (offscreen_atlas_size_.width() <= offscreen_atlas_size_.height() &&
          offscreen_atlas_size_.width() < offscreen_atlas_size_max_.width()) {
        offscreen_atlas_size_.set_width(offscreen_atlas_size_.width() * 2);
      } else if (offscreen_atlas_size_.height() <
                 offscreen_atlas_size_max_.height()) {
        offscreen_atlas_size_.set_height(offscreen_atlas_size_.height() * 2);
      }
    }

    atlas = CreateOffscreenAtlas(offscreen_atlas_size_);
    offscreen_atlases_.push_back(atlas);
    target_rect = atlas->allocator.Allocate(target_size);
  }
  DCHECK(!target_rect.IsEmpty());

  // Clear the atlas if this will be the first draw into it.
  if (atlas->allocation_map.empty()) {
    atlas->skia_surface->getCanvas()->clear(SK_ColorTRANSPARENT);
  }

  atlas->allocation_map.insert(AllocationMap::value_type(
      AllocationKey(node, size), target_rect));
  atlas->allocations_used += 1;
  atlas->needs_flush = true;

  *out_framebuffer = atlas->framebuffer.get();
  *out_skia_canvas = atlas->skia_surface->getCanvas();
  *out_target_rect = target_rect;
}

OffscreenTargetManager::OffscreenAtlas*
    OffscreenTargetManager::CreateOffscreenAtlas(const math::Size& size) {
  OffscreenAtlas* atlas = new OffscreenAtlas(size);

  // Create a new framebuffer.
  atlas->framebuffer.reset(new backend::FramebufferEGL(
      graphics_context_, size, GL_RGBA, GL_NONE));

  // Wrap the framebuffer as a skia surface.
  GrBackendRenderTargetDesc skia_desc;
  skia_desc.fWidth = size.width();
  skia_desc.fHeight = size.height();
  skia_desc.fConfig = kRGBA_8888_GrPixelConfig;
  skia_desc.fOrigin = kTopLeft_GrSurfaceOrigin;
  skia_desc.fSampleCnt = 0;
  skia_desc.fStencilBits = 0;
  skia_desc.fRenderTargetHandle = atlas->framebuffer->gl_handle();

  SkAutoTUnref<GrRenderTarget> skia_render_target(
      skia_context_->wrapBackendRenderTarget(skia_desc));
  SkSurfaceProps skia_surface_props(
      SkSurfaceProps::kUseDistanceFieldFonts_Flag,
      SkSurfaceProps::kLegacyFontHost_InitType);
  atlas->skia_surface.reset(SkSurface::NewRenderTargetDirect(
      skia_render_target, &skia_surface_props));

  return atlas;
}

}  // namespace egl
}  // namespace rasterizer
}  // namespace renderer
}  // namespace cobalt
