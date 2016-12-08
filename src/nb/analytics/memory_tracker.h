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

#ifndef NB_MEMORY_TRACKER_H_
#define NB_MEMORY_TRACKER_H_

#include <vector>
#include "starboard/configuration.h"
#include "starboard/types.h"
#include "nb/scoped_ptr.h"

namespace nb {

namespace analytics {

class MemoryTracker;
class MemoryTrackerPrintThread;
class MemoryTrackerPrintCSVThread;
class AllocationVisitor;
class AllocationGroup;
class AllocationRecord;

struct MemoryStats {
  MemoryStats() : total_cpu_memory(0), used_cpu_memory(0),
                  total_gpu_memory(0), used_gpu_memory(0) {}
  int64_t total_cpu_memory;
  int64_t used_cpu_memory;
  int64_t total_gpu_memory;
  int64_t used_gpu_memory;
};

MemoryStats GetProcessMemoryStats();

// Creates a MemoryTracker instance that implements the
//  MemoryTracker. Once the instance is created it can begin tracking
//  system allocations by calling InstallGlobalTrackingHooks().
//  Deleting the MemoryTracker is forbidden.
//
// Example, Creation and Hooking:
//   static MemoryTracker* s_global_tracker =
//       GetOrCreateMemoryTracker();
//   s_global_tracker->InstallGlobalTrackingHooks();  // now tracking memory.
//
// Data about the allocations are aggregated under AllocationGroups and it's
//  recommended that GetAllocationGroups(...) is used to get simple allocation
//  statistics.
//
// Deeper analytics are possible by creating an AllocationVisitor subclass and
//  traversing through the internal allocations of the tracker. In this way all
//  known information about allocation state of the program is made accessible.
//  The visitor does not need to perform any locking as this is guaranteed by
//  the MemoryTracker.
//
// Example (AllocationVisitor):
//  MyAllocation visitor = ...;
//  s_global_tracker->Accept(&visitor);
//  visitor.PrintAllocations();
//
// Performance:
//  1) Gold builds disallow memory tracking and therefore have zero-cost
//     for this feature.
//  2) All other builds that allow memory tracking have minimal cost as long
//     as memory tracking has not been activated. This is facilitated by NOT
//     using locks, at the expense of thread safety during teardown (hence the
//     reason why you should NOT delete a memory tracker with hooks installed).
//  3) When the memory tracking has been activated then there is a non-trivial
//     performance cost in terms of CPU and memory for the feature.
class MemoryTracker {
 public:
  // Gets the singleton instance of the default MemoryTracker. This
  // is created the first time it is used.
  static MemoryTracker* Get();

  MemoryTracker() {}
  virtual bool InstallGlobalTrackingHooks() = 0;

  // It's recommended the MemoryTracker is never removed or deleted during the
  // runtime.
  virtual void RemoveGlobalTrackingHooks() = 0;

  // Returns the total amount of bytes that are tracked.
  virtual int64_t GetTotalAllocationBytes() = 0;
  virtual int64_t GetTotalNumberOfAllocations() = 0;

  // Allows probing of all memory allocations. The visitor does not need to
  // perform any locking and can allocate memory during it's operation.
  virtual void Accept(AllocationVisitor* visitor) = 0;

  // Collects all memory groups that exist. The AllocationGroups lifetime
  // exists for as long as the MemoryTracker instance is alive.
  virtual void GetAllocationGroups(
      std::vector<const AllocationGroup*>* output) = 0;

  // Enabled/disables memory tracking in the current thread.
  virtual void SetMemoryTrackingEnabled(bool on) = 0;
  // Returns the memory tracking state in the current thread.
  virtual bool IsMemoryTrackingEnabled() const = 0;

  // Returns true if the memory was successfully tracked.
  virtual bool AddMemoryTracking(const void* memory, size_t size) = 0;
  // Returns a non-zero size if the memory was successfully removed.
  virtual size_t RemoveMemoryTracking(const void* memory) = 0;
  // Returns true if the memory has tracking. When true is returned then the
  // supplied AllocRecord is written.
  virtual bool GetMemoryTracking(const void* memory,
                                 AllocationRecord* record) const = 0;

 protected:
  virtual ~MemoryTracker() {}

  SB_DISALLOW_COPY_AND_ASSIGN(MemoryTracker);
};

// A visitor class which is useful for inspecting data.
class AllocationVisitor {
 public:
  // Returns true to keep visiting, otherwise abort.
  virtual bool Visit(const void* memory,
                     const AllocationRecord& alloc_record) = 0;
  virtual ~AllocationVisitor() {}
};

// Contains an allocation record for a pointer including it's size and what
// AllocationGroup it was constructed under.
class AllocationRecord {
 public:
  AllocationRecord() : size(0), allocation_group(NULL) {}
  AllocationRecord(size_t sz, AllocationGroup* group)
      : size(sz), allocation_group(group) {}

  static AllocationRecord Empty() { return AllocationRecord(); }
  bool IsEmpty() const { return !size && !allocation_group; }
  size_t size;
  AllocationGroup* allocation_group;
};

// Creates a SimpleThread that will output the state of the memory
// periodically. Start()/Cancel()/Join() are called AUTOMATICALLY with
// this object. Start() is on the returned thread before it is returned.
// Join() is automatically called on destruction.
scoped_ptr<MemoryTrackerPrintThread>
    CreateDebugPrintThread(MemoryTracker* memory_tracker);

// Creates a SimpleThread that will output the state of the memory
// periodically. Start()/Cancel()/Join() are called AUTOMATICALLY with
// this object. Start() is on the returned thread before it is returned.
// Join() is automatically called on destruction.
scoped_ptr<MemoryTrackerPrintCSVThread>
    CreateDebugPrintCSVThread(MemoryTracker* memory_tracker,
                              int sample_interval_ms,
                              int total_sampling_time_ms);

}  // namespace analytics
}  // namespace nb

#endif  // NB_MEMORY_TRACKER_H_
