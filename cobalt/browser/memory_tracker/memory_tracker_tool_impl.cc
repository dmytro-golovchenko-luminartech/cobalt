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

#include "cobalt/browser/memory_tracker/memory_tracker_tool_impl.h"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "base/time.h"
#include "cobalt/browser/memory_tracker/buffered_file_writer.h"
#include "nb/analytics/memory_tracker.h"
#include "nb/analytics/memory_tracker_helpers.h"
#include "starboard/configuration.h"
#include "starboard/file.h"
#include "starboard/string.h"
#include "starboard/system.h"

namespace cobalt {
namespace browser {
namespace memory_tracker {

using nb::analytics::AllocationGroup;
using nb::analytics::AllocationRecord;
using nb::analytics::AllocationVisitor;
using nb::analytics::GetProcessMemoryStats;
using nb::analytics::MemoryStats;
using nb::analytics::MemoryTracker;

namespace {
const char kQuote[] = "\"";
const char kDelimiter[] = ",";
const char kNewLine[] = "\n";

// This is a simple algorithm to remove the "needle" from the haystack. Note
// that this function is simple and not well optimized.
std::string RemoveString(const std::string& haystack, const char* needle) {
  const size_t kNotFound = std::string::npos;

  // Base case. No modification needed.
  size_t pos = haystack.find(needle);
  if (pos == kNotFound) {
    return haystack;
  }
  const size_t n = strlen(needle);
  std::string output;
  output.reserve(haystack.size());

  // Copy string, omitting the portion containing the "needle".
  std::copy(haystack.begin(), haystack.begin() + pos,
            std::back_inserter(output));
  std::copy(haystack.begin() + pos + n, haystack.end(),
            std::back_inserter(output));

  // Recursively remove same needle in haystack.
  return RemoveString(output, needle);
}

// Not optimized but works ok for a tool that dumps out in user time.
std::string SanitizeCSVKey(std::string key) {
  key = RemoveString(key, kQuote);
  key = RemoveString(key, kDelimiter);
  key = RemoveString(key, kNewLine);
  return key;
}

// Converts "2345.54" => "2,345.54".
std::string InsertCommasIntoNumberString(const std::string& input) {
  typedef std::vector<char> CharVector;
  typedef CharVector::iterator CharIt;

  CharVector chars(input.begin(), input.end());
  std::reverse(chars.begin(), chars.end());

  CharIt curr_it = chars.begin();
  CharIt mid = std::find(chars.begin(), chars.end(), '.');
  if (mid == chars.end()) {
    mid = curr_it;
  }

  CharVector out(curr_it, mid);

  int counter = 0;
  for (CharIt it = mid; it != chars.end(); ++it) {
    if (counter != 0 && (counter % 3 == 0)) {
      out.push_back(',');
    }
    if (*it != '.') {
      counter++;
    }
    out.push_back(*it);
  }

  std::reverse(out.begin(), out.end());
  std::stringstream ss;
  for (size_t i = 0; i < out.size(); ++i) {
    ss << out[i];
  }
  return ss.str();
}

template <typename T>
std::string NumberFormatWithCommas(T val) {
  // Convert value to string.
  std::stringstream ss;
  ss << val;
  std::string s = InsertCommasIntoNumberString(ss.str());
  return s;
}

// NoMemoryTracking will disable memory tracking while in the current scope of
// execution. When the object is destroyed it will reset the previous state
// of allocation tracking.
// Example:
//   void Foo() {
//     NoMemoryTracking no_memory_tracking_in_scope;
//     int* ptr = new int();  // ptr is not tracked.
//     delete ptr;
//     return;    // Previous memory tracking state is restored.
//   }
class NoMemoryTracking {
 public:
  explicit NoMemoryTracking(nb::analytics::MemoryTracker* owner);
  ~NoMemoryTracking();

 private:
  bool prev_val_;
  nb::analytics::MemoryTracker* owner_;
};

// Simple timer class that fires once after dt time has elapsed.
class Timer {
 public:
  explicit Timer(base::TimeDelta dt)
      : start_time_(base::Time::NowFromSystemTime()),
        time_before_expiration_(dt) {}

  bool UpdateAndIsExpired() {
    base::Time now_time = base::Time::NowFromSystemTime();
    base::TimeDelta dt = now_time - start_time_;
    if (dt > time_before_expiration_) {
      start_time_ = now_time;
      return true;
    } else {
      return false;
    }
  }

 private:
  base::Time start_time_;
  base::TimeDelta time_before_expiration_;
};

}  // namespace

class Params {
 public:
  Params(nb::analytics::MemoryTracker* memory_tracker, AbstractLogger* logger,
         base::Time start_time)
      : memory_tracker_(memory_tracker),
        finished_(false),
        logger_(logger),
        timer_(start_time) {}
  bool finished() const { return finished_; }
  void set_finished(bool val) { finished_ = val; }

  nb::analytics::MemoryTracker* memory_tracker() const {
    return memory_tracker_;
  }
  AbstractLogger* logger() { return logger_.get(); }
  base::TimeDelta time_since_start() const {
    return base::Time::NowFromSystemTime() - timer_;
  }
  std::string TimeInMinutesString() const {
    base::TimeDelta delta_t = time_since_start();
    int64 seconds = delta_t.InSeconds();
    float time_mins = static_cast<float>(seconds) / 60.f;
    std::stringstream ss;

    ss << time_mins;
    return ss.str();
  }

 private:
  nb::analytics::MemoryTracker* memory_tracker_;
  bool finished_;
  scoped_ptr<AbstractLogger> logger_;
  base::Time timer_;
};

MemoryTrackerToolThread::MemoryTrackerToolThread(
    nb::analytics::MemoryTracker* memory_tracker,
    AbstractMemoryTrackerTool* tool, AbstractLogger* logger)
    : Super(tool->tool_name()),
      params_(
          new Params(memory_tracker, logger, base::Time::NowFromSystemTime())),
      tool_(tool) {
  Start();
}

MemoryTrackerToolThread::~MemoryTrackerToolThread() {
  Join();
  tool_.reset();
  params_.reset();
}

void MemoryTrackerToolThread::Join() {
  params_->set_finished(true);
  Super::Join();
}

void MemoryTrackerToolThread::Run() {
  NoMemoryTracking no_mem_tracking_in_this_scope(params_->memory_tracker());
  // This tool will run until the finished_ if flipped to false.
  tool_->Run(params_.get());
}

NoMemoryTracking::NoMemoryTracking(nb::analytics::MemoryTracker* owner)
    : owner_(owner) {
  if (owner_) {
    prev_val_ = owner_->IsMemoryTrackingEnabled();
    owner_->SetMemoryTrackingEnabled(false);
  }
}

NoMemoryTracking::~NoMemoryTracking() {
  if (owner_) {
    owner_->SetMemoryTrackingEnabled(prev_val_);
  }
}

void MemoryTrackerPrint::Run(Params* params) {
  const std::string kSeperator
      = "--------------------------------------------------";

  while (!params->finished()) {
    std::vector<const AllocationGroup*> vector_output;
    params->memory_tracker()->GetAllocationGroups(&vector_output);

    typedef std::map<std::string, const AllocationGroup*> Map;
    typedef Map::const_iterator MapIt;

    Map output;
    for (size_t i = 0; i < vector_output.size(); ++i) {
      const AllocationGroup* group = vector_output[i];
      output[group->name()] = group;
    }

    int32 num_allocs = 0;
    int64 total_bytes = 0;

    struct F {
      static void PrintRow(std::stringstream* ss, const std::string& v1,
                           const std::string& v2, const std::string& v3) {
        ss->width(25);
        *ss << std::left << v1;
        ss->width(13);
        *ss << std::right << v2 << "  ";
        ss->width(10);
        *ss << std::right << v3 << "\n";
      }
    };

    if (params->memory_tracker()->IsMemoryTrackingEnabled()) {
      // If this isn't true then it would cause an infinite loop. The
      // following will likely crash.
      SB_DCHECK(false) << "Unexpected, memory tracking should be disabled.";
    }

    std::stringstream ss;

    ss << kNewLine;
    ss << "TimeNow " << params->TimeInMinutesString()
       << " (minutes):" << kNewLine << kNewLine;

    ss << kSeperator << kNewLine;
    MemoryStats memstats = GetProcessMemoryStats();

    F::PrintRow(&ss, "MALLOC STAT", "IN USE BYTES", "");
    ss << kSeperator << kNewLine;
    F::PrintRow(&ss,
                "Total CPU Reserved",
                NumberFormatWithCommas(memstats.total_cpu_memory),
                "");

    F::PrintRow(&ss,
                "Total CPU Used",
                NumberFormatWithCommas(memstats.used_cpu_memory),
                "");

    F::PrintRow(&ss,
                "Total GPU Reserved",
                NumberFormatWithCommas(memstats.total_gpu_memory),
                "");

    F::PrintRow(&ss,
                "Total GPU Used",
                NumberFormatWithCommas(memstats.used_gpu_memory),
                "");

    ss << kSeperator << kNewLine << kNewLine;

    ss << kSeperator << kNewLine;
    F::PrintRow(&ss, "MEMORY REGION", "IN USE BYTES", "NUM ALLOCS");
    ss << kSeperator << kNewLine;

    for (MapIt it = output.begin(); it != output.end(); ++it) {
      const AllocationGroup* group = it->second;
      if (!group) {
        continue;
      }

      int32 num_group_allocs = -1;
      int64 total_group_bytes = -1;

      group->GetAggregateStats(&num_group_allocs, &total_group_bytes);
      SB_DCHECK(-1 != num_group_allocs);
      SB_DCHECK(-1 != total_group_bytes);
      num_allocs += num_group_allocs;
      total_bytes += total_group_bytes;

      F::PrintRow(&ss, it->first, NumberFormatWithCommas(total_group_bytes),
                  NumberFormatWithCommas(num_group_allocs));
    }

    ss << kNewLine;

    F::PrintRow(&ss,
                "Total (in groups above)",
                NumberFormatWithCommas(total_bytes),
                NumberFormatWithCommas(num_allocs));

    ss << kSeperator << kNewLine;
    ss << kNewLine << kNewLine;

    params->logger()->Output(ss.str().c_str());
    // Output once every 5 seconds.
    base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(5));
  }
}

MemoryTrackerPrintCSV::MemoryTrackerPrintCSV(int sampling_interval_ms,
                                             int sampling_time_ms)
    : sample_interval_ms_(sampling_interval_ms),
      sampling_time_ms_(sampling_time_ms) {}

std::string MemoryTrackerPrintCSV::ToCsvString(
    const MapAllocationSamples& samples_in) {
  typedef MapAllocationSamples Map;
  typedef Map::const_iterator MapIt;

  size_t largest_sample_size = 0;
  size_t smallest_sample_size = INT_MAX;

  // Sanitize samples_in and store as samples.
  MapAllocationSamples samples;
  for (MapIt it = samples_in.begin(); it != samples_in.end(); ++it) {
    std::string name = it->first;
    const AllocationSamples& value = it->second;

    if (value.allocated_bytes_.size() != value.number_allocations_.size()) {
      SB_NOTREACHED() << "Error at " << __FILE__ << ":" << __LINE__;
      return "ERROR";
    }

    const size_t n = value.allocated_bytes_.size();
    if (n > largest_sample_size) {
      largest_sample_size = n;
    }
    if (n < smallest_sample_size) {
      smallest_sample_size = n;
    }

    const bool duplicate_found = (samples.end() != samples.find(name));
    if (duplicate_found) {
      SB_NOTREACHED() << "Error, duplicate found for entry: " << name
                      << kNewLine;
    }
    // Store value as a sanitized sample.
    samples[name] = value;
  }

  SB_DCHECK(largest_sample_size == smallest_sample_size);

  std::stringstream ss;

  // Begin output to CSV.
  // Sometimes we need to skip the CPU memory entry.
  const MapIt total_cpu_memory_it = samples.find(UntrackedMemoryKey());

  // Preamble
  ss << kNewLine << "//////////////////////////////////////////////";
  ss << kNewLine << "// CSV of bytes / allocation" << kNewLine;
  // HEADER.
  ss << "Name" << kDelimiter << kQuote << "Bytes/Alloc" << kQuote << kNewLine;
  // DATA.
  for (MapIt it = samples.begin(); it != samples.end(); ++it) {
    if (total_cpu_memory_it == it) {
      continue;
    }

    const AllocationSamples& samples = it->second;
    if (samples.allocated_bytes_.empty() ||
        samples.number_allocations_.empty()) {
      SB_NOTREACHED() << "Should not be here";
      return "ERROR";
    }
    const int64 n_allocs = samples.number_allocations_.back();
    const int64 n_bytes = samples.allocated_bytes_.back();
    int64 bytes_per_alloc = 0;
    if (n_allocs > 0) {
      bytes_per_alloc = n_bytes / n_allocs;
    }
    const std::string& name = it->first;
    ss << kQuote << SanitizeCSVKey(name) << kQuote << kDelimiter
       << bytes_per_alloc << kNewLine;
  }
  ss << kNewLine;

  // Preamble
  ss << kNewLine << "//////////////////////////////////////////////" << kNewLine
     << "// CSV of bytes allocated per region (MB's)." << kNewLine
     << "// Units are in Megabytes. This is designed" << kNewLine
     << "// to be used in a stacked graph." << kNewLine;

  // HEADER.
  for (MapIt it = samples.begin(); it != samples.end(); ++it) {
    if (total_cpu_memory_it == it) {
      continue;
    }
    // Strip out any characters that could make parsing the csv difficult.
    const std::string name = SanitizeCSVKey(it->first);
    ss << kQuote << name << kQuote << kDelimiter;
  }
  // Save the total for last.
  if (total_cpu_memory_it != samples.end()) {
    const std::string& name = SanitizeCSVKey(total_cpu_memory_it->first);
    ss << kQuote << name << kQuote << kDelimiter;
  }
  ss << kNewLine;

  // Print out the values of each of the samples.
  for (size_t i = 0; i < smallest_sample_size; ++i) {
    for (MapIt it = samples.begin(); it != samples.end(); ++it) {
      if (total_cpu_memory_it == it) {
        continue;
      }
      const int64 alloc_bytes = it->second.allocated_bytes_[i];
      // Convert to float megabytes with decimals of precision.
      double n = alloc_bytes / (1000 * 10);
      n = n / (100.);
      ss << n << kDelimiter;
    }
    if (total_cpu_memory_it != samples.end()) {
      const int64 alloc_bytes = total_cpu_memory_it->second.allocated_bytes_[i];
      // Convert to float megabytes with decimals of precision.
      double n = alloc_bytes / (1000 * 10);
      n = n / (100.);
      ss << n << kDelimiter;
    }
    ss << kNewLine;
  }

  ss << kNewLine;
  // Preamble
  ss << kNewLine << "//////////////////////////////////////////////";
  ss << kNewLine << "// CSV of number of allocations per region." << kNewLine;

  // HEADER
  for (MapIt it = samples.begin(); it != samples.end(); ++it) {
    if (total_cpu_memory_it == it) {
      continue;
    }
    const std::string& name = SanitizeCSVKey(it->first);
    ss << kQuote << name << kQuote << kDelimiter;
  }
  ss << kNewLine;
  for (size_t i = 0; i < smallest_sample_size; ++i) {
    for (MapIt it = samples.begin(); it != samples.end(); ++it) {
      if (total_cpu_memory_it == it) {
        continue;
      }
      const int64 n_allocs = it->second.number_allocations_[i];
      ss << n_allocs << kDelimiter;
    }
    ss << kNewLine;
  }
  std::string output = ss.str();
  return output;
}

const char* MemoryTrackerPrintCSV::UntrackedMemoryKey() {
  return "Untracked Memory";
}

void MemoryTrackerPrintCSV::Run(Params* params) {
  params->logger()->Output("\nMemoryTrackerPrintCSVThread is sampling...\n");
  int sample_count = 0;
  MapAllocationSamples map_samples;

  while (!TimeExpiredYet(*params) && !params->finished()) {
    // Sample total memory used by the system.
    MemoryStats mem_stats = GetProcessMemoryStats();
    int64 untracked_used_memory =
        mem_stats.used_cpu_memory + mem_stats.used_gpu_memory;

    std::vector<const AllocationGroup*> vector_output;
    params->memory_tracker()->GetAllocationGroups(&vector_output);

    // Sample all known memory scopes.
    for (size_t i = 0; i < vector_output.size(); ++i) {
      const AllocationGroup* group = vector_output[i];
      const std::string& name = group->name();

      const bool first_creation =
          map_samples.find(group->name()) == map_samples.end();

      AllocationSamples* new_entry = &(map_samples[name]);

      // Didn't see it before so create new entry.
      if (first_creation) {
        // Make up for lost samples...
        new_entry->allocated_bytes_.resize(sample_count, 0);
        new_entry->number_allocations_.resize(sample_count, 0);
      }

      int32 num_allocs = -1;
      int64 allocation_bytes = -1;
      group->GetAggregateStats(&num_allocs, &allocation_bytes);

      new_entry->allocated_bytes_.push_back(allocation_bytes);
      new_entry->number_allocations_.push_back(num_allocs);

      untracked_used_memory -= allocation_bytes;
    }

    // Now push in remaining total.
    AllocationSamples* process_sample = &(map_samples[UntrackedMemoryKey()]);
    if (untracked_used_memory < 0) {
      // On some platforms, total GPU memory may not be correctly reported.
      // However the allocations from the GPU memory may be reported. In this
      // case untracked_used_memory will go negative. To protect the memory
      // reporting the untracked_used_memory is set to 0 so that it doesn't
      // cause an error in reporting.
      untracked_used_memory = 0;
    }
    process_sample->allocated_bytes_.push_back(untracked_used_memory);
    process_sample->number_allocations_.push_back(-1);

    ++sample_count;
    base::PlatformThread::Sleep(
        base::TimeDelta::FromMilliseconds(sample_interval_ms_));
  }

  std::stringstream ss;
  ss.precision(2);
  ss << "Time now: " << params->TimeInMinutesString() << ",\n";
  ss << ToCsvString(map_samples);
  params->logger()->Output(ss.str().c_str());
  params->logger()->Flush();
  // Prevents the "thread exited code 0" from being interleaved into the
  // output. This happens if flush is not implemented correctly in the system.
  base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(1));
}

bool MemoryTrackerPrintCSV::TimeExpiredYet(const Params& params) {
  base::TimeDelta dt = params.time_since_start();
  int64 dt_ms = dt.InMilliseconds();
  const bool expired_time = dt_ms > sampling_time_ms_;
  return expired_time;
}

///////////////////////////////////////////////////////////////////////////////
MemoryTrackerCompressedTimeSeries::MemoryTrackerCompressedTimeSeries()
    : sample_interval_ms_(100), number_samples_(400) {}

void MemoryTrackerCompressedTimeSeries::Run(Params* params) {
  TimeSeries timeseries;
  Timer timer(base::TimeDelta::FromSeconds(2));
  while (!params->finished()) {
    AcquireSample(params->memory_tracker(), &timeseries,
                  params->time_since_start());
    if (IsFull(timeseries, number_samples_)) {
      const std::string str = ToCsvString(timeseries);
      Compress(&timeseries);
      params->logger()->Output(str.c_str());
    } else if (timer.UpdateAndIsExpired()) {
      std::stringstream ss;
      ss << tool_name() << " is running..." << kNewLine;
      params->logger()->Output(ss.str().c_str());
    }
    base::PlatformThread::Sleep(
        base::TimeDelta::FromMilliseconds(sample_interval_ms_));
  }
}

std::string MemoryTrackerCompressedTimeSeries::ToCsvString(
    const TimeSeries& timeseries) {
  size_t largest_sample_size = 0;
  size_t smallest_sample_size = INT_MAX;

  typedef MapAllocationSamples::const_iterator MapIt;

  // Sanitize samples_in and store as samples.
  const MapAllocationSamples& samples_in = timeseries.samples_;
  MapAllocationSamples samples;
  for (MapIt it = samples_in.begin(); it != samples_in.end(); ++it) {
    std::string name = it->first;
    const AllocationSamples& value = it->second;

    if (value.allocated_bytes_.size() != value.number_allocations_.size()) {
      SB_NOTREACHED() << "Error at " << __FILE__ << ":" << __LINE__;
      return "ERROR";
    }

    const size_t n = value.allocated_bytes_.size();
    if (n > largest_sample_size) {
      largest_sample_size = n;
    }
    if (n < smallest_sample_size) {
      smallest_sample_size = n;
    }

    const bool duplicate_found = (samples.end() != samples.find(name));
    if (duplicate_found) {
      SB_NOTREACHED() << "Error, duplicate found for entry: " << name
                      << kNewLine;
    }
    // Store value as a sanitized sample.
    samples[name] = value;
  }

  SB_DCHECK(largest_sample_size == smallest_sample_size);

  std::stringstream ss;

  // Begin output to CSV.

  // Preamble
  ss << kNewLine << "//////////////////////////////////////////////" << kNewLine
     << "// CSV of bytes allocated per region (MB's)." << kNewLine
     << "// Units are in Megabytes. This is designed" << kNewLine
     << "// to be used in a stacked graph." << kNewLine;

  // HEADER.
  for (MapIt it = samples.begin(); it != samples.end(); ++it) {
    const std::string& name = it->first;
    ss << kQuote << SanitizeCSVKey(name) << kQuote << kDelimiter;
  }
  ss << kNewLine;

  // Print out the values of each of the samples.
  for (size_t i = 0; i < smallest_sample_size; ++i) {
    for (MapIt it = samples.begin(); it != samples.end(); ++it) {
      const int64 alloc_bytes = it->second.allocated_bytes_[i];
      // Convert to float megabytes with decimals of precision.
      double n = alloc_bytes / (1000 * 10);
      n = n / (100.);
      ss << n << kDelimiter;
    }
    ss << kNewLine;
  }

  ss << kNewLine;
  // Preamble
  ss << kNewLine << "//////////////////////////////////////////////";
  ss << kNewLine << "// CSV of number of allocations per region." << kNewLine;

  // HEADER
  for (MapIt it = samples.begin(); it != samples.end(); ++it) {
    const std::string& name = it->first;
    ss << kQuote << SanitizeCSVKey(name) << kQuote << kDelimiter;
  }
  ss << kNewLine;
  for (size_t i = 0; i < smallest_sample_size; ++i) {
    for (MapIt it = samples.begin(); it != samples.end(); ++it) {
      const int64 n_allocs = it->second.number_allocations_[i];
      ss << n_allocs << kDelimiter;
    }
    ss << kNewLine;
  }
  ss << kNewLine;
  std::string output = ss.str();
  return output;
}

void MemoryTrackerCompressedTimeSeries::AcquireSample(
    MemoryTracker* memory_tracker, TimeSeries* timeseries,
    const base::TimeDelta& time_now) {
  const size_t sample_count = timeseries->time_stamps_.size();
  timeseries->time_stamps_.push_back(time_now);
  MapAllocationSamples& map_samples = timeseries->samples_;

  std::vector<const AllocationGroup*> vector_output;
  memory_tracker->GetAllocationGroups(&vector_output);

  // Sample all known memory scopes.
  for (size_t i = 0; i < vector_output.size(); ++i) {
    const AllocationGroup* group = vector_output[i];
    const std::string& name = group->name();

    const bool first_creation =
        map_samples.find(group->name()) == map_samples.end();

    AllocationSamples& new_entry = map_samples[name];

    // Didn't see it before so create new entry.
    if (first_creation) {
      // Make up for lost samples...
      new_entry.allocated_bytes_.resize(sample_count, 0);
      new_entry.number_allocations_.resize(sample_count, 0);
    }

    int32 num_allocs = -1;
    int64 allocation_bytes = -1;
    group->GetAggregateStats(&num_allocs, &allocation_bytes);

    new_entry.allocated_bytes_.push_back(allocation_bytes);
    new_entry.number_allocations_.push_back(num_allocs);
  }
}

bool MemoryTrackerCompressedTimeSeries::IsFull(const TimeSeries& timeseries,
                                               size_t samples_limit) {
  return timeseries.time_stamps_.size() >= samples_limit;
}

template <typename VectorT>
void DoCompression(VectorT* samples) {
  for (size_t i = 0; i * 2 < samples->size(); ++i) {
    (*samples)[i] = (*samples)[i * 2];
  }
  samples->resize(samples->size() / 2);
}

void MemoryTrackerCompressedTimeSeries::Compress(TimeSeries* timeseries) {
  typedef MapAllocationSamples::iterator MapIt;
  MapAllocationSamples& samples = timeseries->samples_;
  DoCompression(&(timeseries->time_stamps_));
  for (MapIt it = samples.begin(); it != samples.end(); ++it) {
    AllocationSamples& data = it->second;
    DoCompression(&data.allocated_bytes_);
    DoCompression(&data.number_allocations_);
  }
}

MemorySizeBinner::MemorySizeBinner(const std::string& memory_scope_name)
    : memory_scope_name_(memory_scope_name) {}

const AllocationGroup* FindAllocationGroup(const std::string& name,
                                           MemoryTracker* memory_tracker) {
  std::vector<const AllocationGroup*> groups;
  memory_tracker->GetAllocationGroups(&groups);
  // Find by exact string match.
  for (size_t i = 0; i < groups.size(); ++i) {
    const AllocationGroup* group = groups[i];
    if (group->name().compare(name) == 0) {
      return group;
    }
  }
  return NULL;
}

void MemorySizeBinner::Run(Params* params) {
  const AllocationGroup* target_group = NULL;

  while (!params->finished()) {
    if (target_group == NULL && !memory_scope_name_.empty()) {
      target_group =
          FindAllocationGroup(memory_scope_name_, params->memory_tracker());
    }

    std::stringstream ss;
    ss.precision(2);
    if (target_group || memory_scope_name_.empty()) {
      AllocationSizeBinner visitor_binner = AllocationSizeBinner(target_group);
      params->memory_tracker()->Accept(&visitor_binner);

      size_t min_size = 0;
      size_t max_size = 0;

      visitor_binner.GetLargestSizeRange(&min_size, &max_size);

      FindTopSizes top_size_visitor =
          FindTopSizes(min_size, max_size, target_group);
      params->memory_tracker()->Accept(&top_size_visitor);

      ss << kNewLine;
      ss << "TimeNow " << params->TimeInMinutesString() << " (minutes):";
      ss << kNewLine;
      if (!memory_scope_name_.empty()) {
        ss << "Tracking Memory Scope \"" << memory_scope_name_ << "\", ";
      } else {
        ss << "Tracking whole program, ";
      }
      ss << "first row is allocation size range, second row is number of "
         << kNewLine << "allocations in that range." << kNewLine;
      ss << visitor_binner.ToCSVString();
      ss << kNewLine;
      ss << "Largest allocation range: \"" << min_size << "..." << max_size
         << "\"" << kNewLine;
      ss << "Printing out top allocations from this range: " << kNewLine;
      ss << top_size_visitor.ToString(5) << kNewLine;
    } else {
      ss << "No allocations for \"" << memory_scope_name_ << "\".";
    }

    params->logger()->Output(ss.str().c_str());
    params->logger()->Flush();

    // Sleep until the next sample.
    base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(1));
  }
}

size_t AllocationSizeBinner::GetBucketIndexForAllocationSize(size_t size) {
  for (int i = 0; i < 32; ++i) {
    size_t val = 0x1 << i;
    if (val > size) {
      return i;
    }
  }
  SB_NOTREACHED();
  return 32;
}

void AllocationSizeBinner::GetSizeRange(size_t size, size_t* min_value,
                                        size_t* max_value) {
  size_t idx = GetBucketIndexForAllocationSize(size);
  IndexToSizeRange(idx, min_value, max_value);
}

void AllocationSizeBinner::IndexToSizeRange(size_t idx, size_t* min_value,
                                            size_t* max_value) {
  if (idx == 0) {
    *min_value = 0;
    *max_value = 0;
    return;
  }
  *min_value = 0x1 << (idx - 1);
  *max_value = (*min_value << 1) - 1;
  return;
}

size_t AllocationSizeBinner::GetIndexRepresentingMostMemoryConsumption() const {
  int64 largest_allocation_total = 0;
  size_t largest_allocation_total_idx = 0;

  for (size_t i = 0; i < allocation_histogram_.size(); ++i) {
    size_t alloc_size = 0x1 << i;
    size_t count = allocation_histogram_[i];
    int64 allocation_total =
        static_cast<int64>(alloc_size) * static_cast<int64>(count);

    if (largest_allocation_total < allocation_total) {
      largest_allocation_total = allocation_total;
      largest_allocation_total_idx = i;
    }
  }
  return largest_allocation_total_idx;
}

void AllocationSizeBinner::GetLargestSizeRange(size_t* min_value,
                                               size_t* max_value) const {
  size_t index = GetIndexRepresentingMostMemoryConsumption();
  IndexToSizeRange(index, min_value, max_value);
}

AllocationSizeBinner::AllocationSizeBinner(const AllocationGroup* group_filter)
    : group_filter_(group_filter) {
  allocation_histogram_.resize(33);
}

bool AllocationSizeBinner::PassesFilter(
    const AllocationRecord& alloc_record) const {
  if (group_filter_ == NULL) {
    return true;
  }

  return alloc_record.allocation_group == group_filter_;
}

bool AllocationSizeBinner::Visit(const void* /*memory*/,
                                 const AllocationRecord& alloc_record) {
  if (PassesFilter(alloc_record)) {
    const size_t idx = GetBucketIndexForAllocationSize(alloc_record.size);
    allocation_histogram_[idx]++;
  }
  return true;
}

std::string AllocationSizeBinner::ToCSVString() const {
  size_t first_idx = 0;
  size_t end_idx = allocation_histogram_.size();

  // Determine the start index by skipping all consecutive head entries
  // that are 0.
  while (first_idx < allocation_histogram_.size()) {
    const size_t num_allocs = allocation_histogram_[first_idx];
    if (num_allocs > 0) {
      break;
    }
    first_idx++;
  }

  // Determine the end index by skipping all consecutive tail entries
  // that are 0.
  while (end_idx > 0) {
    if (end_idx < allocation_histogram_.size()) {
      const size_t num_allocs = allocation_histogram_[end_idx];
      if (num_allocs > 0) {
        ++end_idx;
        break;
      }
    }
    end_idx--;
  }

  std::stringstream ss;
  for (size_t i = first_idx; i < end_idx; ++i) {
    size_t min = 0;
    size_t max = 0;
    IndexToSizeRange(i, &min, &max);
    std::stringstream name_ss;
    name_ss << kQuote << min << "..." << max << kQuote;
    ss << name_ss.str() << kDelimiter;
  }
  ss << kNewLine;

  for (size_t i = first_idx; i < end_idx; ++i) {
    const size_t num_allocs = allocation_histogram_[i];
    ss << num_allocs << kDelimiter;
  }
  ss << kNewLine;
  return ss.str();
}

FindTopSizes::FindTopSizes(size_t minimum_size, size_t maximum_size,
                           const AllocationGroup* group)
    : minimum_size_(minimum_size),
      maximum_size_(maximum_size),
      group_filter_(group) {}

bool FindTopSizes::Visit(const void* /*memory*/,
                         const AllocationRecord& alloc_record) {
  if (PassesFilter(alloc_record)) {
    size_counter_[alloc_record.size]++;
  }
  return true;
}

std::string FindTopSizes::ToString(size_t max_elements_to_print) const {
  std::vector<GroupAllocation> group_allocs = GetTopAllocations();
  const size_t n = std::min(max_elements_to_print, group_allocs.size());

  if (!group_allocs.empty()) {
    std::stringstream ss;

    for (size_t i = 0; i < n; ++i) {
      GroupAllocation g = group_allocs[i];
      size_t total_size = g.allocation_count * g.allocation_size;
      ss << "    " << total_size
         << " bytes allocated with object size: " << g.allocation_size
         << " bytes in " << g.allocation_count << " instances " << kNewLine;
    }
    return ss.str();
  } else {
    return std::string();
  }
}

std::vector<FindTopSizes::GroupAllocation> FindTopSizes::GetTopAllocations()
    const {
  std::vector<GroupAllocation> group_allocs;
  // Push objects to a vector.
  for (SizeCounterMap::const_iterator it = size_counter_.begin();
       it != size_counter_.end(); ++it) {
    GroupAllocation alloc = {it->first, it->second};
    group_allocs.push_back(alloc);
  }

  std::sort(group_allocs.begin(), group_allocs.end(),
            GroupAllocation::LessAllocationSize);
  // Biggest first.
  std::reverse(group_allocs.begin(), group_allocs.end());
  return group_allocs;
}

bool FindTopSizes::PassesFilter(const AllocationRecord& alloc_record) const {
  if (alloc_record.size < minimum_size_) return false;
  if (alloc_record.size > maximum_size_) return false;
  if (!group_filter_) return true;  // No group filter when null.
  return group_filter_ == alloc_record.allocation_group;
}

MemoryTrackerLogWriter::MemoryTrackerLogWriter() : start_time_(NowTime()) {
  buffered_file_writer_.reset(new BufferedFileWriter(MemoryLogPath()));
  InitAndRegisterMemoryReporter();
}

MemoryTrackerLogWriter::~MemoryTrackerLogWriter() {
  // No locks are used for the thread reporter, so when it's set to null
  // we allow one second for any suspended threads to run through and finish
  // their reporting.
  SbMemorySetReporter(NULL);
  SbThreadSleep(kSbTimeSecond);
  buffered_file_writer_.reset(NULL);
}

std::string MemoryTrackerLogWriter::tool_name() const {
  return "MemoryTrackerLogWriter";
}

void MemoryTrackerLogWriter::Run(Params* params) {
  // Run function does almost nothing.
  params->logger()->Output("MemoryTrackerLogWriter running...");
}

void MemoryTrackerLogWriter::OnMemoryAllocation(const void* memory_block,
                                                size_t size) {
  void* addresses[kMaxStackSize] = {};
  // Though the SbSystemGetStack API documentation does not specify any possible
  // negative return values, we take no chance.
  const size_t count = std::max(SbSystemGetStack(addresses, kMaxStackSize), 0);

  const size_t n = 256;
  char buff[n] = {0};
  size_t buff_pos = 0;

  int time_since_start_ms = GetTimeSinceStartMs();
  // Writes "+ <ALLOCATION ADDRESS> <size> <time>"
  int bytes_written =
      SbStringFormatF(buff, sizeof(buff), "+ %" PRIXPTR " %x %d",
                      reinterpret_cast<uintptr_t>(memory_block),
                      static_cast<unsigned int>(size), time_since_start_ms);

  buff_pos += bytes_written;
  const size_t end_index = std::min(count, kStartIndex + kNumAddressPrints);

  // For each of the stack addresses that we care about, concat them to the
  // buffer. This was originally written to do multiple stack addresses but
  // this tends to overflow on some lower platforms so it's possible that
  // this loop only iterates once.
  for (size_t i = kStartIndex; i < end_index; ++i) {
    void* p = addresses[i];
    int bytes_written = SbStringFormatF(buff + buff_pos,
                                        sizeof(buff) - buff_pos,
                                        " %" PRIXPTR "",
                                        reinterpret_cast<uintptr_t>(p));
    DCHECK_GE(bytes_written, 0);

    if (bytes_written < 0) {
      DCHECK(false) << "Error occurred while writing string.";
      continue;
    }

    buff_pos += static_cast<size_t>(bytes_written);
  }
  // Adds a "\n" at the end.
  SbStringConcat(buff + buff_pos, "\n", static_cast<int>(n - buff_pos));
  buffered_file_writer_->Append(buff, strlen(buff));
}

void MemoryTrackerLogWriter::OnMemoryDeallocation(const void* memory_block) {
  const size_t n = 256;
  char buff[n] = {0};
  // Writes "- <ADDRESS OF ALLOCATION> \n"
  SbStringFormatF(buff, sizeof(buff), "- %" PRIXPTR "\n",
                  reinterpret_cast<uintptr_t>(memory_block));
  buffered_file_writer_->Append(buff, strlen(buff));
}

void MemoryTrackerLogWriter::OnAlloc(void* context, const void* memory,
                                     size_t size) {
  MemoryTrackerLogWriter* self = static_cast<MemoryTrackerLogWriter*>(context);
  self->OnMemoryAllocation(memory, size);
}

void MemoryTrackerLogWriter::OnDealloc(void* context, const void* memory) {
  MemoryTrackerLogWriter* self = static_cast<MemoryTrackerLogWriter*>(context);
  self->OnMemoryDeallocation(memory);
}

void MemoryTrackerLogWriter::OnMapMemory(void* context, const void* memory,
                                         size_t size) {
  MemoryTrackerLogWriter* self = static_cast<MemoryTrackerLogWriter*>(context);
  self->OnMemoryAllocation(memory, size);
}

void MemoryTrackerLogWriter::OnUnMapMemory(void* context, const void* memory,
                                           size_t size) {
  SB_UNREFERENCED_PARAMETER(size);
  MemoryTrackerLogWriter* self = static_cast<MemoryTrackerLogWriter*>(context);
  self->OnMemoryDeallocation(memory);
}

std::string MemoryTrackerLogWriter::MemoryLogPath() {
  char file_name_buff[2048] = {};
  SbSystemGetPath(kSbSystemPathDebugOutputDirectory, file_name_buff,
                  arraysize(file_name_buff));
  std::string path(file_name_buff);
  if (!path.empty()) {  // Protect against a dangling "/" at end.
    const int back_idx_signed = static_cast<int>(path.length()) - 1;
    if (back_idx_signed >= 0) {
      const size_t idx = back_idx_signed;
      if (path[idx] == '/') {
        path.erase(idx);
      }
    }
  }
  path.append("/memory_log.txt");
  return path;
}

base::TimeTicks MemoryTrackerLogWriter::NowTime() {
  // NowFromSystemTime() is slower but more accurate. However it might
  // be useful to use the faster but less accurate version if there is
  // a speedup.
  return base::TimeTicks::Now();
}

int MemoryTrackerLogWriter::GetTimeSinceStartMs() const {
  base::TimeDelta dt = NowTime() - start_time_;
  return static_cast<int>(dt.InMilliseconds());
}

void MemoryTrackerLogWriter::InitAndRegisterMemoryReporter() {
  DCHECK(!memory_reporter_.get()) << "Memory Reporter already registered.";

  SbMemoryReporter mem_reporter = {OnAlloc, OnDealloc, OnMapMemory,
                                   OnUnMapMemory, this};
  memory_reporter_.reset(new SbMemoryReporter(mem_reporter));
  SbMemorySetReporter(memory_reporter_.get());
}

}  // namespace memory_tracker
}  // namespace browser
}  // namespace cobalt
