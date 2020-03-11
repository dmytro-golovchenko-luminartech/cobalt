// Copyright 2015 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/common/mutex.h"

#include <pthread.h>

#include "starboard/common/log.h"
#include "starboard/configuration.h"
#include "starboard/shared/pthread/is_success.h"
#include "starboard/shared/pthread/types_internal.h"

bool SbMutexDestroy(SbMutex* mutex) {
  if (!mutex) {
    return false;
  }

#if SB_API_VERSION >= SB_MUTEX_ACQUIRE_TRY_API_CHANGE_VERSION
  // Both trying to recursively acquire a mutex that is locked by the calling
  // thread, as well as deleting a locked mutex, result in undefined behavior.
  return IsSuccess(pthread_mutex_destroy(SB_PTHREAD_INTERNAL_MUTEX(mutex)));
#else   // SB_API_VERSION >= SB_MUTEX_ACQUIRE_TRY_API_CHANGE_VERSION
  // Destroying a locked mutex is undefined, so fail if the mutex is
  // already locked,
  if (!IsSuccess(pthread_mutex_trylock(SB_PTHREAD_INTERNAL_MUTEX(mutex)))) {
    SB_LOG(ERROR) << "Trying to destroy a locked mutex";
    return false;
  }
  return IsSuccess(pthread_mutex_unlock(SB_PTHREAD_INTERNAL_MUTEX(mutex))) &&
         IsSuccess(pthread_mutex_destroy(SB_PTHREAD_INTERNAL_MUTEX(mutex)));
#endif  // SB_API_VERSION >= SB_MUTEX_ACQUIRE_TRY_API_CHANGE_VERSION
}
