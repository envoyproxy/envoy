#include "source/common/upstream/edf_scheduler.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {
namespace {

TEST(EdfSchedulerTest, Empty) {
  EdfScheduler<uint32_t> sched;
  EXPECT_EQ(nullptr, sched.peekAgain([](const double&) { return 0; }));
  EXPECT_EQ(nullptr, sched.pickAndAdd([](const double&) { return 0; }));
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
      auto peek = sched.peekAgain([](const double&) { return 1; });
      auto p = sched.pickAndAdd([](const double&) { return 1; });
      EXPECT_EQ(i, *p);
      EXPECT_EQ(*peek, *p);
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
    auto peek = sched.peekAgain([](const double& orig) { return orig + 1; });
    auto p = sched.pickAndAdd([](const double& orig) { return orig + 1; });
    EXPECT_EQ(*p, *peek);
    ++pick_count[*p];
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

  auto peek = sched.peekAgain([](const double&) { return 1; });
  auto p = sched.pickAndAdd([](const double&) { return 1; });
  EXPECT_EQ(*peek, *p);
  EXPECT_EQ(*second_entry, *p);
  EXPECT_EQ(*second_entry, *p);
}

// Validate that expired entries are not peeked.
TEST(EdfSchedulerTest, ExpiredPeek) {
  EdfScheduler<uint32_t> sched;

  {
    auto second_entry = std::make_shared<uint32_t>(42);
    auto first_entry = std::make_shared<uint32_t>(37);
    sched.add(2, first_entry);
    sched.add(1, second_entry);
  }
  auto third_entry = std::make_shared<uint32_t>(37);
  sched.add(3, third_entry);

  EXPECT_EQ(37, *sched.peekAgain([](const double&) { return 1; }));
}

// Validate that expired entries are ignored.
TEST(EdfSchedulerTest, ExpiredPeekedIsNotPicked) {
  EdfScheduler<uint32_t> sched;

  {
    auto second_entry = std::make_shared<uint32_t>(42);
    auto first_entry = std::make_shared<uint32_t>(37);
    sched.add(2, first_entry);
    sched.add(1, second_entry);
    for (int i = 0; i < 3; ++i) {
      EXPECT_TRUE(sched.peekAgain([](const double&) { return 1; }) != nullptr);
    }
  }

  EXPECT_TRUE(sched.peekAgain([](const double&) { return 1; }) == nullptr);
  EXPECT_TRUE(sched.pickAndAdd([](const double&) { return 1; }) == nullptr);
}

TEST(EdfSchedulerTest, ManyPeekahead) {
  EdfScheduler<uint32_t> sched1;
  EdfScheduler<uint32_t> sched2;
  constexpr uint32_t num_entries = 128;
  std::shared_ptr<uint32_t> entries[num_entries];

  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched1.add(1, entries[i]);
    sched2.add(1, entries[i]);
  }

  std::vector<uint32_t> picks;
  for (uint32_t rounds = 0; rounds < 10; ++rounds) {
    picks.push_back(*sched1.peekAgain([](const double&) { return 1; }));
  }
  for (uint32_t rounds = 0; rounds < 10; ++rounds) {
    auto p1 = sched1.pickAndAdd([](const double&) { return 1; });
    auto p2 = sched2.pickAndAdd([](const double&) { return 1; });
    EXPECT_EQ(picks[rounds], *p1);
    EXPECT_EQ(*p2, *p1);
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
