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

#include "starboard/android/shared/application_android.h"

#include <android_native_app_glue.h>
#include <time.h>

#include <map>
#include <vector>

#include "starboard/android/shared/file_internal.h"
#include "starboard/android/shared/input_event.h"
#include "starboard/android/shared/jni_env_ext.h"
#include "starboard/android/shared/window_internal.h"
#include "starboard/event.h"
#include "starboard/log.h"
#include "starboard/shared/starboard/audio_sink/audio_sink_internal.h"
#include "starboard/string.h"

namespace {

#ifndef NDEBUG
std::map<int, const char*> CreateAppCmdNamesMap() {
  std::map<int, const char*> m;
#define X(cmd) m[cmd] = #cmd
  X(APP_CMD_INPUT_CHANGED);
  X(APP_CMD_INIT_WINDOW);
  X(APP_CMD_TERM_WINDOW);
  X(APP_CMD_WINDOW_RESIZED);
  X(APP_CMD_WINDOW_REDRAW_NEEDED);
  X(APP_CMD_CONTENT_RECT_CHANGED);
  X(APP_CMD_GAINED_FOCUS);
  X(APP_CMD_LOST_FOCUS);
  X(APP_CMD_CONFIG_CHANGED);
  X(APP_CMD_LOW_MEMORY);
  X(APP_CMD_START);
  X(APP_CMD_RESUME);
  X(APP_CMD_SAVE_STATE);
  X(APP_CMD_PAUSE);
  X(APP_CMD_STOP);
  X(APP_CMD_DESTROY);
#undef X
  return m;
}
std::map<int, const char*> kAppCmdNames = CreateAppCmdNamesMap();
#endif  // NDEBUG

}  // namespace

namespace starboard {
namespace android {
namespace shared {

// "using" doesn't work with class members, so make a local convienience type.
typedef ::starboard::shared::starboard::Application::Event Event;

ApplicationAndroid::ApplicationAndroid(struct android_app* state)
    : android_state_(state), window_(kSbWindowInvalid) {}

ApplicationAndroid::~ApplicationAndroid() {}

void ApplicationAndroid::Initialize() {
  // Called once here to help SbTimeZoneGet*Name()
  tzset();
  SbFileAndroidInitialize(android_state_->activity);
  SbAudioSinkPrivate::Initialize();
}

void ApplicationAndroid::Teardown() {
  SbAudioSinkPrivate::TearDown();
  SbFileAndroidTeardown();
}

SbWindow ApplicationAndroid::CreateWindow(const SbWindowOptions* options) {
  SB_UNREFERENCED_PARAMETER(options);
  if (SbWindowIsValid(window_)) {
    return kSbWindowInvalid;
  }
  window_ = new SbWindowPrivate;
  window_->native_window = android_state_->window;
  return window_;
}

bool ApplicationAndroid::DestroyWindow(SbWindow window) {
  if (!SbWindowIsValid(window)) {
    return false;
  }
  delete window_;
  window_ = kSbWindowInvalid;
  return true;
}

bool ApplicationAndroid::DispatchNextEvent() {
  // We already dispatched our own system events in OnAndroidCommand() and/or
  // OnAndroidInput(), but we may have an injected event to dispatch.
  DispatchAndDelete(GetNextEvent());

  // Keep pumping events until Android is done with its lifecycle.
  return !android_state_->destroyRequested;
}

Event* ApplicationAndroid::WaitForSystemEventWithTimeout(SbTime time) {
  int ident;
  int looper_events;
  struct android_poll_source* source;
  int timeMillis = time / 1000;

  ident = ALooper_pollAll(timeMillis, NULL, &looper_events,
                          reinterpret_cast<void**>(&source));
  if (ident >= 0 && source != NULL) {
    // This will end up calling OnAndroidCommand() or OnAndroidInput().
    source->process(android_state_, source);
  }

  // Always return NULL since we already dispatched our own system events.
  return NULL;
}

void ApplicationAndroid::WakeSystemEventWait() {
  ALooper_wake(android_state_->looper);
}

void ApplicationAndroid::OnAndroidCommand(int32_t cmd) {
#ifndef NDEBUG
  const char* cmd_name = kAppCmdNames[cmd];
  if (cmd_name) {
    SB_LOG(INFO) << cmd_name;
  } else {
    SB_LOG(INFO) << "APP_CMD_[unknown  " << cmd << "]";
  }
#endif

  // The window surface being created/destroyed is more significant than the
  // Activity lifecycle since Cobalt can't do anything at all if it doesn't have
  // a window surface to draw on.
  switch (cmd) {
    case APP_CMD_INIT_WINDOW:
      if (window_) {
        window_->native_window = android_state_->window;
      }
      if (state() == kStateUnstarted) {
        // This is the initial launch, so we have to start Cobalt now that we
        // have a window.
        DispatchStart();
      } else {
        // Now that we got a window back, change the command for the switch
        // below to sync up with the current activity lifecycle.
        cmd = android_state_->activityState;
      }
      break;
    case APP_CMD_TERM_WINDOW:
      // Cobalt can't keep running without a window, even if the Activity hasn't
      // stopped yet. DispatchAndDelete() will inject events as needed if we're
      // not already paused.
      DispatchAndDelete(new Event(kSbEventTypeSuspend, NULL, NULL));
      if (window_) {
        window_->native_window = NULL;
      }
      break;
    case APP_CMD_DESTROY:
      // The window is already gone before we get destroyed, so this must be in
      // this switch. In practice we don't ever get destroyed, and the process
      // is just killed instead.
      DispatchAndDelete(new Event(kSbEventTypeStop, NULL, NULL));
      break;
  }

  // If there's a window, sync the app state to the Activity lifecycle, letting
  // DispatchAndDelete() inject events as needed if we missed a state.
  if (android_state_->window) {
    switch (cmd) {
      case APP_CMD_START:
        DispatchAndDelete(new Event(kSbEventTypeResume, NULL, NULL));
        break;
      case APP_CMD_RESUME:
        DispatchAndDelete(new Event(kSbEventTypeUnpause, NULL, NULL));
        break;
      case APP_CMD_PAUSE:
        DispatchAndDelete(new Event(kSbEventTypePause, NULL, NULL));
        break;
      case APP_CMD_STOP:
        // In practice we've already suspended in APP_CMD_TERM_WINDOW above,
        // and DispatchAndDelete() will squelch this event.
        DispatchAndDelete(new Event(kSbEventTypeSuspend, NULL, NULL));
        break;
    }
  }
}

bool ApplicationAndroid::OnAndroidInput(AInputEvent* androidEvent) {
  Event *event = CreateInputEvent(androidEvent, window_);
  if (event == NULL) {
    return false;
  }
  DispatchAndDelete(event);
  return true;
}

static void GetArgs(struct android_app* state, std::vector<char*>* out_args) {
  out_args->push_back(SbStringDuplicate("starboard"));

  JniEnvExt* env = JniEnvExt::Get();

  jobjectArray args_array = (jobjectArray)env->CallActivityObjectMethod(
      "getArgs", "()[Ljava/lang/String;");
  jint argc = env->GetArrayLength(args_array);

  for (jint i = 0; i < argc; i++) {
    jstring element = (jstring)env->GetObjectArrayElement(args_array, i);
    const char* utf_chars = env->GetStringUTFChars(element, NULL);
    out_args->push_back(SbStringDuplicate(utf_chars));
    env->ReleaseStringUTFChars(element, utf_chars);
  }
  state->activity->vm->DetachCurrentThread();
}

/**
 * This is the main entry point of a native application that is using
 * android_native_app_glue.  It runs in its own thread, using the event
 * loop in ApplicationAndroid.
 */
extern "C" void android_main(struct android_app* state) {
  JniEnvExt::Initialize(state->activity);

  std::vector<char*> args;
  GetArgs(state, &args);

  ApplicationAndroid application(state);
  state->userData = &application;
  state->onAppCmd = ApplicationAndroid::HandleCommand;
  state->onInputEvent = ApplicationAndroid::HandleInput;
  application.Run(args.size(), args.data());

  for (std::vector<char*>::iterator it = args.begin(); it != args.end(); ++it) {
    SbMemoryDeallocate(*it);
  }
}

static SB_C_INLINE ApplicationAndroid* ToApplication(
    struct android_app* app) {
  return static_cast<ApplicationAndroid*>(app->userData);
}

// static
void ApplicationAndroid::HandleCommand(struct android_app* app, int32_t cmd) {
  SB_LOG(INFO) << "HandleCommand " << cmd;
  ToApplication(app)->OnAndroidCommand(cmd);
}

// static
int32_t ApplicationAndroid::HandleInput(
    struct android_app* app, AInputEvent* event) {
  return ToApplication(app)->OnAndroidInput(event) ? 1 : 0;
}

// TODO: Figure out how to export ANativeActivity_onCreate()
extern "C" SB_EXPORT_PLATFORM void CobaltActivity_onCreate(
    ANativeActivity *activity, void *savedState, size_t savedStateSize) {
  SB_LOG(INFO) << "CobaltActivity_onCreate";
  ANativeActivity_onCreate(activity, savedState, savedStateSize);
}

}  // namespace shared
}  // namespace android
}  // namespace starboard
