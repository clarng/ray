// Copyright 2022 The Ray Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ray/common/memory_monitor.h"

#include <filesystem>
#include <fstream>  // std::ifstream
#include <tuple>

#include "ray/common/ray_config.h"
#include "ray/util/logging.h"

namespace ray {

MemoryMonitor::MemoryMonitor(
    instrumented_io_context &io_service,
    float usage_threshold,
    int64_t max_overhead_bytes,
    uint64_t monitor_interval_ms,
    MemoryUsageRefreshCallback monitor_callback,
    ObjectStoreMemoryUsageFetcher object_store_memory_usage_fetcher)
    : usage_threshold_(usage_threshold),
      max_overhead_bytes_(max_overhead_bytes),
      monitor_callback_(monitor_callback),
      object_store_memory_usage_fetcher_(object_store_memory_usage_fetcher),
      runner_(io_service) {
  RAY_CHECK(monitor_callback_ != nullptr);
  RAY_CHECK_GE(usage_threshold_, 0);
  RAY_CHECK_LE(usage_threshold_, 1);
  if (monitor_interval_ms > 0) {
#ifdef __linux__
    runner_.RunFnPeriodically(
        [this] {
          auto [used_memory_bytes, total_memory_bytes] = GetMemoryBytes();
          MemorySnapshot system_memory;
          system_memory.heap_used_bytes = used_memory_bytes;
          system_memory.object_store_used_bytes = object_store_memory_usage_fetcher_();
          system_memory.total_bytes = total_memory_bytes;

          bool is_usage_above_threshold = IsUsageAboveThreshold(system_memory);
          monitor_callback_(is_usage_above_threshold, system_memory, usage_threshold_);
        },
        monitor_interval_ms,
        "MemoryMonitor.CheckIsMemoryUsageAboveThreshold");
    RAY_LOG(INFO) << "MemoryMonitor initialized";
#else
    RAY_LOG(WARNING) << "Not running MemoryMonitor. It is currently supported "
                     << "only on Linux.";
#endif
  } else {
    RAY_LOG(INFO) << "MemoryMonitor disabled. Specify "
                  << "`memory_monitor_interval_ms` > 0 to enable the monitor.";
  }
}

bool MemoryMonitor::IsUsageAboveThreshold(MemorySnapshot system_memory) {
  int64_t threshold_bytes = GetMemoryThreshold(system_memory.total_bytes, usage_threshold_, max_overhead_bytes_);
  int64_t heap_used_memory_bytes = system_memory.heap_used_bytes;
  int64_t total_memory_bytes = system_memory.total_bytes;
  if (total_memory_bytes == kNull || heap_used_memory_bytes == kNull) {
    RAY_LOG_EVERY_MS(WARNING, kLogIntervalMs)
        << "Unable to capture node memory. Monitor will not be able "
        << "to detect memory usage above threshold.";
    return false;
  }
  bool is_usage_above_threshold = system_memory.GetTotalUsedBytes() > threshold_bytes;
  if (is_usage_above_threshold) {
    RAY_LOG(INFO) << "Node memory usage above threshold, heap used: " << system_memory.heap_used_bytes
                  << ", object store used: " << system_memory.object_store_used_bytes
                  << ", total used: " << system_memory.GetTotalUsedBytes()
                  << ", threshold: " << threshold_bytes
                  << ", system total: " << system_memory.total_bytes;
  }
  return is_usage_above_threshold;
}

std::tuple<int64_t, int64_t> MemoryMonitor::GetMemoryBytes() {
  int64_t cgroup_total_bytes = GetCGroupMemoryLimitBytes();
#ifndef __linux__
  RAY_CHECK(false) << "Memory monitor currently supports only linux";
#endif
  auto [system_used_bytes, system_total_bytes] = GetLinuxMemoryBytes();
  /// cgroup memory limit can be higher than system memory limit when it is
  /// not used. We take its value only when it is less than or equal to system memory
  /// limit. TODO(clarng): find a better way to detect cgroup memory limit is used.
  system_total_bytes = NullableMin(system_total_bytes, cgroup_total_bytes);

  // We always take the used memory from system, which excludes cached and buffers.
  // Cgroup used memory includes cached and buffers and it does not accurately represent
  // when the OS OOM killer triggers, which ignores cached and buffers. We could
  // calculcate Cgroup used memory from memory.stats but those numbers are the same
  // as meminfo, which is what GetLinuxMemoryBytes() uses.
  return std::tuple(system_used_bytes, system_total_bytes);
}

int64_t MemoryMonitor::GetCGroupMemoryLimitBytes() {
  int64_t total_bytes = kNull;
  if (std::filesystem::exists(kCgroupsV2MemoryMaxPath)) {
    std::ifstream mem_file(kCgroupsV2MemoryMaxPath, std::ios::in | std::ios::binary);
    mem_file >> total_bytes;
  } else if (std::filesystem::exists(kCgroupsV1MemoryMaxPath)) {
    std::ifstream mem_file(kCgroupsV1MemoryMaxPath, std::ios::in | std::ios::binary);
    mem_file >> total_bytes;
  }

  /// This can be zero if the memory limit is not set for cgroup v2.
  if (total_bytes == 0) {
    total_bytes = kNull;
  }

  return total_bytes;
}


std::tuple<int64_t, int64_t> MemoryMonitor::GetLinuxMemoryBytes() {
  std::string meminfo_path = "/proc/meminfo";
  std::ifstream meminfo_ifs(meminfo_path, std::ios::in | std::ios::binary);
  if (!meminfo_ifs.is_open()) {
    RAY_LOG_EVERY_MS(ERROR, kLogIntervalMs) << " file not found: " << meminfo_path;
    return {kNull, kNull};
  }
  std::string line;
  std::string title;
  uint64_t value;
  std::string unit;

  int64_t mem_total_bytes = kNull;
  int64_t mem_available_bytes = kNull;
  int64_t mem_free_bytes = kNull;
  int64_t cached_bytes = kNull;
  int64_t buffer_bytes = kNull;
  while (std::getline(meminfo_ifs, line)) {
    std::istringstream iss(line);
    iss >> title >> value >> unit;
    /// Linux reports them as kiB
    RAY_CHECK(unit == "kB");
    value = value * 1024;
    if (title == "MemAvailable:") {
      mem_available_bytes = value;
    } else if (title == "MemFree:") {
      mem_free_bytes = value;
    } else if (title == "Cached:") {
      cached_bytes = value;
    } else if (title == "Buffers:") {
      buffer_bytes = value;
    } else if (title == "MemTotal:") {
      mem_total_bytes = value;
    }
  }
  if (mem_total_bytes == kNull) {
    RAY_LOG_EVERY_MS(ERROR, kLogIntervalMs)
        << "Unable to determine total bytes . Will return null";
    return {kNull, kNull};
  }

  int64_t available_bytes = kNull;
  /// Follows logic from psutil
  if (mem_available_bytes > 0) {
    available_bytes = mem_available_bytes;
  } else if (mem_free_bytes != kNull && cached_bytes != kNull && buffer_bytes != kNull) {
    available_bytes = mem_free_bytes + cached_bytes + buffer_bytes;
  }

  if (available_bytes == kNull) {
    RAY_LOG_EVERY_MS(ERROR, kLogIntervalMs)
        << "Unable to determine available bytes. Will return null";
    return {kNull, kNull};
  }
  if (mem_total_bytes < available_bytes) {
    RAY_LOG_EVERY_MS(ERROR, kLogIntervalMs)
        << "Total bytes less than available bytes. Will return null";
    return {kNull, kNull};
  }
  int64_t used_bytes = mem_total_bytes - available_bytes;
  if (used_bytes < 0) {
    RAY_LOG_EVERY_MS(WARNING, kLogIntervalMs)
        << "Got negative used memory for linux " << used_bytes << ", setting it to zero";
    used_bytes = 0;
  }
  return {used_bytes, mem_total_bytes};
}

int64_t MemoryMonitor::GetProcessMemoryBytes(int64_t process_id) const {
  std::stringstream smap_path;
  smap_path << "/proc/" << std::to_string(process_id) << "/smaps_rollup";
  return GetLinuxProcessMemoryBytesFromSmap(smap_path.str());
}

/// TODO:(clarng) align logic with psutil / Python-side memory calculations
int64_t MemoryMonitor::GetLinuxProcessMemoryBytesFromSmap(const std::string smap_path) {
  std::ifstream smap_ifs(smap_path, std::ios::in | std::ios::binary);
  if (!smap_ifs.is_open()) {
    RAY_LOG_EVERY_MS(ERROR, kLogIntervalMs) << " file not found: " << smap_path;
    return kNull;
  }

  int64_t uss = 0;

  std::string line;
  std::string title;
  uint64_t value;
  std::string unit;

  /// Read first line, which is the header
  std::getline(smap_ifs, line);
  while (std::getline(smap_ifs, line)) {
    std::istringstream iss(line);
    iss >> title >> value >> unit;

    /// Linux reports them as kiB
    RAY_CHECK(unit == "kB");
    value = value * 1024;
    if (title == "Private_Clean:" || title == "Private_Dirty:" ||
        title == "Private_Hugetlb:") {
      uss += value;
    }
  }

  if (uss == 0) {
    RAY_LOG_EVERY_MS(ERROR, kLogIntervalMs)
        << "Got zero used memory for smap file " << smap_path;
    return kNull;
  }
  return uss;
}

int64_t MemoryMonitor::NullableMin(int64_t left, int64_t right) {
  RAY_CHECK_GE(left, kNull);
  RAY_CHECK_GE(right, kNull);

  if (left == kNull) {
    return right;
  } else if (right == kNull) {
    return left;
  } else {
    return std::min(left, right);
  }
}

int64_t MemoryMonitor::GetMemoryThreshold(int64_t total_memory_bytes, float usage_threshold_, int64_t max_overhead_bytes) {
  RAY_CHECK_GE(total_memory_bytes, kNull);
  RAY_CHECK_GE(max_overhead_bytes, kNull);
  RAY_CHECK_GE(usage_threshold_, 0);
  RAY_CHECK_LE(usage_threshold_, 1);

  int64_t threshold_fraction = (int64_t) (total_memory_bytes * usage_threshold_);

  if (max_overhead_bytes > kNull) {
    int64_t threshold_absolute = total_memory_bytes - max_overhead_bytes;
    RAY_CHECK_GE(threshold_absolute, 0);
    return std::max(threshold_fraction, threshold_absolute);
  } else {
    return threshold_fraction;
  }
}

std::ostream &operator<<(std::ostream &os, const MemorySnapshot &memory_snapshot) {
  os << "Heap used bytes: " << memory_snapshot.heap_used_bytes
     << "Object store used bytes: " << memory_snapshot.object_store_used_bytes
     << ", Total bytes: " << memory_snapshot.total_bytes;
  return os;
}

}  // namespace ray
