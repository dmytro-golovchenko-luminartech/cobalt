/*
 * Copyright 2016 Google Inc. All Rights Reserved.
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
#ifndef COBALT_SCRIPT_JAVASCRIPTCORE_THREAD_LOCAL_HASH_TABLE_H_
#define COBALT_SCRIPT_JAVASCRIPTCORE_THREAD_LOCAL_HASH_TABLE_H_

#include "base/memory/singleton.h"
#include "base/threading/thread_local_storage.h"
#include "third_party/WebKit/Source/JavaScriptCore/config.h"
#include "third_party/WebKit/Source/JavaScriptCore/runtime/ClassInfo.h"
#include "third_party/WebKit/Source/JavaScriptCore/runtime/Lookup.h"

namespace cobalt {
namespace script {
namespace javascriptcore {

class ThreadLocalHashTable {
 public:
  static ThreadLocalHashTable* GetInstance();

  JSC::HashTable* GetHashTable(const JSC::ClassInfo* class_info,
                               const JSC::HashTable& prototype);

 private:
  ThreadLocalHashTable();
  ~ThreadLocalHashTable();
  static void SlotDestructor(void* value);

  base::ThreadLocalStorage::Slot slot_;

  friend struct DefaultSingletonTraits<ThreadLocalHashTable>;
  DISALLOW_COPY_AND_ASSIGN(ThreadLocalHashTable);
};

}  // namespace javascriptcore
}  // namespace script
}  // namespace cobalt

#endif  // COBALT_SCRIPT_JAVASCRIPTCORE_THREAD_LOCAL_HASH_TABLE_H_
