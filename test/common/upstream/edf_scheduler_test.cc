#include "source/common/upstream/edf_scheduler.h"

#include "test/test_common/test_random_generator.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class EdfSchedulerTest : public testing::Test {
public:
  template <typename T>
  static void compareEdfSchedulers(EdfScheduler<T>& scheduler1, EdfScheduler<T>& scheduler2) {
    // Compares that the given EdfSchedulers internal queues are equal up
    // (ignoring the order_offset_ values).
    EXPECT_EQ(scheduler1.queue_.size(), scheduler2.queue_.size());
    // Cannot iterate over std::priority_queue directly, so need to copy the
    // contents to a vector first.
    auto copyFunc = [](EdfScheduler<T>& scheduler) {
      std::vector<typename EdfScheduler<T>::EdfEntry> result;
      result.reserve(scheduler.queue_.size());
      while (!scheduler.empty()) {
        result.emplace_back(std::move(scheduler.queue_.top()));
        scheduler.queue_.pop();
      }
      // Re-add all elements so the contents of the input scheduler isn't
      // changed.
      for (auto& entry : result) {
        scheduler.queue_.push(entry);
      }
      return result;
    };
    std::vector<typename EdfScheduler<T>::EdfEntry> contents1 = copyFunc(scheduler1);
    std::vector<typename EdfScheduler<T>::EdfEntry> contents2 = copyFunc(scheduler2);
    for (size_t i = 0; i < contents1.size(); ++i) {
      // Given 2 queues and some number of picks P, where one queue is created empty
      // and P picks are performed, and the other queue is created using
      // `EdfScheduler::createWithPicks()` their deadlines may be a bit different
      // due to floating point arithmetic differences. The comparison code uses
      // a NEAR comparison to account for such differences.
      EXPECT_NEAR(contents1[i].deadline_, contents2[i].deadline_, 1e-5)
          << "inequal deadline in element " << i;
      std::shared_ptr<T> entry1 = contents1[i].entry_.lock();
      std::shared_ptr<T> entry2 = contents2[i].entry_.lock();
      EXPECT_EQ(*entry1, *entry2) << "inequal entry in element " << i;
    }
  }
};

TEST_F(EdfSchedulerTest, Empty) {
  EdfScheduler<uint32_t> sched;
  EXPECT_EQ(nullptr, sched.peekAgain([](const double&) { return 0; }));
  EXPECT_EQ(nullptr, sched.pickAndAdd([](const double&) { return 0; }));
}

// Validate we get regular RR behavior when all weights are the same.
TEST_F(EdfSchedulerTest, Unweighted) {
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
TEST_F(EdfSchedulerTest, Weighted) {
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
TEST_F(EdfSchedulerTest, Expired) {
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
TEST_F(EdfSchedulerTest, ExpiredPeek) {
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
TEST_F(EdfSchedulerTest, ExpiredPeekedIsNotPicked) {
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

TEST_F(EdfSchedulerTest, ManyPeekahead) {
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

// Validates that creating a scheduler using the createWithPicks (with 0 picks)
// is equal to creating an empty scheduler and adding entries one after the other.
TEST_F(EdfSchedulerTest, SchedulerWithZeroPicksEqualToEmptyWithAddedEntries) {
  constexpr uint32_t num_entries = 128;
  std::vector<std::shared_ptr<uint32_t>> entries;
  entries.reserve(num_entries);

  // Populate sched1 one entry after the other.
  EdfScheduler<uint32_t> sched1;
  for (uint32_t i = 0; i < num_entries; ++i) {
    entries.emplace_back(std::make_shared<uint32_t>(i + 1));
    sched1.add(i + 1, entries.back());
  }

  EdfScheduler<uint32_t> sched2 = EdfScheduler<uint32_t>::createWithPicks(
      entries, [](const double& w) { return w; }, 0);

  compareEdfSchedulers(sched1, sched2);
}

// Validates that creating a scheduler using the createWithPicks (with 5 picks)
// is equal to creating an empty scheduler and adding entries one after the other,
// and then performing some number of picks.
TEST_F(EdfSchedulerTest, SchedulerWithSomePicksEqualToEmptyWithAddedEntries) {
  constexpr uint32_t num_entries = 128;
  // Use double-precision weights from the range [0.01, 100.5].
  // Using different weights to avoid a case where entries with the same weight
  // will be chosen in different order.
  std::vector<std::shared_ptr<double>> entries;
  entries.reserve(num_entries);
  for (uint32_t i = 0; i < num_entries; ++i) {
    const double entry_weight = (100.5 - 0.01) / num_entries * i + 0.01;
    entries.emplace_back(std::make_shared<double>(entry_weight));
  }

  const std::vector<uint32_t> all_picks{5, 140, 501, 123456, 894571};
  for (const auto picks : all_picks) {
    // Populate sched1 one entry after the other.
    EdfScheduler<double> sched1;
    for (uint32_t i = 0; i < num_entries; ++i) {
      sched1.add(*entries[i], entries[i]);
    }
    // Perform the picks on sched1.
    for (uint32_t i = 0; i < picks; ++i) {
      sched1.pickAndAdd([](const double& w) { return w; });
    }

    // Create sched2 with pre-built and pre-picked entries.
    EdfScheduler<double> sched2 = EdfScheduler<double>::createWithPicks(
        entries, [](const double& w) { return w; }, picks);

    compareEdfSchedulers(sched1, sched2);
  }
}

// Validating that calling `createWithPicks()` with no entries returns an empty
// scheduler.
TEST_F(EdfSchedulerTest, SchedulerWithSomePicksEmptyEntries) {
  EdfScheduler<double> sched = EdfScheduler<double>::createWithPicks(
      {}, [](const double& w) { return w; }, 123);
  EXPECT_EQ(nullptr, sched.peekAgain([](const double&) { return 0; }));
  EXPECT_EQ(nullptr, sched.pickAndAdd([](const double&) { return 0; }));
}

// Emulates first-pick scenarios by creating a scheduler with the given
// weights and a random number of pre-picks, and validates that the next pick
// of all the weights is close to the given weights.
void firstPickTest(const std::vector<double> weights) {
  TestRandomGenerator rand;
  ASSERT(std::accumulate(weights.begin(), weights.end(), 0.) == 100.0);
  // To be able to converge to the expected weights, a decent number of iterations
  // should be used. If the number of weights is large, the number of iterations
  // should be larger than 10000.
  constexpr uint64_t iterations = 4e5;
  // The expected range of the weights is [0,100). If this is no longer the
  // case, this value may need to be updated.
  constexpr double tolerance_pct = 1.0;

  // Set up the entries as simple integers.
  std::vector<std::shared_ptr<size_t>> entries;
  entries.reserve(weights.size());
  for (size_t i = 0; i < weights.size(); ++i) {
    entries.emplace_back(std::make_shared<size_t>(i));
  }

  absl::flat_hash_map<size_t, int> sched_picks;
  auto calc_weight = [&weights](const size_t& i) -> double { return weights[i]; };

  for (uint64_t i = 0; i < iterations; ++i) {
    // Create a scheduler with the given weights with a random number of
    // emulated pre-picks.
    uint32_t r = rand.random();
    auto sched = EdfScheduler<size_t>::createWithPicks(entries, calc_weight, r);

    // Perform a "first-pick" from that scheduler, and increase the counter for
    // that entry.
    sched_picks[*sched.pickAndAdd(calc_weight)]++;
  }

  // Validate that the observed distribution and expected weights are close.
  ASSERT_EQ(weights.size(), sched_picks.size());
  for (const auto& it : sched_picks) {
    const double expected = calc_weight(it.first);
    const double observed = 100 * static_cast<double>(it.second) / iterations;
    EXPECT_NEAR(expected, observed, tolerance_pct);
  }
}

// Validates that after creating schedulers using the createWithPicks (with random picks)
// and then performing a "first-pick", the distribution of the "first-picks" is
// equal to the weights.
TEST_F(EdfSchedulerTest, SchedulerWithRandomPicksFirstPickDistribution) {
  firstPickTest({25.0, 75.0});
  firstPickTest({1.0, 99.0});
  firstPickTest({50.0, 50.0});
  firstPickTest({1.0, 20.0, 79.0});
}

constexpr uint64_t BATCH_SIZE = 50;
static std::vector<uint64_t> picksStarts() {
  std::vector<uint64_t> start_idxs;
  // Add the first range, as it starts at 1 (and not 0).
  start_idxs.emplace_back(1);
  // The weight delta between iterations is 0.001, so to cover the range
  // from 0 to 100, the largest start_idx will be 100 / 0.001.
  for (uint64_t i = 50; i < 100 * 1000; i += BATCH_SIZE) {
    start_idxs.emplace_back(i);
  }
  return start_idxs;
}

class EdfSchedulerSpecialTest : public testing::TestWithParam<uint64_t> {};
// Validates that after creating schedulers using the createWithPicks (with random picks)
// and then performing a "first-pick", the distribution of the "first-picks" is
// equal to the weights. Trying the case of 2 weights between 0 to 100, in steps
// of 0.001. This test takes too long, and therefore it is disabled by default.
// If the EDF scheduler is enable, it can be manually executed.
TEST_P(EdfSchedulerSpecialTest, DISABLED_ExhaustiveValidator) {
  const uint64_t start_idx = GetParam();
  for (uint64_t i = start_idx; i < start_idx + BATCH_SIZE; ++i) {
    const double w1 = 0.001 * i;
    ENVOY_LOG_MISC(trace, "Testing weights: w1={}, w2={}", w1, 100.0 - w1);
    firstPickTest({w1, 100.0 - w1});
  }
}

INSTANTIATE_TEST_SUITE_P(ExhustiveValidator, EdfSchedulerSpecialTest,
                         testing::ValuesIn(picksStarts()));

} // namespace Upstream
} // namespace Envoy
