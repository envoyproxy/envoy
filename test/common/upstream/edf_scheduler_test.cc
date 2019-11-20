#include "common/upstream/edf_scheduler.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(EdfSchedulerTest, Empty) {
  EdfScheduler<uint32_t> sched;
  EXPECT_EQ(nullptr, sched.pick());
}

// Validate we get regular RR behavior when all weights are the same.
TEST(EdfSchedulerTest, Unweighted) {
  EdfScheduler<uint32_t> sched;
  constexpr uint32_t num_entries = 128;
  std::shared_ptr<uint32_t> entries[num_entries];

  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched.add(1, entries[i]);
  }

  for (uint32_t rounds = 0; rounds < 128; ++rounds) {
    for (uint32_t i = 0; i < num_entries; ++i) {
      auto p = sched.pick();
      EXPECT_EQ(i, *p);
      sched.add(1, p);
    }
  }
}

// Validate we get weighted RR behavior when weights are distinct.
TEST(EdfSchedulerTest, Weighted) {
  EdfScheduler<uint32_t> sched;
  constexpr uint32_t num_entries = 128;
  std::shared_ptr<uint32_t> entries[num_entries];
  uint32_t pick_count[num_entries];

  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched.add(i + 1, entries[i]);
    pick_count[i] = 0;
  }

  for (uint32_t i = 0; i < (num_entries * (1 + num_entries)) / 2; ++i) {
    auto p = sched.pick();
    ++pick_count[*p];
    sched.add(*p + 1, p);
  }

  for (uint32_t i = 0; i < num_entries; ++i) {
    EXPECT_EQ(i + 1, pick_count[i]);
  }
}

// Validate that expired entries are ignored.
TEST(EdfSchedulerTest, Expired) {
  EdfScheduler<uint32_t> sched;

  auto second_entry = std::make_shared<uint32_t>(42);
  {
    auto first_entry = std::make_shared<uint32_t>(37);
    sched.add(2, first_entry);
    sched.add(1, second_entry);
  }

  auto p = sched.pick();
  EXPECT_EQ(*second_entry, *p);
  EXPECT_EQ(nullptr, sched.pick());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
