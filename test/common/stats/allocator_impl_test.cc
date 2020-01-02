#include <string>

#include "common/stats/allocator_impl.h"
#include "common/stats/symbol_table_creator.h"

#include "test/test_common/logging.h"
#include "test/test_common/thread_factory_for_test.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class AllocatorImplTest : public testing::Test {
protected:
  AllocatorImplTest()
      : symbol_table_(SymbolTableCreator::makeSymbolTable()), alloc_(*symbol_table_),
        pool_(*symbol_table_) {}
  ~AllocatorImplTest() override { clearStorage(); }

  StatNameStorage makeStatStorage(absl::string_view name) {
    return StatNameStorage(name, *symbol_table_);
  }

  StatName makeStat(absl::string_view name) { return pool_.add(name); }

  void clearStorage() {
    pool_.clear();
    EXPECT_EQ(0, symbol_table_->numSymbols());
  }

  SymbolTablePtr symbol_table_;
  AllocatorImpl alloc_;
  StatNamePool pool_;
};

// Allocate 2 counters of the same name, and you'll get the same object.
TEST_F(AllocatorImplTest, CountersWithSameName) {
  StatName counter_name = makeStat("counter.name");
  CounterSharedPtr c1 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
  EXPECT_EQ(1, c1->use_count());
  CounterSharedPtr c2 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
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
  GaugeSharedPtr g1 =
      alloc_.makeGauge(gauge_name, "", std::vector<Tag>(), Gauge::ImportMode::Accumulate);
  EXPECT_EQ(1, g1->use_count());
  GaugeSharedPtr g2 =
      alloc_.makeGauge(gauge_name, "", std::vector<Tag>(), Gauge::ImportMode::Accumulate);
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

TEST_F(AllocatorImplTest, Threads1) {
  StatName counter_name = makeStat("counter.name");
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();

  const uint32_t num_threads = 12;
  std::vector<Thread::ThreadPtr> threads;
  std::atomic<bool> cont(true);
  std::atomic<uint64_t> count(0);
  for (uint32_t i = 0; i < num_threads; ++i) {
    threads.push_back(thread_factory.createThread([this, &counter_name, &cont, &count]() {
      while (cont) {
        CounterSharedPtr c1 = alloc_.makeCounter(counter_name, "", std::vector<Tag>());
        ++count;
      }
    }));
  }
  sleep(5);
  cont = false;
  for (uint32_t i = 0; i < num_threads; ++i) {
    threads[i]->join();
  }
  ENVOY_LOG_MISC(error, "Count={}", count);
}

/*
TEST(RefcountPtr, Threads2) {
  Thread::ThreadFactory& thread_factory = Thread::threadFactoryForTest();

  const uint32_t num_threads = 20;
  const uint32_t num_iters = 200;

  for (uint32_t j = 0; j < num_iters; ++j) {
    RefcountedString* ptr = new RefcountedString("Hello, World!");
    Thread::ThreadPtr threads[num_threads];
    std::unique_ptr<SharedString> strings[num_threads];

    for (uint32_t i = 0; i < num_threads; ++i) {
      strings[i] = std::make_unique<SharedString>(ptr);
    }
    absl::Notification go;
    for (uint32_t i = 0; i < num_threads; ++i) {
      threads[i] = thread_factory.createThread([&strings, i, &go]() {
        go.WaitForNotification();
        strings[i].reset();
      });
    }
    go.Notify();
    for (uint32_t i = 0; i < num_threads; ++i) {
      threads[i]->join();
    }
  }
}
*/

} // namespace
} // namespace Stats
} // namespace Envoy
