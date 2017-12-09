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

#ifndef COBALT_DOM_ON_SCREEN_KEYBOARD_H_
#define COBALT_DOM_ON_SCREEN_KEYBOARD_H_

#include <memory>
#include <string>
#include <unordered_map>

#include "base/callback.h"
#include "cobalt/base/tokens.h"
#include "cobalt/dom/event_target.h"
#include "cobalt/dom/window.h"
#include "cobalt/script/promise.h"
#include "cobalt/script/wrappable.h"
#include "starboard/window.h"

namespace cobalt {
namespace dom {

class Window;

class OnScreenKeyboard : public EventTarget {
 public:
  typedef script::ScriptValue<script::Promise<void>> VoidPromiseValue;

  typedef std::unordered_map<int,
                             std::unique_ptr<VoidPromiseValue::StrongReference>>
      TicketToPromiseMap;

  OnScreenKeyboard(const base::Callback<SbWindow()>& get_sb_window_callback,
                   script::ScriptValueFactory* script_value_factory);

  // Shows the on screen keyboard by calling a Starboard function
  // and dispatches an onshow event.
  scoped_ptr<VoidPromiseValue> Show();

  // Hides the on screen keyboard by calling a Starboard function,
  // and dispatches an onhide event.
  scoped_ptr<VoidPromiseValue> Hide();

  std::string data() const { return data_; }
  void set_data(const std::string& data) { data_ = data; }

  const EventListenerScriptValue* onshow() const;
  void set_onshow(const EventListenerScriptValue& event_listener);

  const EventListenerScriptValue* onhide() const;
  void set_onhide(const EventListenerScriptValue& event_listener);

  const EventListenerScriptValue* oninput() const;
  void set_oninput(const EventListenerScriptValue& event_listener);

  // If the keyboard is shown.
  bool shown() const;

  DEFINE_WRAPPABLE_TYPE(OnScreenKeyboard);

  // Called by the WebModule to dispatch DOM show and hide events.
  void DispatchHideEvent(int ticket);
  void DispatchShowEvent(int ticket);

 private:
  ~OnScreenKeyboard() override {}

  TicketToPromiseMap ticket_to_hide_promise_map_;
  TicketToPromiseMap ticket_to_show_promise_map_;

  const base::Callback<SbWindow()> get_sb_window_callback_;

  script::ScriptValueFactory* const script_value_factory_;

  std::string data_;

  int next_ticket_;

  DISALLOW_COPY_AND_ASSIGN(OnScreenKeyboard);
};
}  // namespace dom
}  // namespace cobalt

#endif  // COBALT_DOM_ON_SCREEN_KEYBOARD_H_
