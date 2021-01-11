#include "common/upstream/wrsq_scheduler.h"
#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Upstream {
namespace {

TEST(WRSQSchedulerTest, Empty) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched(random);
  EXPECT_EQ(nullptr, sched.peekAgain([](const double&) { return 0; }));
  EXPECT_EQ(nullptr, sched.pickAndAdd([](const double&) { return 0; }));
}

// Validate we get regular RR behavior when all weights are the same.
TEST(WRSQSchedulerTest, Unweighted) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched(random);
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

// Validate selection probabilities.
TEST(WRSQSchedulerTest, ProbabilityVerification) {
  Random::MockRandomGenerator random;
  WRSQScheduler<uint32_t> sched(random);
  constexpr uint32_t num_entries = 16;
  std::shared_ptr<uint32_t> entries[num_entries];
  uint32_t pick_count[num_entries];

  double weight_sum = 0;

  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched.add(i + 1, entries[i]);
    weight_sum += (i + 1);
    pick_count[i] = 0;
  }

  // If we try every random number between 0 and the weight sum, we should select each object the
  // number of times equal to its weight.
  for (uint32_t i = 0; i < weight_sum; ++i) {
    EXPECT_CALL(random, random()).Times(1).WillRepeatedly(Return(i));
    // The weights will not change with WRSQ, so the predicate does not matter.
    auto peek = sched.peekAgain([](const double&) { return 1; });
    auto p = sched.pickAndAdd([](const double&) { return 1; });
    EXPECT_EQ(*p, *peek);
    ++pick_count[*p];
  }

  for (uint32_t i = 0; i < num_entries; ++i) {
    EXPECT_EQ(i + 1, pick_count[i]);
  }
}

// Validate that expired entries are ignored.
TEST(WRSQSchedulerTest, Expired1) {
  Random::MockRandomGenerator random;
  WRSQScheduler<uint32_t> sched(random);

  auto second_entry = std::make_shared<uint32_t>(42);
  {
    auto first_entry = std::make_shared<uint32_t>(37);
    sched.add(1000, first_entry);
    sched.add(1, second_entry);
  }

  EXPECT_CALL(random, random())
    .WillOnce(Return(0))
    .WillOnce(Return(299))
    .WillOnce(Return(1337));
  auto peek = sched.peekAgain([](const double&) { return 1; });
  auto p1 = sched.pickAndAdd([](const double&) { return 1; });
  auto p2 = sched.pickAndAdd([](const double&) { return 1; });
  EXPECT_EQ(*peek, *p1);
  EXPECT_EQ(*second_entry, *p1);
  EXPECT_EQ(*second_entry, *p2);
}

// Validate that expired entries on either end of "the good one" are ignored.
TEST(WRSQSchedulerTest, Expired2) {
  Random::MockRandomGenerator random;
  WRSQScheduler<uint32_t> sched(random);

  auto second_entry = std::make_shared<uint32_t>(42);
  {
    auto first_entry = std::make_shared<uint32_t>(37);
    auto third_entry = std::make_shared<uint32_t>(22);
    sched.add(1000, first_entry);
    sched.add(1, second_entry);
    sched.add(100, third_entry);
  }

  EXPECT_CALL(random, random())
    .WillOnce(Return(0))
    .WillOnce(Return(299))
    .WillOnce(Return(1337))
    .WillOnce(Return(8675309));
  auto peek = sched.peekAgain([](const double&) { return 1; });
  auto p1 = sched.pickAndAdd([](const double&) { return 1; });
  auto p2 = sched.pickAndAdd([](const double&) { return 1; });
  EXPECT_EQ(*peek, *p1);
  EXPECT_EQ(*second_entry, *p1);
  EXPECT_EQ(*second_entry, *p2);
}

// Validate that expired entries are not peeked.
TEST(WRSQSchedulerTest, ExpiredPeek) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched(random);

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
TEST(WRSQSchedulerTest, ExpiredPeekedIsNotPicked) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched(random);

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

TEST(WRSQSchedulerTest, ManyPeekahead) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched1(random);
  WRSQScheduler<uint32_t> sched2(random);
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
