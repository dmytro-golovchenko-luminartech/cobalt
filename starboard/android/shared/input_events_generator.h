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

#ifndef STARBOARD_ANDROID_SHARED_INPUT_EVENTS_GENERATOR_H_
#define STARBOARD_ANDROID_SHARED_INPUT_EVENTS_GENERATOR_H_

#include <android/input.h>
#include <map>
#include <vector>

#include "starboard/input.h"
#include "starboard/shared/starboard/application.h"
#include "starboard/window.h"

namespace starboard {
namespace android {
namespace shared {

class InputEventsGenerator {
 public:
  explicit InputEventsGenerator(SbWindow window);
  virtual ~InputEventsGenerator();

  // Translates an Android input event into a series of Starboard application
  // events. The caller owns the new events and must delete them when done with
  // them.
  bool CreateInputEvents(
      AInputEvent* android_event,
      std::vector< ::starboard::shared::starboard::Application::Event*>*
          events);

 private:
  enum FlatAxis {
    kLeftX,
    kLeftY,
    kRightX,
    kRightY,
    kNumAxes,
  };

  bool ProcessKeyEvent(
      AInputEvent* android_event,
      std::vector< ::starboard::shared::starboard::Application::Event*>*
          events);
  bool ProcessMotionEvent(
      AInputEvent* android_event,
      std::vector< ::starboard::shared::starboard::Application::Event*>*
          events);
  void ProcessJoyStickEvent(
      FlatAxis axis,
      int32_t motion_axis,
      AInputEvent* android_event,
      std::vector< ::starboard::shared::starboard::Application::Event*>*
          events);
  void UpdateDeviceFlatMapIfNecessary(AInputEvent* android_event);

  SbWindow window_;

  // Map the device id with joystick flat position.
  // Cache the flat area of joystick to avoid calling jni functions frequently.
  std::map<int32_t, std::vector<float> > device_flat_;
};

}  // namespace shared
}  // namespace android
}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_INPUT_EVENTS_GENERATOR_H_
