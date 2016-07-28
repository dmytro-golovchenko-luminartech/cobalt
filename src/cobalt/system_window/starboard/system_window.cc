/*
 * Copyright 2015 Google Inc. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "cobalt/base/event_dispatcher.h"
#include "cobalt/system_window/keyboard_event.h"
#include "cobalt/system_window/starboard/system_window.h"

namespace cobalt {
namespace system_window {

namespace {
SystemWindowStarboard* g_the_window = NULL;
}  // namespace

SystemWindowStarboard::SystemWindowStarboard(
    base::EventDispatcher* event_dispatcher, const math::Size& window_size)
    : SystemWindow(event_dispatcher, window_size),
      window_(kSbWindowInvalid),
      key_down_(false) {
  SbWindowOptions options;
  SbWindowSetDefaultOptions(&options);
  options.size.width = window_size.width();
  options.size.height = window_size.height();
  window_ = SbWindowCreate(&options);
  DCHECK(SbWindowIsValid(window_));
  DCHECK(!g_the_window) << "TODO: Support multiple SystemWindows.";
  g_the_window = this;
}

SystemWindowStarboard::~SystemWindowStarboard() {
  DCHECK_EQ(this, g_the_window);
  if (g_the_window == this) {
    g_the_window = NULL;
  }
  SbWindowDestroy(window_);
}

SbWindow SystemWindowStarboard::GetSbWindow() { return window_; }

void* SystemWindowStarboard::GetWindowHandle() {
  return SbWindowGetPlatformHandle(window_);
}

void SystemWindowStarboard::HandleInputEvent(const SbInputData& data) {
  DCHECK_EQ(window_, data.window);

  if (data.type == kSbInputEventTypePress) {
    // Starboard handily uses the Microsoft key mapping, which is also what
    // Cobalt uses.
    int key_code = static_cast<int>(data.key);
    scoped_ptr<KeyboardEvent> keyboard_event(
        new KeyboardEvent(KeyboardEvent::kKeyDown, key_code, data.key_modifiers,
                          key_down_ /* is_repeat */));
    key_down_ = true;
    event_dispatcher()->DispatchEvent(keyboard_event.PassAs<base::Event>());
  } else if (data.type == kSbInputEventTypeUnpress) {
    key_down_ = false;
    int key_code = static_cast<int>(data.key);
    scoped_ptr<KeyboardEvent> keyboard_event(
        new KeyboardEvent(KeyboardEvent::kKeyUp, key_code, data.key_modifiers,
                          false /* is_repeat */));
    event_dispatcher()->DispatchEvent(keyboard_event.PassAs<base::Event>());
  }
}

scoped_ptr<SystemWindow> CreateSystemWindow(
    base::EventDispatcher* event_dispatcher, const math::Size& window_size) {
  return scoped_ptr<SystemWindow>(
      new SystemWindowStarboard(event_dispatcher, window_size));
}

// Returns true if the event was handled.
void HandleInputEvent(const SbEvent* event) {
  if (event->type != kSbEventTypeInput) {
    return;
  }

  DCHECK(g_the_window);
  DCHECK(event->data);
  SbInputData* data = reinterpret_cast<SbInputData*>(event->data);
  g_the_window->HandleInputEvent(*data);
  return;
}

}  // namespace system_window
}  // namespace cobalt
