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

#include <sys/sysinfo.h>

#include "gtest/gtest.h"
#include "ray/common/asio/instrumented_io_context.h"
#include "ray/util/process.h"

namespace ray {
class MemoryMonitorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    thread_ = std::make_unique<std::thread>([this]() {
      boost::asio::io_context::work work(io_context_);
      io_context_.run();
    });
  }
  void TearDown() override {
    io_context_.stop();
    thread_->join();
  }
  std::unique_ptr<std::thread> thread_;
  instrumented_io_context io_context_;
};

TEST_F(MemoryMonitorTest, TestThresholdZeroMonitorAlwaysAboveThreshold) {
  MemoryMonitor monitor(
      MemoryMonitorTest::io_context_,
      0 /*usage_threshold*/,
      MemoryMonitor::kNull /*max_overhead_bytes*/,
      0 /*refresh_interval_ms*/,
      [](bool is_usage_above_threshold,
         MemorySnapshot system_memory,
         float usage_threshold) { FAIL() << "Expected monitor to not run"; },
      []() { return 0; });
  ASSERT_TRUE(monitor.IsUsageAboveThreshold({1, 0, 10}));
}

TEST_F(MemoryMonitorTest, TestThresholdOneMonitorAlwaysBelowThreshold) {
  MemoryMonitor monitor(
      MemoryMonitorTest::io_context_,
      1 /*usage_threshold*/,
      MemoryMonitor::kNull /*max_overhead_bytes*/,
      0 /*refresh_interval_ms*/,
      [](bool is_usage_above_threshold,
         MemorySnapshot system_memory,
         float usage_threshold) { FAIL() << "Expected monitor to not run"; },
      []() { return 0; });
  ASSERT_FALSE(monitor.IsUsageAboveThreshold({9, 0, 10}));
}

TEST_F(MemoryMonitorTest, TestUsageAtThresholdReportsTrue) {
  MemoryMonitor monitor(
      MemoryMonitorTest::io_context_,
      0.5 /*usage_threshold*/,
      MemoryMonitor::kNull /*max_overhead_bytes*/,
      0 /*refresh_interval_ms*/,
      [](bool is_usage_above_threshold,
         MemorySnapshot system_memory,
         float usage_threshold) { FAIL() << "Expected monitor to not run"; },
      []() { return 0; });
  ASSERT_FALSE(monitor.IsUsageAboveThreshold({4, 0, 10}));
  ASSERT_FALSE(monitor.IsUsageAboveThreshold({2, 2, 10}));
  ASSERT_FALSE(monitor.IsUsageAboveThreshold({0, 4, 10}));

  ASSERT_TRUE(monitor.IsUsageAboveThreshold({5, 0, 10}));
  ASSERT_TRUE(monitor.IsUsageAboveThreshold({2, 3, 10}));
  ASSERT_TRUE(monitor.IsUsageAboveThreshold({0, 5, 10}));

  ASSERT_TRUE(monitor.IsUsageAboveThreshold({6, 0, 10}));
  ASSERT_TRUE(monitor.IsUsageAboveThreshold({3, 3, 10}));
  ASSERT_TRUE(monitor.IsUsageAboveThreshold({0, 6, 10}));
}

TEST_F(MemoryMonitorTest, TestGetNodeAvailableMemoryAlwaysPositive) {
  MemoryMonitor monitor(
      MemoryMonitorTest::io_context_,
      0 /*usage_threshold*/,
      MemoryMonitor::kNull /*max_overhead_bytes*/,
      0 /*refresh_interval_ms*/,
      [](bool is_usage_above_threshold,
         MemorySnapshot system_memory,
         float usage_threshold) { FAIL() << "Expected monitor to not run"; },
      []() { return 0; });
  auto [used_bytes, total_bytes] = monitor.GetMemoryBytes();
  ASSERT_GT(total_bytes, 0);
  ASSERT_GT(total_bytes, used_bytes);
}

TEST_F(MemoryMonitorTest, TestGetNodeTotalMemoryEqualsFreeOrCGroup) {
  {
    MemoryMonitor monitor(
        MemoryMonitorTest::io_context_,
        0 /*usage_threshold*/,
        MemoryMonitor::kNull /*max_overhead_bytes*/,
        0 /*refresh_interval_ms*/,
        [](bool is_usage_above_threshold,
           MemorySnapshot system_memory,
           float usage_threshold) { FAIL() << "Expected monitor to not run"; },
        []() { return 0; });
    auto [used_bytes, total_bytes] = monitor.GetMemoryBytes();
    int64_t cgroup_total_bytes = monitor.GetCGroupMemoryLimitBytes();

    auto cmd_out = Process::Exec("free -b");
    std::string title;
    std::string total;
    std::string used;
    std::string free;
    std::string shared;
    std::string cache;
    std::string available;
    std::istringstream cmd_out_ss(cmd_out);
    cmd_out_ss >> total >> used >> free >> shared >> cache >> available;
    cmd_out_ss >> title >> total >> used >> free >> shared >> cache >> available;

    int64_t free_total_bytes;
    std::istringstream total_ss(total);
    total_ss >> free_total_bytes;

    ASSERT_TRUE(total_bytes == free_total_bytes || total_bytes == cgroup_total_bytes);
  }
}

TEST_F(MemoryMonitorTest, TestMonitorPeriodSetCallbackExecuted) {
  std::condition_variable callback_ran;
  std::mutex callback_ran_mutex;

  MemoryMonitor monitor(
      MemoryMonitorTest::io_context_,
      1 /*usage_threshold*/,
      MemoryMonitor::kNull /*max_overhead_bytes*/,
      1 /*refresh_interval_ms*/,
      [&callback_ran](bool is_usage_above_threshold,
                      MemorySnapshot system_memory,
                      float usage_threshold) {
        ASSERT_EQ(1.0f, usage_threshold);
        ASSERT_GT(system_memory.total_bytes, 0);
        ASSERT_GT(system_memory.heap_used_bytes, 0);
        callback_ran.notify_all();
      },
      []() { return 0; });
  std::unique_lock<std::mutex> callback_ran_mutex_lock(callback_ran_mutex);
  callback_ran.wait(callback_ran_mutex_lock);
}

TEST_F(MemoryMonitorTest, TestZeroMaxOverheadMeansThresholdIsMax) {
  MemoryMonitor monitor(
      MemoryMonitorTest::io_context_,
      0.5 /*usage_threshold*/,
      0 /*max_overhead_bytes*/,
      0 /*refresh_interval_ms*/,
      [](bool is_usage_above_threshold,
         MemorySnapshot system_memory,
         float usage_threshold) { FAIL() << "Expected monitor to not run"; },
      []() { return 0; });
  ASSERT_FALSE(monitor.IsUsageAboveThreshold({9, 0, 10}));
  ASSERT_TRUE(monitor.IsUsageAboveThreshold({10, 0, 10}));
}

TEST_F(MemoryMonitorTest, TestGetMemoryThresholdTakeGreaterOfTheTwoValues) {
  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 0.5, 0), 100);
  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 0.5, 60), 50);

  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 1, 10), 100);
  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 1, 100), 100);

  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 0.1, 100), 10);
  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 0, 10), 90);
  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 0, 100), 0);

  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 0, MemoryMonitor::kNull), 0);
  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 0.5, MemoryMonitor::kNull), 50);
  ASSERT_EQ(MemoryMonitor::GetMemoryThreshold(100, 1, MemoryMonitor::kNull), 100);
}

}  // namespace ray

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
