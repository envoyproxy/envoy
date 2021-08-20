#include <string>

#include "envoy/stats/stats.h"

#include "source/common/stats/allocator_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class AllocatorImplTest : public testing::Test {
protected:
  AllocatorImplTest() : alloc_(symbol_table_), pool_(symbol_table_) {}
  ~AllocatorImplTest() override { clearStorage(); }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, symbol_table_);
  }

  StatName makeStat(absl::string_view name) { return pool_.add(name); }

  void clearStorage() {
    pool_.clear();
    EXPECT_EQ(0, symbol_table_.numSymbols());
  }

  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  StatNamePool pool_;
};

// Allocate 2 counters of the same name, and you'll get the same object.
TEST_F(AllocatorImplTest, CountersWithSameName) {
  StatName counter_name = makeStat("counter.name");
  CounterSharedPtr c1 = alloc_.makeCounter(counter_name, StatName(), {});
  EXPECT_EQ(1, c1->use_count());
  CounterSharedPtr c2 = alloc_.makeCounter(counter_name, StatName(), {});
  EXPECT_EQ(2, c1->use_count());
  EXPECT_EQ(2, c2->use_count());
  EXPECT_EQ(c1.get(), c2.get());
  EXPECT_FALSE(c1->used());
  EXPECT_FALSE(c2->used());
  c1->inc();
  EXPECT_TRUE(c1->used());
  EXPECT_TRUE(c2->used());
  c2->inc();
  EXPECT_EQ(2, c1->value());
  EXPECT_EQ(2, c2->value());
}

TEST_F(AllocatorImplTest, GaugesWithSameName) {
  StatName gauge_name = makeStat("gauges.name");
  GaugeSharedPtr g1 = alloc_.makeGauge(gauge_name, StatName(), {}, Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, g1->use_count());
  GaugeSharedPtr g2 = alloc_.makeGauge(gauge_name, StatName(), {}, Gauge::ImportMode::Accumulate);
  EXPECT_EQ(2, g1->use_count());
  EXPECT_EQ(2, g2->use_count());
  EXPECT_EQ(g1.get(), g2.get());
  EXPECT_FALSE(g1->used());
  EXPECT_FALSE(g2->used());
  g1->inc();
  EXPECT_TRUE(g1->used());
  EXPECT_TRUE(g2->used());
  EXPECT_EQ(1, g1->value());
  EXPECT_EQ(1, g2->value());
  g2->dec();
  EXPECT_EQ(0, g1->value());
  EXPECT_EQ(0, g2->value());
}

// Test for a race-condition where we may decrement the ref-count of a stat to
// zero at the same time as we are allocating another instance of that
// stat. This test reproduces that race organically by having a 12 threads each
// iterate 10k times.
TEST_F(AllocatorImplTest, RefCountDecAllocRaceOrganic) {
  StatName counter_name = makeStat("counter.name");
  StatName gauge_name = makeStat("gauge.name");
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();

  const uint32_t num_threads = 12;
  const uint32_t iters = 10000;
  std::vector<Thread::ThreadPtr> threads;
  absl::Notification go;
  for (uint32_t i = 0; i < num_threads; ++i) {
    threads.push_back(thread_factory.createThread([&]() {
      go.WaitForNotification();
      for (uint32_t i = 0; i < iters; ++i) {
        alloc_.makeCounter(counter_name, StatName(), {});
        alloc_.makeGauge(gauge_name, StatName(), {}, Gauge::ImportMode::NeverImport);
      }
    }));
  }
  go.Notify();
  for (uint32_t i = 0; i < num_threads; ++i) {
    threads[i]->join();
  }
}

// Tests the same scenario as RefCountDecAllocRaceOrganic, but using just two
// threads and the ThreadSynchronizer, in one iteration. Note that if the code
// has the bug in it, this test fails fast as expected. However, if the bug is
// fixed, the allocator's mutex will cause the second thread to block in
// makeCounter() until the first thread finishes destructing the object. Thus
// the test gives thread2 5 seconds to complete before releasing thread 1 to
// complete its destruction of the counter.
TEST_F(AllocatorImplTest, RefCountDecAllocRaceSynchronized) {
  StatName counter_name = makeStat("counter.name");
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();
  alloc_.sync().enable();
  alloc_.sync().waitOn(AllocatorImpl::DecrementToZeroSyncPoint);
  Thread::ThreadPtr thread = thread_factory.createThread([&]() {
    CounterSharedPtr counter = alloc_.makeCounter(counter_name, StatName(), {});
    counter->inc();
    counter->reset(); // Blocks in thread synchronizer waiting on DecrementToZeroSyncPoint
  });

  alloc_.sync().barrierOn(AllocatorImpl::DecrementToZeroSyncPoint);
  EXPECT_TRUE(alloc_.isMutexLockedForTest());
  alloc_.sync().signal(AllocatorImpl::DecrementToZeroSyncPoint);
  thread->join();
  EXPECT_FALSE(alloc_.isMutexLockedForTest());
}

TEST_F(AllocatorImplTest, ForEachSinkedCounter) {

  StatNameHashSet sinked_stat_names;
  std::vector<CounterSharedPtr> sinked_counters;
  std::vector<CounterSharedPtr> unsinked_counters;

  alloc_.setCounterSinkFilter([&sinked_stat_names](const Stats::Counter& counter) {
    if (sinked_stat_names.count(counter.statName()) == 1) {
      return true;
    }
    return false;
  });

  size_t n_stats = 11;

  for (size_t idx = 0; idx < n_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("counter.", idx));
    // sink every 3rd stat
    if ((idx + 1) % 3 == 0) {
      sinked_stat_names.insert(stat_name);
      sinked_counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
    } else {
      unsinked_counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
    }
  }

  size_t n_sinked_counters = 0;
  size_t n_iterations = 0;
  alloc_.forEachSinkedCounter([&n_sinked_counters](std::size_t size) { n_sinked_counters = size; },
                              [&n_iterations, &sinked_stat_names](Stats::Counter& counter) {
                                EXPECT_EQ(sinked_stat_names.count(counter.statName()), 1);
                                ++n_iterations;
                              });
  EXPECT_EQ(n_sinked_counters, 3);

  // Erase all sinked stats.
  sinked_counters.clear();
  n_iterations = 0;
  alloc_.forEachSinkedCounter([&n_sinked_counters](std::size_t size) { n_sinked_counters = size; },
                              [&n_iterations](Stats::Counter&) { ++n_iterations; });
  EXPECT_EQ(n_sinked_counters, 0);
  EXPECT_EQ(n_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachSinkedGauge) {

  StatNameHashSet sinked_stat_names;
  std::vector<GaugeSharedPtr> sinked_gauges;
  std::vector<GaugeSharedPtr> unsinked_gauges;

  alloc_.setGaugeSinkFilter([&sinked_stat_names](const Stats::Gauge& gauge) {
    if (sinked_stat_names.count(gauge.statName()) == 1) {
      return true;
    }
    return false;
  });

  size_t n_stats = 11;

  for (size_t idx = 0; idx < n_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("gauge.", idx));
    // sink every 5th stat
    if ((idx + 1) % 5 == 0) {
      sinked_stat_names.insert(stat_name);
      sinked_gauges.emplace_back(
          alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
    } else {
      unsinked_gauges.emplace_back(
          alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
    }
  }

  size_t n_sinked_gauges = 0;
  size_t n_iterations = 0;
  alloc_.forEachSinkedGauge([&n_sinked_gauges](std::size_t size) { n_sinked_gauges = size; },
                            [&n_iterations, &sinked_stat_names](Stats::Gauge& gauge) {
                              EXPECT_EQ(sinked_stat_names.count(gauge.statName()), 1);
                              ++n_iterations;
                            });
  EXPECT_EQ(n_sinked_gauges, 2);

  // Erase all sinked stats.
  sinked_gauges.clear();
  n_iterations = 0;
  alloc_.forEachSinkedGauge([&n_sinked_gauges](std::size_t size) { n_sinked_gauges = size; },
                            [&n_iterations](Stats::Gauge&) { ++n_iterations; });
  EXPECT_EQ(n_sinked_gauges, 0);
  EXPECT_EQ(n_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachSinkedTextReadout) {

  StatNameHashSet sinked_stat_names;
  std::vector<TextReadoutSharedPtr> sinked_text_readouts;
  std::vector<TextReadoutSharedPtr> unsinked_text_readouts;

  alloc_.setTextReadoutSinkFilter([&sinked_stat_names](const Stats::TextReadout& text_readout) {
    if (sinked_stat_names.count(text_readout.statName()) == 1) {
      return true;
    }
    return false;
  });

  size_t n_stats = 11;

  for (size_t idx = 0; idx < n_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    // sink every 2nd stat
    if ((idx + 1) % 2 == 0) {
      sinked_stat_names.insert(stat_name);
      sinked_text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
    } else {
      unsinked_text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
    }
  }

  size_t n_sinked_text_readouts = 0;
  size_t n_iterations = 0;
  alloc_.forEachSinkedTextReadout(
      [&n_sinked_text_readouts](std::size_t size) { n_sinked_text_readouts = size; },
      [&n_iterations, &sinked_stat_names](Stats::TextReadout& text_readout) {
        EXPECT_EQ(sinked_stat_names.count(text_readout.statName()), 1);
        ++n_iterations;
      });
  EXPECT_EQ(n_sinked_text_readouts, 5);

  // Erase all sinked stats.
  sinked_text_readouts.clear();
  n_iterations = 0;
  alloc_.forEachSinkedTextReadout(
      [&n_sinked_text_readouts](std::size_t size) { n_sinked_text_readouts = size; },
      [&n_iterations](Stats::TextReadout&) { ++n_iterations; });
  EXPECT_EQ(n_sinked_text_readouts, 0);
  EXPECT_EQ(n_iterations, 0);
}

} // namespace
} // namespace Stats
} // namespace Envoy
