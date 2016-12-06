// Copyright 2016 Google Inc. All Rights Reserved.
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

#ifndef STARBOARD_RASPI_SHARED_OPEN_MAX_OPEN_MAX_VIDEO_DECODE_COMPONENT_H_
#define STARBOARD_RASPI_SHARED_OPEN_MAX_OPEN_MAX_VIDEO_DECODE_COMPONENT_H_

#include <map>
#include <queue>

#include "starboard/common/ref_counted.h"
#include "starboard/raspi/shared/dispmanx_util.h"
#include "starboard/raspi/shared/open_max/open_max_component.h"

namespace starboard {
namespace raspi {
namespace shared {
namespace open_max {

class VideoFrameResourcePool
    : public RefCountedThreadSafe<VideoFrameResourcePool> {
 public:
  explicit VideoFrameResourcePool(size_t max_number_of_resources);
  ~VideoFrameResourcePool();

  DispmanxYUV420Resource* Alloc(int width,
                                int height,
                                int visible_width,
                                int visible_height);
  void Free(DispmanxYUV420Resource* resource);

  static void DisposeDispmanxYUV420Resource(void* context,
                                            void* dispmanx_resource);

 private:
  typedef std::queue<DispmanxYUV420Resource*> ResourceQueue;
  // Map frame height to resource handles.
  typedef std::map<int, ResourceQueue> ResourceMap;

  const size_t max_number_of_resources_;

  Mutex mutex_;
  size_t number_of_resources_;
  int last_frame_height_;
  ResourceMap resource_map_;
};

// Encapsulate a "OMX.broadcom.video_decode" component.  Note that member
// functions of this class is expected to be called from ANY threads as this
// class works with the VideoDecoder filter, the OpenMAX component, and also
// manages the disposition of Dispmanx resource.
class OpenMaxVideoDecodeComponent : public OpenMaxComponent {
 public:
  typedef starboard::shared::starboard::player::VideoFrame VideoFrame;

  OpenMaxVideoDecodeComponent();

  scoped_refptr<VideoFrame> ReadVideoFrame();

 private:
  scoped_refptr<VideoFrame> CreateVideoFrame(OMX_BUFFERHEADERTYPE* buffer);

  bool OnEnableOutputPort(OMXParamPortDefinition* port_definition) SB_OVERRIDE;

  scoped_refptr<VideoFrameResourcePool> resource_pool_;
  OMXParamPortDefinition output_port_definition_;
};

}  // namespace open_max
}  // namespace shared
}  // namespace raspi
}  // namespace starboard

#endif  // STARBOARD_RASPI_SHARED_OPEN_MAX_OPEN_MAX_VIDEO_DECODE_COMPONENT_H_
