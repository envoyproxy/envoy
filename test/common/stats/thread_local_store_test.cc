#include "common/stats/thread_local_store.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Stats {

/**
 * This is a heap test allocator that works similar to how the shared memory allocator works in
 * terms of reference counting, etc.
 */
class TestAllocator : public RawStatDataAllocator {
public:
  ~TestAllocator() { EXPECT_TRUE(stats_.empty()); }

  RawStatData* alloc(const std::string& name) override {
    std::unique_ptr<RawStatData>& stat_ref = stats_[name];
    if (!stat_ref) {
      stat_ref.reset(new RawStatData());
      memset(stat_ref.get(), 0, sizeof(RawStatData));
      stat_ref->initialize(name);
    } else {
      stat_ref->ref_count_++;
    }

    return stat_ref.get();
  }

  void free(RawStatData& data) override {
    if (--data.ref_count_ > 0) {
      return;
    }

    for (auto i = stats_.begin(); i != stats_.end(); i++) {
      if (i->second.get() == &data) {
        stats_.erase(i);
        return;
      }
    }

    FAIL();
  }

private:
  std::unordered_map<std::string, std::unique_ptr<RawStatData>> stats_;
};

class StatsThreadLocalStoreTest : public testing::Test, public RawStatDataAllocator {
public:
  StatsThreadLocalStoreTest() {
    ON_CALL(*this, alloc(_))
        .WillByDefault(
            Invoke([this](const std::string& name) -> RawStatData* { return alloc_.alloc(name); }));

    ON_CALL(*this, free(_))
        .WillByDefault(Invoke([this](RawStatData& data) -> void { return alloc_.free(data); }));

    EXPECT_CALL(*this, alloc("stats.overflow"));
    store_.reset(new ThreadLocalStoreImpl(*this));
    store_->addSink(sink_);
  }

  CounterPtr findCounter(const std::string& name) {
    for (auto counter : store_->counters()) {
      if (counter->name() == name) {
        return counter;
      }
    }
    return nullptr;
  }

  GaugePtr findGauge(const std::string& name) {
    for (auto gauge : store_->gauges()) {
      if (gauge->name() == name) {
        return gauge;
      }
    }
    return nullptr;
  }

  MOCK_METHOD1(alloc, RawStatData*(const std::string& name));
  MOCK_METHOD1(free, void(RawStatData& data));

  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  TestAllocator alloc_;
  MockSink sink_;
  std::unique_ptr<ThreadLocalStoreImpl> store_;
};

TEST_F(StatsThreadLocalStoreTest, NoTls) {
  InSequence s;
  EXPECT_CALL(*this, alloc(_)).Times(2);

  Counter& c1 = store_->counter("c1");
  EXPECT_EQ(&c1, &store_->counter("c1"));

  Gauge& g1 = store_->gauge("g1");
  EXPECT_EQ(&g1, &store_->gauge("g1"));

  Timer& t1 = store_->timer("t1");
  EXPECT_EQ(&t1, &store_->timer("t1"));

  EXPECT_CALL(sink_, onHistogramComplete("h", 100));
  store_->deliverHistogramToSinks("h", 100);

  EXPECT_CALL(sink_, onTimespanComplete("t", std::chrono::milliseconds(200)));
  store_->deliverTimingToSinks("t", std::chrono::milliseconds(200));

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, store_->counters().front().get());
  EXPECT_EQ(2L, store_->counters().front().use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get());
  EXPECT_EQ(2L, store_->gauges().front().use_count());

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(3);

  store_->shutdownThreading();
}

TEST_F(StatsThreadLocalStoreTest, Tls) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*this, alloc(_)).Times(2);

  Counter& c1 = store_->counter("c1");
  EXPECT_EQ(&c1, &store_->counter("c1"));

  Gauge& g1 = store_->gauge("g1");
  EXPECT_EQ(&g1, &store_->gauge("g1"));

  Timer& t1 = store_->timer("t1");
  EXPECT_EQ(&t1, &store_->timer("t1"));

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, store_->counters().front().get());
  EXPECT_EQ(3L, store_->counters().front().use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get());
  EXPECT_EQ(3L, store_->gauges().front().use_count());

  store_->shutdownThreading();
  tls_.shutdownThread();

  EXPECT_EQ(2UL, store_->counters().size());
  EXPECT_EQ(&c1, store_->counters().front().get());
  EXPECT_EQ(2L, store_->counters().front().use_count());
  EXPECT_EQ(1UL, store_->gauges().size());
  EXPECT_EQ(&g1, store_->gauges().front().get());
  EXPECT_EQ(2L, store_->gauges().front().use_count());

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(3);
}

TEST_F(StatsThreadLocalStoreTest, BasicScope) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*this, alloc(_)).Times(4);
  Counter& c1 = store_->counter("c1");
  Counter& c2 = scope1->counter("c2");
  EXPECT_EQ("c1", c1.name());
  EXPECT_EQ("scope1.c2", c2.name());

  Gauge& g1 = store_->gauge("g1");
  Gauge& g2 = scope1->gauge("g2");
  EXPECT_EQ("g1", g1.name());
  EXPECT_EQ("scope1.g2", g2.name());

  Timer& t1 = store_->timer("t1");
  Timer& t2 = scope1->timer("t2");
  EXPECT_EQ("t1", t1.name());
  EXPECT_EQ("scope1.t2", t2.name());

  EXPECT_CALL(sink_, onHistogramComplete("scope1.h", 100));
  scope1->deliverHistogramToSinks("h", 100);

  EXPECT_CALL(sink_, onTimespanComplete("scope1.t", std::chrono::milliseconds(200)));
  scope1->deliverTimingToSinks("t", std::chrono::milliseconds(200));

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(5);
}

TEST_F(StatsThreadLocalStoreTest, ScopeDelete) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  ScopePtr scope1 = store_->createScope("scope1.");
  EXPECT_CALL(*this, alloc(_));
  scope1->counter("c1");
  EXPECT_EQ(2UL, store_->counters().size());
  CounterPtr c1 = store_->counters().front();
  EXPECT_EQ("scope1.c1", c1->name());

  EXPECT_CALL(main_thread_dispatcher_, post(_));
  EXPECT_CALL(tls_, runOnAllThreads(_));
  scope1.reset();
  EXPECT_EQ(1UL, store_->counters().size());

  EXPECT_CALL(*this, free(_));
  EXPECT_EQ(1L, c1.use_count());
  c1.reset();

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(main_thread_dispatcher_, post(_));
  EXPECT_CALL(*this, free(_));
}

TEST_F(StatsThreadLocalStoreTest, OverlappingScopes) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  // Both scopes point to the same namespace. This can happen during reload of a cluster for
  // example.
  ScopePtr scope1 = store_->createScope("scope1.");
  ScopePtr scope2 = store_->createScope("scope1.");

  // We will call alloc twice, but they should point to the same backing storage.
  EXPECT_CALL(*this, alloc(_)).Times(2);
  Counter& c1 = scope1->counter("c");
  Counter& c2 = scope2->counter("c");
  EXPECT_NE(&c1, &c2);
  c1.inc();
  EXPECT_EQ(1UL, c1.value());
  EXPECT_EQ(1UL, c2.value());
  c2.inc();
  EXPECT_EQ(2UL, c1.value());
  EXPECT_EQ(2UL, c2.value());

  // We should dedup when we fetch all counters to handle the overlapping case.
  EXPECT_EQ(2UL, store_->counters().size());

  // Gauges should work the same way.
  EXPECT_CALL(*this, alloc(_)).Times(2);
  Gauge& g1 = scope1->gauge("g");
  Gauge& g2 = scope2->gauge("g");
  EXPECT_NE(&g1, &g2);
  g1.set(5);
  EXPECT_EQ(5UL, g1.value());
  EXPECT_EQ(5UL, g2.value());
  g2.set(1);
  EXPECT_EQ(1UL, g1.value());
  EXPECT_EQ(1UL, g2.value());
  EXPECT_EQ(1UL, store_->gauges().size());

  // Deleting scope 1 will call free but will be reference counted. It still leaves scope 2 valid.
  EXPECT_CALL(*this, free(_)).Times(2);
  scope1.reset();
  c2.inc();
  EXPECT_EQ(3UL, c2.value());
  EXPECT_EQ(2UL, store_->counters().size());
  g2.set(10);
  EXPECT_EQ(10UL, g2.value());
  EXPECT_EQ(1UL, store_->gauges().size());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(3);
}

TEST_F(StatsThreadLocalStoreTest, AllocFailed) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*this, alloc("foo")).WillOnce(Return(nullptr));
  Counter& c1 = store_->counter("foo");
  EXPECT_EQ(1UL, store_->counter("stats.overflow").value());

  c1.inc();
  EXPECT_EQ(1UL, c1.value());

  store_->shutdownThreading();
  tls_.shutdownThread();

  // Includes overflow but not the failsafe stat which we allocated from the heap.
  EXPECT_CALL(*this, free(_));
}

TEST_F(StatsThreadLocalStoreTest, ShuttingDown) {
  InSequence s;
  store_->initializeThreading(main_thread_dispatcher_, tls_);

  EXPECT_CALL(*this, alloc(_)).Times(4);
  store_->counter("c1");
  store_->gauge("g1");
  store_->shutdownThreading();
  store_->counter("c2");
  store_->gauge("g2");

  // c1, g1 should have a thread local ref, but c2, g2 should not.
  EXPECT_EQ(3L, findCounter("c1").use_count());
  EXPECT_EQ(3L, findGauge("g1").use_count());
  EXPECT_EQ(2L, findCounter("c2").use_count());
  EXPECT_EQ(2L, findGauge("g2").use_count());

  tls_.shutdownThread();

  // Includes overflow stat.
  EXPECT_CALL(*this, free(_)).Times(5);
}

} // Stats
