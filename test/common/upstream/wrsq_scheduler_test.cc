#include "source/common/upstream/wrsq_scheduler.h"

#include "test/mocks/common.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Upstream {
namespace {

TEST(WRSQSchedulerTest, Empty) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched(random);
  EXPECT_EQ(nullptr, sched.peekAgain([](const double&) { return 1; }));
  EXPECT_EQ(nullptr, sched.pickAndAdd([](const double&) { return 1; }));
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
    EXPECT_CALL(random, random()).WillOnce(Return(i));
    auto peek = sched.peekAgain([](const double& x) { return x + 1; });
    auto p = sched.pickAndAdd([](const double& x) { return x + 1; });
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

  EXPECT_CALL(random, random()).WillOnce(Return(0)).WillOnce(Return(299)).WillOnce(Return(1337));
  auto peek = sched.peekAgain({});
  auto p1 = sched.pickAndAdd({});
  auto p2 = sched.pickAndAdd({});
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
  auto peek = sched.peekAgain({});
  auto p1 = sched.pickAndAdd({});
  auto p2 = sched.pickAndAdd({});
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
  auto third_entry = std::make_shared<uint32_t>(99);
  sched.add(3, third_entry);

  EXPECT_EQ(*third_entry, *sched.peekAgain({}));
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
      EXPECT_TRUE(sched.peekAgain({}) != nullptr);
    }
  }

  EXPECT_TRUE(sched.peekAgain({}) == nullptr);
  EXPECT_TRUE(sched.pickAndAdd({}) == nullptr);
}

// Ensure the multiple values that are peeked are the same ones returned via calls to `pickAndAdd`.
// This test also verifies that the scheduler behavior is vanilla round-robin when all of the
// weights are identical by ensuring the same values are selected across 2 different schedulers.
TEST(WRSQSchedulerTest, ManyPeekahead) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched1(random);
  WRSQScheduler<uint32_t> sched2(random);
  constexpr uint32_t num_entries = 128;
  std::shared_ptr<uint32_t> entries[num_entries];

  // Populate the schedulers.
  for (uint32_t i = 0; i < num_entries; ++i) {
    entries[i] = std::make_shared<uint32_t>(i);
    sched1.add(1, entries[i]);
    sched2.add(1, entries[i]);
  }

  // Peek values and store them for comparison later.
  std::vector<uint32_t> picks;
  for (uint32_t rounds = 0; rounds < 10; ++rounds) {
    picks.push_back(*sched1.peekAgain({}));
  }

  // Verify the picked values are those we peeked earlier. We'll also verify both schedulers are
  // returning the same values, since we expect vanilla round-robin behavior.
  for (uint32_t rounds = 0; rounds < 10; ++rounds) {
    auto p1 = sched1.pickAndAdd({});
    auto p2 = sched2.pickAndAdd({});
    EXPECT_EQ(picks[rounds], *p1);
    EXPECT_EQ(*p2, *p1);
  }
}

// Expire all objects and verify nullptr is returned.
TEST(WRSQSchedulerTest, ExpireAll) {
  Random::MockRandomGenerator random;
  WRSQScheduler<uint32_t> sched(random);

  // The weights are small enough that we can just burn through all the relevant random numbers that
  // would be generated as long as we hit 12 consecutive numbers for each part.
  uint32_t rnum{0};

  {
    // Add objects of the same weight.
    auto e1 = std::make_shared<uint32_t>(42);
    auto e2 = std::make_shared<uint32_t>(37);
    sched.add(1, e1);
    sched.add(1, e2);

    {
      auto e3 = std::make_shared<uint32_t>(7);
      auto e4 = std::make_shared<uint32_t>(13);
      sched.add(5, e3);
      sched.add(5, e4);

      // We've got unexpired values, so we should be able to pick them. While we're at it, we can
      // check we're getting objects from both weight queues.
      uint32_t weight1pick{0}, weight5pick{0};
      for (int i = 0; i < 1000; ++i) {
        EXPECT_CALL(random, random()).WillOnce(Return(rnum++));
        switch (*sched.pickAndAdd({})) {
        case 42:
        case 37:
          ++weight1pick;
          break;
        case 7:
        case 13:
          ++weight5pick;
          break;
        default:
          FAIL() << "bogus value returned";
        }
      }
      EXPECT_GT(weight5pick, 0);
      EXPECT_GT(weight1pick, 0);
    }

    // Expired the entirety of the high-probability queue. Let's make sure we behave properly by
    // expiring them and only returning the unexpired entries.
    for (int i = 0; i < 1000; ++i) {
      EXPECT_CALL(random, random()).WillRepeatedly(Return(rnum++));
      switch (*sched.peekAgain({})) {
      case 42:
      case 37:
        break;
      default:
        FAIL() << "bogus value returned";
      }
    }
  }

  // All values have expired, so only nullptr should be returned.
  EXPECT_CALL(random, random()).WillRepeatedly(Return(rnum++));
  EXPECT_EQ(sched.peekAgain({}), nullptr);
}

// Validate that a new requested weight is honored.
TEST(WRSQSchedulerTest, ChangingWeight) {
  NiceMock<Random::MockRandomGenerator> random;
  WRSQScheduler<uint32_t> sched(random);

  auto e1 = std::make_shared<uint32_t>(123);
  auto e2 = std::make_shared<uint32_t>(456);
  auto e3 = std::make_shared<uint32_t>(789);
  sched.add(1, e1);
  sched.add(0, e2);

  // Expecting only e1 to be picked. Weights are {e1=0, e2=1}.
  for (uint32_t rounds = 0; rounds < 128; ++rounds) {
    auto peek = sched.peekAgain({});
    auto p = sched.pickAndAdd({});
    EXPECT_EQ(*e1, *p);
    EXPECT_EQ(*peek, *p);
  }

  // Weights should be unchanged at this point. Still expect to pick e1, but now we'll change it to
  // be 0.
  auto p = sched.pickAndAdd([](auto) { return 0.0; });
  EXPECT_EQ(*e1, *p);
  sched.add(1, e3);

  // Weights are now {e1=0, e2=0, e3=1}. Without changing the weights, e3 should be the one picked
  // repeatedly
  for (uint32_t rounds = 0; rounds < 128; ++rounds) {
    auto p = sched.pickAndAdd({});
    EXPECT_EQ(*e3, *p);
  }
}

} // namespace
} // namespace Upstream
} // namespace Envoy
