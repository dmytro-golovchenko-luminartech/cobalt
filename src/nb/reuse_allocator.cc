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

#include "nb/reuse_allocator.h"

#include <algorithm>

#include "nb/pointer_arithmetic.h"
#include "starboard/log.h"
#include "starboard/types.h"

namespace nb {

ReuseAllocator::ReuseAllocator(Allocator* fallback_allocator) {
  fallback_allocator_ = fallback_allocator;
  capacity_ = 0;
  total_allocated_ = 0;
}

ReuseAllocator::~ReuseAllocator() {
  // Assert that everything was freed.
  // Note that in some unit tests this may
  // not be the case.
  if (allocated_blocks_.size() != 0) {
    SB_DLOG(ERROR) << allocated_blocks_.size() << " blocks still allocated.";
  }

  for (std::vector<void*>::iterator iter = fallback_allocations_.begin();
       iter != fallback_allocations_.end(); ++iter) {
    fallback_allocator_->Free(*iter);
  }
}

void ReuseAllocator::AddFreeBlock(void* address, std::size_t size) {
  MemoryBlock new_block;
  new_block.address = address;
  new_block.size = size;

  if (free_blocks_.size() == 0) {
    free_blocks_.insert(new_block);
    return;
  }

  // See if we can merge this block with one on the right or left.
  FreeBlockSet::iterator it = free_blocks_.lower_bound(new_block);
  // lower_bound will return an iterator to our neighbor on the right,
  // if one exists.
  FreeBlockSet::iterator right_to_erase = free_blocks_.end();
  FreeBlockSet::iterator left_to_erase = free_blocks_.end();

  if (it != free_blocks_.end()) {
    MemoryBlock right_block = *it;
    if (AsInteger(new_block.address) + new_block.size ==
        AsInteger(right_block.address)) {
      new_block.size += right_block.size;
      right_to_erase = it;
    }
  }

  // Now look to our left.
  if (it != free_blocks_.begin()) {
    it--;
    MemoryBlock left_block = *it;
    // Are we contiguous with the block to our left?
    if (AsInteger(left_block.address) + left_block.size ==
        AsInteger(new_block.address)) {
      new_block.address = left_block.address;
      new_block.size += left_block.size;
      left_to_erase = it;
    }
  }

  if (right_to_erase != free_blocks_.end()) {
    free_blocks_.erase(right_to_erase);
  }
  if (left_to_erase != free_blocks_.end()) {
    free_blocks_.erase(left_to_erase);
  }

  free_blocks_.insert(new_block);
}

void ReuseAllocator::RemoveFreeBlock(FreeBlockSet::iterator it) {
  free_blocks_.erase(it);
}

void* ReuseAllocator::Allocate(std::size_t size) {
  return Allocate(size, 1);
}

void* ReuseAllocator::AllocateForAlignment(std::size_t size,
                                           std::size_t alignment) {
  return Allocate(size, alignment);
}

void* ReuseAllocator::Allocate(std::size_t size, std::size_t alignment) {
  if (alignment == 0) {
    alignment = 1;
  }

  // Try to satisfy request from free list.
  // First look for a block that is appropriately aligned.
  // If we can't, look for a block that is big enough that we can
  // carve out an aligned block.
  // If there is no such block, allocate more from our fallback allocator.
  void* user_address = 0;

  // Keeping things rounded and aligned will help us
  // avoid creating tiny and/or badly misaligned free blocks.
  // Also ensure even for a 0-byte request we return a unique block.
  const std::size_t kMinBlockSizeBytes = 16;
  const std::size_t kMinAlignment = 16;
  size = std::max(size, kMinBlockSizeBytes);
  size = AlignUp(size, kMinBlockSizeBytes);
  alignment = AlignUp(alignment, kMinAlignment);

  // Worst case how much memory we need.
  MemoryBlock allocated_block;

  // Start looking through the free list.
  // If this is slow, we can store another map sorted by size.
  for (FreeBlockSet::iterator it = free_blocks_.begin();
       it != free_blocks_.end(); ++it) {
    MemoryBlock block = *it;
    const std::size_t extra_bytes_for_alignment =
        (alignment - AsInteger(block.address) % alignment) % alignment;
    const std::size_t aligned_size = size + extra_bytes_for_alignment;
    if (block.size >= aligned_size) {
      // The block is big enough.  We may waste some space due to alignment.
      RemoveFreeBlock(it);
      const std::size_t remaining_bytes = block.size - aligned_size;
      if (remaining_bytes >= kMinBlockSizeBytes) {
        AddFreeBlock(AsPointer(AsInteger(block.address) + aligned_size),
                     remaining_bytes);
        allocated_block.size = aligned_size;
      } else {
        allocated_block.size = block.size;
      }
      user_address = AlignUp(block.address, alignment);
      allocated_block.address = block.address;
      SB_DCHECK(allocated_block.size <= block.size);
      break;
    }
  }

  if (user_address == 0) {
    // No free blocks found.
    // Allocate one from the fallback allocator.
    size = AlignUp(size, alignment);
    void* ptr = fallback_allocator_->AllocateForAlignment(size, alignment);
    if (ptr == NULL) {
      return NULL;
    }
    uint8_t* memory_address = reinterpret_cast<uint8_t*>(ptr);
    user_address = AlignUp(memory_address, alignment);
    allocated_block.size = size;
    allocated_block.address = user_address;

    if (memory_address != user_address) {
      std::size_t alignment_padding_size =
          AsInteger(user_address) - AsInteger(memory_address);
      if (alignment_padding_size >= kMinBlockSizeBytes) {
        // Register the memory range skipped for alignment as a free block for
        // later use.
        AddFreeBlock(memory_address, alignment_padding_size);
        capacity_ += alignment_padding_size;
      } else {
        // The memory range skipped for alignment is too small for a free block.
        // Adjust the allocated block to include the alignment padding.
        allocated_block.size += alignment_padding_size;
        allocated_block.address = AsPointer(AsInteger(allocated_block.address) -
                                            alignment_padding_size);
      }
    }

    capacity_ += allocated_block.size;
    fallback_allocations_.push_back(ptr);
  }
  SB_DCHECK(allocated_blocks_.find(user_address) == allocated_blocks_.end());
  allocated_blocks_[user_address] = allocated_block;
  total_allocated_ += allocated_block.size;
  return user_address;
}

void ReuseAllocator::Free(void* memory) {
  if (!memory) {
    return;
  }

  AllocatedBlockMap::iterator it = allocated_blocks_.find(memory);
  SB_DCHECK(it != allocated_blocks_.end());

  // Mark this block as free and remove it from the allocated set.
  const MemoryBlock& block = (*it).second;
  AddFreeBlock(block.address, block.size);

  SB_DCHECK(block.size <= total_allocated_);
  total_allocated_ -= block.size;

  allocated_blocks_.erase(it);
}

void ReuseAllocator::PrintAllocations() const {
  typedef std::map<std::size_t, std::size_t> SizesHistogram;
  SizesHistogram sizes_histogram;
  for (AllocatedBlockMap::const_iterator iter = allocated_blocks_.begin();
       iter != allocated_blocks_.end(); ++iter) {
    std::size_t block_size = iter->second.size;
    if (sizes_histogram.find(block_size) == sizes_histogram.end()) {
      sizes_histogram[block_size] = 0;
    }
    sizes_histogram[block_size] = sizes_histogram[block_size] + 1;
  }

  for (SizesHistogram::const_iterator iter = sizes_histogram.begin();
       iter != sizes_histogram.end(); ++iter) {
    SB_LOG(INFO) << iter->first << " : " << iter->second;
  }
  SB_LOG(INFO) << "Total allocations: " << allocated_blocks_.size();
}

}  // namespace nb
