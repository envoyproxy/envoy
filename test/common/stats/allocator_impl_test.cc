#include <cmath>
#include <memory>
#include <string>

#include "envoy/stats/sink.h"

#include "source/common/stats/allocator_impl.h"

#include "test/test_common/logging.h"
#include "test/test_common/thread_factory_for_test.h"

#include "absl/synchronization/notification.h"
#include "gmock/gmock-matchers.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

constexpr size_t num_stats = 11;

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
    // If stats have been marked for deletion, they are not cleared until the
    // destructor of alloc_ is called, and hence the symbol_table_.numSymbols()
    // will be greater than zero at this point.
    if (!are_stats_marked_for_deletion_) {
      EXPECT_EQ(0, symbol_table_.numSymbols());
    }
  }

  std::vector<CounterSharedPtr> makeCounters() {
    std::vector<CounterSharedPtr> counters;
    for (size_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = makeStat(absl::StrCat("counter.", idx));
      counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
    }
    return counters;
  }

  std::vector<GaugeSharedPtr> makeGauges() {
    std::vector<GaugeSharedPtr> gauges;
    for (size_t idx = 0; idx < num_stats; ++idx) {
      auto stat_name = makeStat(absl::StrCat("gauge.", idx));
      gauges.emplace_back(
          alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
    }
    return gauges;
  }

  template <class Metrics> StatNameHashSet collectStatNames(const Metrics& metrics) {
    StatNameHashSet stat_names;
    for (auto& metric : metrics) {
      stat_names.insert(metric->statName());
    }
    return stat_names;
  }

  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_;
  StatNamePool pool_;
  bool are_stats_marked_for_deletion_ = false;
};

class TestSinkPredicates : public SinkPredicates {
public:
  ~TestSinkPredicates() override = default;
  StatNameHashSet& sinkedStatNames() { return sinked_stat_names_; }

  // SinkPredicates
  bool includeCounter(const Counter& counter) override {
    return sinked_stat_names_.find(counter.statName()) != sinked_stat_names_.end();
  }
  bool includeGauge(const Gauge& gauge) override {
    return sinked_stat_names_.find(gauge.statName()) != sinked_stat_names_.end();
  }
  bool includeTextReadout(const TextReadout& text_readout) override {
    return sinked_stat_names_.find(text_readout.statName()) != sinked_stat_names_.end();
  }

private:
  StatNameHashSet sinked_stat_names_;
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

TEST_F(AllocatorImplTest, ForEachCounter) {
  std::vector<CounterSharedPtr> counters = makeCounters();
  StatNameHashSet stat_names = collectStatNames(counters);

  size_t num_counters = 0;
  size_t num_iterations = 0;
  alloc_.forEachCounter([&num_counters](std::size_t size) { num_counters = size; },
                        [&num_iterations, &stat_names](Stats::Counter& counter) -> bool {
                          EXPECT_EQ(stat_names.count(counter.statName()), 1);
                          ++num_iterations;
                          return true;
                        });
  EXPECT_EQ(num_counters, 11);
  EXPECT_EQ(num_iterations, 11);

  // Reject a stat and remove it from "scope".
  StatName rejected_stat_name = counters[4]->statName();
  alloc_.markCounterForDeletion(counters[4]);
  are_stats_marked_for_deletion_ = true;
  // Save a local reference to rejected stat.
  Counter& rejected_counter = *counters[4];
  counters.erase(counters.begin() + 4);

  // Verify that the rejected stat does not show up during iteration.
  num_iterations = 0;
  num_counters = 0;
  alloc_.forEachCounter([&num_counters](std::size_t size) { num_counters = size; },
                        [&num_iterations, &rejected_stat_name](Stats::Counter& counter) -> bool {
                          EXPECT_THAT(counter.statName(), ::testing::Ne(rejected_stat_name));
                          ++num_iterations;
                          return true;
                        });
  EXPECT_EQ(num_iterations, 10);
  EXPECT_EQ(num_counters, 10);

  // Verify that we can access the local reference without a crash.
  rejected_counter.inc();

  // Erase all stats.
  counters.clear();
  num_iterations = 0;
  alloc_.forEachCounter([&num_counters](std::size_t size) { num_counters = size; },
                        [&num_iterations](Stats::Counter&) -> bool {
                          ++num_iterations;
                          return true;
                        });
  EXPECT_EQ(num_counters, 0);
  EXPECT_EQ(num_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachGauge) {
  std::vector<GaugeSharedPtr> gauges = makeGauges();
  StatNameHashSet stat_names = collectStatNames(gauges);

  size_t num_gauges = 0;
  size_t num_iterations = 0;
  alloc_.forEachGauge([&num_gauges](std::size_t size) { num_gauges = size; },
                      [&num_iterations, &stat_names](Stats::Gauge& gauge) -> bool {
                        EXPECT_EQ(stat_names.count(gauge.statName()), 1);
                        ++num_iterations;
                        return true;
                      });
  EXPECT_EQ(num_gauges, 11);
  EXPECT_EQ(num_iterations, 11);

  // Reject a stat and remove it from "scope".
  StatName rejected_stat_name = gauges[3]->statName();
  alloc_.markGaugeForDeletion(gauges[3]);
  are_stats_marked_for_deletion_ = true;
  // Save a local reference to rejected stat.
  Gauge& rejected_gauge = *gauges[3];
  gauges.erase(gauges.begin() + 3);

  // Verify that the rejected stat does not show up during iteration.
  num_iterations = 0;
  num_gauges = 0;
  alloc_.forEachGauge([&num_gauges](std::size_t size) { num_gauges = size; },
                      [&num_iterations, &rejected_stat_name](Stats::Gauge& gauge) -> bool {
                        EXPECT_THAT(gauge.statName(), ::testing::Ne(rejected_stat_name));
                        ++num_iterations;
                        return true;
                      });
  EXPECT_EQ(num_iterations, 10);
  EXPECT_EQ(num_gauges, 10);

  // Verify that we can access the local reference without a crash.
  rejected_gauge.inc();

  // Erase all stats.
  gauges.clear();
  num_iterations = 0;
  alloc_.forEachGauge([&num_gauges](std::size_t size) { num_gauges = size; },
                      [&num_iterations](Stats::Gauge&) -> bool {
                        ++num_iterations;
                        return true;
                      });
  EXPECT_EQ(num_gauges, 0);
  EXPECT_EQ(num_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachTextReadout) {
  StatNameHashSet stat_names;
  std::vector<TextReadoutSharedPtr> text_readouts;

  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    stat_names.insert(stat_name);
    text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
  }

  size_t num_text_readouts = 0;
  size_t num_iterations = 0;
  alloc_.forEachTextReadout(
      [&num_text_readouts](std::size_t size) { num_text_readouts = size; },
      [&num_iterations, &stat_names](Stats::TextReadout& text_readout) -> bool {
        EXPECT_EQ(stat_names.count(text_readout.statName()), 1);
        ++num_iterations;
        return true;
      });
  EXPECT_EQ(num_text_readouts, 11);
  EXPECT_EQ(num_iterations, 11);

  // Reject a stat and remove it from "scope".
  StatName rejected_stat_name = text_readouts[4]->statName();
  alloc_.markTextReadoutForDeletion(text_readouts[4]);
  are_stats_marked_for_deletion_ = true;
  // Save a local reference to rejected stat.
  TextReadout& rejected_text_readout = *text_readouts[4];
  text_readouts.erase(text_readouts.begin() + 4);

  // Verify that the rejected stat does not show up during iteration.
  num_iterations = 0;
  num_text_readouts = 0;
  alloc_.forEachTextReadout(
      [&num_text_readouts](std::size_t size) { num_text_readouts = size; },
      [&num_iterations, &rejected_stat_name](Stats::TextReadout& text_readout) -> bool {
        EXPECT_THAT(text_readout.statName(), ::testing::Ne(rejected_stat_name));
        ++num_iterations;
        return true;
      });
  EXPECT_EQ(num_iterations, 10);
  EXPECT_EQ(num_text_readouts, 10);

  // Verify that we can access the local reference without a crash.
  rejected_text_readout.set("no crash");

  // Erase all stats.
  text_readouts.clear();
  num_iterations = 0;
  alloc_.forEachTextReadout([&num_text_readouts](std::size_t size) { num_text_readouts = size; },
                            [&num_iterations](Stats::TextReadout&) -> bool {
                              ++num_iterations;
                              return true;
                            });
  EXPECT_EQ(num_text_readouts, 0);
  EXPECT_EQ(num_iterations, 0);
}

// Verify that we don't crash if a nullptr is passed in for the size lambda for
// the for each stat methods.
TEST_F(AllocatorImplTest, ForEachWithNullSizeLambda) {
  std::vector<CounterSharedPtr> counters;
  std::vector<TextReadoutSharedPtr> text_readouts;
  std::vector<GaugeSharedPtr> gauges;

  const size_t num_stats = 3;

  // For each counter.
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("counter.", idx));
    counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
  }
  size_t num_iterations = 0;
  alloc_.forEachCounter(nullptr, [&num_iterations](Stats::Counter& counter) -> bool {
    UNREFERENCED_PARAMETER(counter);
    ++num_iterations;
    return true;
  });
  EXPECT_EQ(num_iterations, num_stats);

  // For each gauge.
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("gauge.", idx));
    gauges.emplace_back(alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
  }
  num_iterations = 0;
  alloc_.forEachGauge(nullptr, [&num_iterations](Stats::Gauge& gauge) -> bool {
    UNREFERENCED_PARAMETER(gauge);
    ++num_iterations;
    return true;
  });
  EXPECT_EQ(num_iterations, num_stats);

  // For each text readout.
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
  }
  num_iterations = 0;
  alloc_.forEachTextReadout(nullptr, [&num_iterations](Stats::TextReadout& text_readout) -> bool {
    UNREFERENCED_PARAMETER(text_readout);
    ++num_iterations;
    return true;
  });
  EXPECT_EQ(num_iterations, num_stats);
}

// Currently, if we ask for a stat from the Allocator that has already been
// marked for deletion (i.e. rejected) we get a new stat with the same name.
// This test documents this behavior.
TEST_F(AllocatorImplTest, AskForDeletedStat) {
  const size_t num_stats = 10;
  are_stats_marked_for_deletion_ = true;

  std::vector<CounterSharedPtr> counters = makeCounters();

  // Reject a stat and remove it from "scope".
  StatName const rejected_counter_name = counters[4]->statName();
  alloc_.markCounterForDeletion(counters[4]);
  // Save a local reference to rejected stat.
  Counter& rejected_counter = *counters[4];
  counters.erase(counters.begin() + 4);

  rejected_counter.inc();
  rejected_counter.inc();

  // Make the deleted stat again.
  CounterSharedPtr deleted_counter = alloc_.makeCounter(rejected_counter_name, StatName(), {});

  EXPECT_EQ(deleted_counter->value(), 0);
  EXPECT_EQ(rejected_counter.value(), 2);

  std::vector<GaugeSharedPtr> gauges = makeGauges();

  // Reject a stat and remove it from "scope".
  StatName const rejected_gauge_name = gauges[4]->statName();
  alloc_.markGaugeForDeletion(gauges[4]);
  // Save a local reference to rejected stat.
  Gauge& rejected_gauge = *gauges[4];
  gauges.erase(gauges.begin() + 4);

  rejected_gauge.set(10);

  // Make the deleted stat again.
  GaugeSharedPtr deleted_gauge =
      alloc_.makeGauge(rejected_gauge_name, StatName(), {}, Gauge::ImportMode::Accumulate);

  EXPECT_EQ(deleted_gauge->value(), 0);
  EXPECT_EQ(rejected_gauge.value(), 10);

  std::vector<TextReadoutSharedPtr> text_readouts;
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
  }
  // Reject a stat and remove it from "scope".
  StatName const rejected_text_readout_name = text_readouts[4]->statName();
  alloc_.markTextReadoutForDeletion(text_readouts[4]);
  // Save a local reference to rejected stat.
  TextReadout& rejected_text_readout = *text_readouts[4];
  text_readouts.erase(text_readouts.begin() + 4);

  rejected_text_readout.set("deleted value");

  // Make the deleted stat again.
  TextReadoutSharedPtr deleted_text_readout =
      alloc_.makeTextReadout(rejected_text_readout_name, StatName(), {});

  EXPECT_EQ(deleted_text_readout->value(), "");
  EXPECT_EQ(rejected_text_readout.value(), "deleted value");
}

TEST_F(AllocatorImplTest, ForEachSinkedCounter) {
  std::unique_ptr<TestSinkPredicates> moved_sink_predicates =
      std::make_unique<TestSinkPredicates>();
  TestSinkPredicates* sink_predicates = moved_sink_predicates.get();
  std::vector<CounterSharedPtr> sinked_counters;
  std::vector<CounterSharedPtr> unsinked_counters;

  alloc_.setSinkPredicates(std::move(moved_sink_predicates));

  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("counter.", idx));
    // sink every 3rd stat
    if ((idx + 1) % 3 == 0) {
      sink_predicates->sinkedStatNames().insert(stat_name);
      sinked_counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
    } else {
      unsinked_counters.emplace_back(alloc_.makeCounter(stat_name, StatName(), {}));
    }
  }

  EXPECT_EQ(sinked_counters.size(), 3);
  EXPECT_EQ(unsinked_counters.size(), 8);

  size_t num_sinked_counters = 0;
  size_t num_iterations = 0;
  alloc_.forEachSinkedCounter(
      [&num_sinked_counters](std::size_t size) { num_sinked_counters = size; },
      [&num_iterations, sink_predicates](Stats::Counter& counter) -> bool {
        EXPECT_EQ(sink_predicates->sinkedStatNames().count(counter.statName()), 1);
        ++num_iterations;
        return true;
      });
  EXPECT_EQ(num_sinked_counters, 3);
  EXPECT_EQ(num_iterations, 3);

  // Erase all sinked stats.
  sinked_counters.clear();
  num_iterations = 0;
  alloc_.forEachSinkedCounter(
      [&num_sinked_counters](std::size_t size) { num_sinked_counters = size; },
      [&num_iterations](Stats::Counter&) -> bool {
        ++num_iterations;
        return true;
      });
  EXPECT_EQ(num_sinked_counters, 0);
  EXPECT_EQ(num_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachSinkedGauge) {
  std::unique_ptr<TestSinkPredicates> moved_sink_predicates =
      std::make_unique<TestSinkPredicates>();
  TestSinkPredicates* sink_predicates = moved_sink_predicates.get();
  std::vector<GaugeSharedPtr> sinked_gauges;
  std::vector<GaugeSharedPtr> unsinked_gauges;

  alloc_.setSinkPredicates(std::move(moved_sink_predicates));
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("gauge.", idx));
    // sink every 5th stat
    if ((idx + 1) % 5 == 0) {
      sink_predicates->sinkedStatNames().insert(stat_name);
      sinked_gauges.emplace_back(
          alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
    } else {
      unsinked_gauges.emplace_back(
          alloc_.makeGauge(stat_name, StatName(), {}, Gauge::ImportMode::Accumulate));
    }
  }

  EXPECT_EQ(sinked_gauges.size(), 2);
  EXPECT_EQ(unsinked_gauges.size(), 9);

  size_t num_sinked_gauges = 0;
  size_t num_iterations = 0;
  alloc_.forEachSinkedGauge([&num_sinked_gauges](std::size_t size) { num_sinked_gauges = size; },
                            [&num_iterations, sink_predicates](Stats::Gauge& gauge) -> bool {
                              EXPECT_EQ(sink_predicates->sinkedStatNames().count(gauge.statName()),
                                        1);
                              ++num_iterations;
                              return true;
                            });
  EXPECT_EQ(num_sinked_gauges, 2);
  EXPECT_EQ(num_iterations, 2);

  // Erase all sinked stats.
  sinked_gauges.clear();
  num_iterations = 0;
  alloc_.forEachSinkedGauge([&num_sinked_gauges](std::size_t size) { num_sinked_gauges = size; },
                            [&num_iterations](Stats::Gauge&) -> bool {
                              ++num_iterations;
                              return true;
                            });
  EXPECT_EQ(num_sinked_gauges, 0);
  EXPECT_EQ(num_iterations, 0);
}

TEST_F(AllocatorImplTest, ForEachSinkedTextReadout) {
  std::unique_ptr<TestSinkPredicates> moved_sink_predicates =
      std::make_unique<TestSinkPredicates>();
  TestSinkPredicates* sink_predicates = moved_sink_predicates.get();
  std::vector<TextReadoutSharedPtr> sinked_text_readouts;
  std::vector<TextReadoutSharedPtr> unsinked_text_readouts;

  alloc_.setSinkPredicates(std::move(moved_sink_predicates));
  for (size_t idx = 0; idx < num_stats; ++idx) {
    auto stat_name = makeStat(absl::StrCat("text_readout.", idx));
    // sink every 2nd stat
    if ((idx + 1) % 2 == 0) {
      sink_predicates->sinkedStatNames().insert(stat_name);
      sinked_text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
    } else {
      unsinked_text_readouts.emplace_back(alloc_.makeTextReadout(stat_name, StatName(), {}));
    }
  }

  EXPECT_EQ(sinked_text_readouts.size(), 5);
  EXPECT_EQ(unsinked_text_readouts.size(), 6);

  size_t num_sinked_text_readouts = 0;
  size_t num_iterations = 0;
  alloc_.forEachSinkedTextReadout(
      [&num_sinked_text_readouts](std::size_t size) { num_sinked_text_readouts = size; },
      [&num_iterations, sink_predicates](Stats::TextReadout& text_readout) -> bool {
        EXPECT_EQ(sink_predicates->sinkedStatNames().count(text_readout.statName()), 1);
        ++num_iterations;
        return true;
      });
  EXPECT_EQ(num_sinked_text_readouts, 5);
  EXPECT_EQ(num_iterations, 5);

  // Erase all sinked stats.
  sinked_text_readouts.clear();
  num_iterations = 0;
  alloc_.forEachSinkedTextReadout(
      [&num_sinked_text_readouts](std::size_t size) { num_sinked_text_readouts = size; },
      [&num_iterations](Stats::TextReadout&) -> bool {
        ++num_iterations;
        return true;
      });
  EXPECT_EQ(num_sinked_text_readouts, 0);
  EXPECT_EQ(num_iterations, 0);
}

#if 0
TEST_F(AllocatorImplTest, CounterPageForward) {
  std::vector<CounterSharedPtr> counters = makeCounters();
  constexpr size_t page_size = 5;
  uint32_t page_index = 0;

  auto test_page = [this, &page_index](absl::string_view start,
                                       std::vector<uint32_t> suffixes) -> bool {
    ++page_index;
    std::vector<Counter*> page;
    bool ret = alloc_.counterPage(
        [&page](Counter& counter) {
          page.push_back(&counter);
          return page.size() < page_size;
        },
        start, PageDirection::Forward);
    EXPECT_EQ(suffixes.size(), page.size());
    uint32_t i = 0;
    for (uint32_t suffix : suffixes) {
      if (i == page.size()) {
        break;
      }
      EXPECT_EQ(absl::StrCat("counter.", suffix), page[i++]->name()) << "Page: " << page_index;
    }
    return ret;
  };

  EXPECT_TRUE(test_page("", {0, 1, 10, 2, 3}));
  EXPECT_TRUE(test_page("a", {0, 1, 10, 2, 3}));
  EXPECT_TRUE(test_page("counter.3x", {4, 5, 6, 7, 8}));
  EXPECT_FALSE(test_page("counter.4", {5, 6, 7, 8, 9}));
  EXPECT_FALSE(test_page("counter.8x", {9}));
  EXPECT_FALSE(test_page("counter.9", {}));
}

TEST_F(AllocatorImplTest, CounterPageBackward) {
  std::vector<CounterSharedPtr> counters = makeCounters();
  constexpr size_t page_size = 5;
  uint32_t page_index = 0;

  auto test_page = [this, &page_index](absl::string_view start,
                                       std::vector<uint32_t> suffixes) -> bool {
    ++page_index;
    std::vector<Counter*> page;
    bool ret = alloc_.counterPage(
        [&page](Counter& counter) {
          page.push_back(&counter);
          return page.size() < page_size;
        },
        start, PageDirection::Backward);
    EXPECT_EQ(suffixes.size(), page.size());
    uint32_t i = 0;
    for (uint32_t suffix : suffixes) {
      if (i == page.size()) {
        break;
      }
      EXPECT_EQ(absl::StrCat("counter.", suffix), page[i++]->name()) << "Page: " << page_index;
    }
    return ret;
  };

  EXPECT_TRUE(test_page("", {9, 8, 7, 6, 5}));
  EXPECT_TRUE(test_page("z", {9, 8, 7, 6, 5}));
  EXPECT_TRUE(test_page("counter.5", {4, 3, 2, 10, 1}));
  EXPECT_TRUE(test_page("counter.4x", {4, 3, 2, 10, 1}));
  EXPECT_FALSE(test_page("counter.1", {0}));
  EXPECT_FALSE(test_page("counter.0x", {0}));
}
#endif

} // namespace
} // namespace Stats
} // namespace Envoy
