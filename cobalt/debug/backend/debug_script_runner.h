// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#ifndef COBALT_DEBUG_BACKEND_DEBUG_SCRIPT_RUNNER_H_
#define COBALT_DEBUG_BACKEND_DEBUG_SCRIPT_RUNNER_H_

#include <string>

#include "base/callback.h"
#include "base/optional.h"
#include "cobalt/dom/csp_delegate.h"
#include "cobalt/script/callback_function.h"
#include "cobalt/script/global_environment.h"
#include "cobalt/script/script_debugger.h"
#include "cobalt/script/script_value.h"
#include "cobalt/script/value_handle.h"
#include "cobalt/script/wrappable.h"

namespace cobalt {
namespace debug {
namespace backend {

// Used by the various debugger agents to run JavaScript and persist state. An
// object of this class creates a persistent JavaScript object bound to the
// global object, and executes methods on this object, passing in the JSON
// parameters as a parameter object, and returning the result as a serialized
// JSON object. Other classes may run scripts that attach additional data to the
// JavaScript object created by this class.
class DebugScriptRunner : public script::Wrappable {
 public:
  // Event callback. A callback of this type is specified in the constructor,
  // and used to send asynchronous debugging events that are not a direct
  // response to a command.
  // See: https://chromedevtools.github.io/devtools-protocol/
  typedef base::Callback<void(const std::string& method,
                              const base::optional<std::string>& params)>
      OnEventCallback;

  DebugScriptRunner(script::GlobalEnvironment* global_environment,
                    script::ScriptDebugger* script_debugger,
                    const dom::CspDelegate* csp_delegate,
                    const OnEventCallback& on_event_callback);

  // Runs |method| on the JavaScript |devtoolsBackend| object, passing in
  // |json_params|. If |json_result| is non-NULL it receives the result.
  // Returns |true| if the method was executed; |json_result| is the value
  // returned by the method.
  // Returns |false| if the method wasn't executed; if the method isn't defined
  // |json_result| is empty, otherwise it's an error message.
  bool RunCommand(const std::string& method, const std::string& json_params,
                  std::string* json_result);

  // Loads JavaScript from file and executes the contents. Used to add
  // functionality to the JS object wrapped by this class.
  bool RunScriptFile(const std::string& filename);

  // IDL: Sends a protocol event to the debugger frontend.
  void SendEvent(const std::string& method,
                 const base::optional<std::string>& params);

  // IDL: Returns the RemoteObject JSON representation of the given object for
  // the debugger frontend.
  // https://chromedevtools.github.io/devtools-protocol/1-3/Runtime#type-RemoteObject
  std::string CreateRemoteObject(const script::ValueHandleHolder& object,
                                 const std::string& group);

  DEFINE_WRAPPABLE_TYPE(DebugScriptRunner);

 private:
  bool EvaluateDebuggerScript(const std::string& script,
                              std::string* out_result_utf8);

  // Ensures the JS eval command is enabled, overriding CSP if necessary.
  void ForceEnableEval();
  // Enables/disables eval according to CSP.
  void SetEvalAllowedFromCsp();

  // No ownership.
  script::GlobalEnvironment* global_environment_;

  // Engine-specific debugger implementation.
  script::ScriptDebugger* script_debugger_;

  // Non-owned reference to let this object query whether CSP allows eval.
  const dom::CspDelegate* csp_delegate_;

  // Callback to send events.
  OnEventCallback on_event_callback_;
};

}  // namespace backend
}  // namespace debug
}  // namespace cobalt

#endif  // COBALT_DEBUG_BACKEND_DEBUG_SCRIPT_RUNNER_H_
