/*
 * Copyright 2014 Google Inc. All Rights Reserved.
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

#include "nb/memory_pool.h"

#include "starboard/log.h"

namespace nb {

MemoryPool::MemoryPool(void* buffer,
                       std::size_t size,
                       bool verify_full_capacity,
                       std::size_t small_allocation_threshold)
    : no_free_allocator_(buffer, size),
      reuse_allocator_(&no_free_allocator_, size, small_allocation_threshold) {
  SB_DCHECK(buffer);
  SB_DCHECK(size != 0U);

  // This is redundant if ReuseAllocator::Allcator() can allocate the difference
  // between the requested size and the last free block from the fallback
  // allocator and combine the blocks.
  if (verify_full_capacity && small_allocation_threshold == 0) {
    void* p = Allocate(size);
    SB_DCHECK(p);
    Free(p);
  }
}

void MemoryPool::PrintAllocations() const {
  reuse_allocator_.PrintAllocations();
}

}  // namespace nb
