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

#ifndef COBALT_SCRIPT_V8C_V8C_HEAP_TRACER_H_
#define COBALT_SCRIPT_V8C_V8C_HEAP_TRACER_H_

#include <unordered_set>
#include <utility>
#include <vector>

#include "cobalt/script/wrappable.h"
#include "v8/include/v8.h"
#include "v8/include/v8-platform.h"

namespace cobalt {
namespace script {
namespace v8c {

// We need to re-forward declare this because |V8cEngine| needs us to be
// defined to have us as a member inside of a |scoped_ptr|.
v8::Platform* GetPlatform();

class V8cHeapTracer final : public v8::EmbedderHeapTracer,
                            public ::cobalt::script::Tracer {
 public:
  explicit V8cHeapTracer(v8::Isolate* isolate) : isolate_(isolate) {}

  void RegisterV8References(
      const std::vector<std::pair<void*, void*>>& embedder_fields) override;
  void TracePrologue() override {}
  bool AdvanceTracing(double deadline_in_ms,
                      AdvanceTracingActions actions) override;
  void TraceEpilogue() override {
    DCHECK(frontier_.empty());
    visited_.clear();
  }
  void EnterFinalPause() override {}
  void AbortTracing() override {
    LOG(WARNING) << "Tracing aborted.";
    frontier_.clear();
    visited_.clear();
  }
  size_t NumberOfWrappersToTrace() override { return frontier_.size(); }

  void Trace(Traceable* traceable) override;

 private:
  v8::Isolate* const isolate_;
  v8::Platform* const platform_ = GetPlatform();
  std::vector<Traceable*> frontier_;
  std::unordered_set<Traceable*> visited_;
};

}  // namespace v8c
}  // namespace script
}  // namespace cobalt

#endif  // COBALT_SCRIPT_V8C_V8C_HEAP_TRACER_H_
