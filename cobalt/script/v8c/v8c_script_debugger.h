// Copyright 2018 The Cobalt Authors. All Rights Reserved.
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
#ifndef COBALT_SCRIPT_V8C_V8C_SCRIPT_DEBUGGER_H_
#define COBALT_SCRIPT_V8C_V8C_SCRIPT_DEBUGGER_H_

#include <string>
#include <vector>

#include "base/logging.h"
#include "cobalt/script/script_debugger.h"
#include "cobalt/script/v8c/v8c_global_environment.h"
#include "v8/include/v8-inspector.h"

namespace cobalt {
namespace script {
namespace v8c {

class V8cScriptDebugger : public ScriptDebugger,
                          public v8_inspector::V8InspectorClient,
                          public v8_inspector::V8Inspector::Channel {
 public:
  V8cScriptDebugger(V8cGlobalEnvironment* v8c_global_environment,
                    Delegate* delegate);
  ~V8cScriptDebugger() override;

  // ScriptDebugger implementation.
  void Attach() override { attached_ = true; }
  void Detach() override { attached_ = false; }

  bool EvaluateDebuggerScript(const std::string& js_code,
                              std::string* out_result_utf8) override;

  bool CanDispatchProtocolMethod(const std::string& method) override;
  void DispatchProtocolMessage(const std::string& message) override;

  std::string CreateRemoteObject(const ValueHandleHolder& object,
                                 const std::string& group) override;

  void StartTracing(const std::vector<std::string>& categories,
                    TraceDelegate* trace_delegate) override;
  void StopTracing() override;

  PauseOnExceptionsState SetPauseOnExceptions(
      PauseOnExceptionsState state) override;

  // v8_inspector::V8InspectorClient implementation.
  void runMessageLoopOnPause(int contextGroupId) override;
  void quitMessageLoopOnPause() override;
  void runIfWaitingForDebugger(int contextGroupId) override;
  v8::Local<v8::Context> ensureDefaultContextInGroup(
      int contextGroupId) override;
  void consoleAPIMessage(int contextGroupId,
                         v8::Isolate::MessageErrorLevel level,
                         const v8_inspector::StringView& message,
                         const v8_inspector::StringView& url,
                         unsigned lineNumber, unsigned columnNumber,
                         v8_inspector::V8StackTrace*) override;

  // v8_inspector::V8Inspector::Channel implementation.
  void sendResponse(
      int callId, std::unique_ptr<v8_inspector::StringBuffer> message) override;
  void sendNotification(
      std::unique_ptr<v8_inspector::StringBuffer> message) override;
  void flushProtocolNotifications() override {}

 private:
  V8cGlobalEnvironment* global_environment_;
  Delegate* delegate_;
  std::unique_ptr<v8_inspector::V8Inspector> inspector_;
  std::unique_ptr<v8_inspector::V8InspectorSession> inspector_session_;
  PauseOnExceptionsState pause_on_exception_state_;
  bool attached_ = false;
};

}  // namespace v8c
}  // namespace script
}  // namespace cobalt

#endif  // COBALT_SCRIPT_V8C_V8C_SCRIPT_DEBUGGER_H_
