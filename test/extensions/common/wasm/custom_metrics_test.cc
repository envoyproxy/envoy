#include <limits>

#include "source/common/stats/allocator.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/thread_local_store.h"
#include "source/extensions/common/wasm/wasm.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using proxy_wasm::MetricType;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Wasm {
namespace {

// Tests the host side of define_metric, increment_metric, record_metric and get_metric against a
// real ThreadLocalStoreImpl, so that eviction runs as in a server. ThreadLocal::MockInstance runs
// the thread local part of an eviction pass synchronously.
class CustomMetricsTest : public testing::Test, public Event::TestUsingSimulatedTime {
protected:
  CustomMetricsTest()
      : alloc_(symbol_table_), store_(std::make_unique<Stats::ThreadLocalStoreImpl>(alloc_)) {
    store_->initializeThreading(main_thread_dispatcher_, tls_);
    api_ = Api::createApiForTest(*store_, simTime());
    dispatcher_ = api_->allocateDispatcher("custom_metrics_test");
    // A prefixed parent scope, as filters may use, to check that lookups use the full name.
    scope_ = store_->createScope("prefix.");
  }

  ~CustomMetricsTest() override {
    contexts_.clear();
    wasms_.clear();
    scope_.reset();
    tls_.shutdownGlobalThreading();
    store_->shutdownThreading();
    tls_.shutdownThread();
  }

  void createWasm(const std::string& custom_metrics_yaml = "") {
    envoy::extensions::wasm::v3::PluginConfig plugin_config;
    plugin_config.mutable_vm_config()->set_runtime("envoy.wasm.runtime.null");
    if (!custom_metrics_yaml.empty()) {
      TestUtility::loadFromYaml(custom_metrics_yaml,
                                *plugin_config.mutable_vm_config()->mutable_custom_metrics());
    }
    plugin_ = std::make_shared<Plugin>(plugin_config, local_info_);
    auto wasm = std::make_shared<Wasm>(plugin_->wasmConfig(), "", scope_, *api_, cluster_manager_,
                                       *dispatcher_);
    addWasm(wasm);
  }

  // Creates a clone of the first VM, as is done for each worker thread.
  void cloneWasm() {
    auto clone = std::make_shared<Wasm>(std::make_shared<WasmHandle>(wasms_.front()), *dispatcher_);
    addWasm(clone);
  }

  void addWasm(const WasmSharedPtr& wasm) {
    wasms_.push_back(wasm);
    contexts_.emplace_back(static_cast<Context*>(wasm->createRootContext(plugin_)));
  }

  uint32_t define(MetricType type, absl::string_view name, size_t vm = 0) {
    uint32_t id = 0;
    EXPECT_EQ(WasmResult::Ok, contexts_[vm]->defineMetric(static_cast<uint32_t>(type), name, &id));
    return id;
  }
  void increment(uint32_t id, int64_t offset, size_t vm = 0) {
    EXPECT_EQ(WasmResult::Ok, contexts_[vm]->incrementMetric(id, offset));
  }
  void record(uint32_t id, uint64_t value, size_t vm = 0) {
    EXPECT_EQ(WasmResult::Ok, contexts_[vm]->recordMetric(id, value));
  }
  uint64_t get(uint32_t id, size_t vm = 0) {
    uint64_t value = 0;
    EXPECT_EQ(WasmResult::Ok, contexts_[vm]->getMetric(id, &value));
    return value;
  }

  static std::string fullName(absl::string_view name) {
    return absl::StrCat("prefix.wasmcustom.", name);
  }
  // These look up the store, not a scope, so they find any live metric with the name.
  Stats::CounterSharedPtr counter(absl::string_view name) {
    return TestUtility::findCounter(*store_, fullName(name));
  }
  Stats::GaugeSharedPtr gauge(absl::string_view name) {
    return TestUtility::findGauge(*store_, fullName(name));
  }
  bool hasHistogram(absl::string_view name) {
    for (const auto& histogram : store_->histograms()) {
      if (histogram->name() == fullName(name)) {
        return true;
      }
    }
    return false;
  }
  uint64_t overflow(absl::string_view type) {
    return TestUtility::findCounter(*store_, absl::StrCat("server.stats_overflow.", type))->value();
  }

  void evict() { store_->evictUnused(); }
  void evictTwice() {
    evict();
    evict();
  }

  // Names of the stats in the store that are not custom metrics.
  std::vector<std::string> envoyStatNames() {
    std::vector<std::string> names;
    for (const auto& c : store_->counters()) {
      names.push_back(c->name());
    }
    for (const auto& g : store_->gauges()) {
      names.push_back(g->name());
    }
    names.erase(std::remove_if(
                    names.begin(), names.end(),
                    [](const std::string& name) { return absl::StrContains(name, "wasmcustom"); }),
                names.end());
    std::sort(names.begin(), names.end());
    return names;
  }

  Stats::SymbolTableImpl symbol_table_;
  NiceMock<Event::MockDispatcher> main_thread_dispatcher_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Stats::Allocator alloc_;
  std::unique_ptr<Stats::ThreadLocalStoreImpl> store_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Stats::ScopeSharedPtr scope_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  PluginSharedPtr plugin_;
  std::vector<WasmSharedPtr> wasms_;
  std::vector<std::unique_ptr<Context>> contexts_;
};

TEST_F(CustomMetricsTest, DefineReturnsSameIdForSameName) {
  createWasm();
  const uint32_t c = define(MetricType::Counter, "c");
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(c, define(MetricType::Counter, "c"));
  }
  // The repeated definitions did not use ids.
  const uint32_t d = define(MetricType::Counter, "d");
  EXPECT_EQ(c + Wasm::kMetricIdIncrement, d);

  // The same name with another type is another metric. A counter and a gauge cannot share a name
  // in the stats store, so only a histogram is used here.
  const uint32_t h = define(MetricType::Histogram, "c");
  EXPECT_NE(c, h);
  EXPECT_EQ(h, define(MetricType::Histogram, "c"));
  const uint32_t g = define(MetricType::Gauge, "g");
  EXPECT_EQ(g, define(MetricType::Gauge, "g"));
  EXPECT_NE(g, h);

  increment(c, 1);
  increment(define(MetricType::Counter, "c"), 2);
  EXPECT_EQ(3, get(c));
  EXPECT_EQ(3, counter("c")->value());

  uint32_t id;
  EXPECT_EQ(WasmResult::BadArgument, contexts_[0]->defineMetric(3, "c", &id));
}

TEST_F(CustomMetricsTest, ClonesShareMetrics) {
  createWasm();
  cloneWasm();
  const uint32_t c0 = define(MetricType::Counter, "c", 0);
  const uint32_t c1 = define(MetricType::Counter, "c", 1);
  increment(c0, 1, 0);
  increment(c1, 2, 1);
  EXPECT_EQ(3, get(c0, 0));
  EXPECT_EQ(3, get(c1, 1));
}

TEST_F(CustomMetricsTest, UnknownIds) {
  createWasm();
  const uint32_t c = define(MetricType::Counter, "c");
  const uint32_t g = define(MetricType::Gauge, "g");
  const uint32_t h = define(MetricType::Histogram, "h");
  const uint32_t unknown = Wasm::kMetricIdIncrement * 1000;
  for (uint32_t type = 0; type < 3; ++type) {
    EXPECT_EQ(WasmResult::NotFound, contexts_[0]->recordMetric(unknown + type, 1));
  }
  EXPECT_EQ(WasmResult::NotFound, contexts_[0]->incrementMetric(unknown, 1));
  EXPECT_EQ(WasmResult::NotFound, contexts_[0]->incrementMetric(unknown + 1, 1));
  uint64_t value;
  EXPECT_EQ(WasmResult::NotFound, contexts_[0]->getMetric(unknown, &value));
  EXPECT_EQ(WasmResult::NotFound, contexts_[0]->getMetric(unknown + 1, &value));
  // Unchanged behavior for known ids.
  EXPECT_EQ(WasmResult::BadArgument, contexts_[0]->incrementMetric(c, 0));
  EXPECT_EQ(WasmResult::BadArgument, contexts_[0]->incrementMetric(c, -1));
  EXPECT_EQ(WasmResult::BadArgument, contexts_[0]->incrementMetric(h, 1));
  EXPECT_EQ(WasmResult::BadArgument, contexts_[0]->getMetric(h, &value));
  record(h, 1);
  record(g, 1);
}

TEST_F(CustomMetricsTest, GaugeDecrementByInt64Min) {
  createWasm();
  const uint32_t g = define(MetricType::Gauge, "g");
  record(g, uint64_t{1} << 63);
  increment(g, std::numeric_limits<int64_t>::min());
  EXPECT_EQ(0, get(g));
}

TEST_F(CustomMetricsTest, NotEvictableByDefault) {
  createWasm();
  const uint32_t c = define(MetricType::Counter, "c");
  const uint32_t g = define(MetricType::Gauge, "g");
  increment(c, 1);
  evictTwice();
  ASSERT_NE(nullptr, counter("c"));
  ASSERT_NE(nullptr, gauge("g"));
  EXPECT_EQ(1, get(c));
  EXPECT_EQ(0, get(g));
}

TEST_F(CustomMetricsTest, NotEvictableWhenDisabled) {
  createWasm("enable_eviction: false");
  define(MetricType::Counter, "c");
  evictTwice();
  EXPECT_NE(nullptr, counter("c"));
}

// Without eviction, a metric over the limit is never created, and the null stat is kept.
TEST_F(CustomMetricsTest, LimitsWithoutEviction) {
  createWasm("max_counters: 1");
  const uint32_t c1 = define(MetricType::Counter, "c1");
  const uint32_t c2 = define(MetricType::Counter, "c2");
  EXPECT_EQ(1, overflow("counter"));
  for (int i = 0; i < 10; ++i) {
    increment(c2, 1);
  }
  increment(c1, 1);
  EXPECT_EQ(nullptr, counter("c2"));
  EXPECT_EQ(0, get(c2));
  EXPECT_EQ(1, get(c1));
  // The null stat is cached, so updates do not look up the store again.
  EXPECT_EQ(1, overflow("counter"));
}

TEST_F(CustomMetricsTest, CounterIsEvictedAndRecreated) {
  createWasm("enable_eviction: true");
  const uint32_t c = define(MetricType::Counter, "c");
  increment(c, 5);
  EXPECT_EQ(5, get(c));

  // The first pass marks the counter as unused; it is kept.
  evict();
  ASSERT_NE(nullptr, counter("c"));
  EXPECT_EQ(5, get(c));

  // The second pass evicts it.
  evict();
  EXPECT_EQ(nullptr, counter("c"));
  // get_metric does not re-create the metric.
  EXPECT_EQ(0, get(c));
  EXPECT_EQ(nullptr, counter("c"));

  // The id stays valid, and an update re-creates the metric from zero with the full name.
  increment(c, 1);
  ASSERT_NE(nullptr, counter("c"));
  EXPECT_EQ(1, counter("c")->value());
  EXPECT_EQ(1, get(c));
  // A repeated definition after eviction returns the same id without looking up the store, so
  // plugins that define metrics per request do not take the store lock on every request.
  evictTwice();
  EXPECT_EQ(nullptr, counter("c"));
  EXPECT_EQ(c, define(MetricType::Counter, "c"));
  EXPECT_EQ(nullptr, counter("c"));
  increment(c, 1);
  EXPECT_EQ(1, counter("c")->value());
}

TEST_F(CustomMetricsTest, UsedCounterIsNotEvicted) {
  createWasm("enable_eviction: true");
  const uint32_t c = define(MetricType::Counter, "c");
  for (int i = 0; i < 5; ++i) {
    increment(c, 1);
    evict();
  }
  ASSERT_NE(nullptr, counter("c"));
  EXPECT_EQ(5, get(c));
}

TEST_F(CustomMetricsTest, DefinedMetricIsCreatedAndEvictedIfNotUsed) {
  createWasm("enable_eviction: true");
  define(MetricType::Counter, "c");
  define(MetricType::Gauge, "g");
  EXPECT_NE(nullptr, counter("c"));
  EXPECT_NE(nullptr, gauge("g"));
  evict();
  EXPECT_EQ(nullptr, counter("c"));
  EXPECT_EQ(nullptr, gauge("g"));
}

TEST_F(CustomMetricsTest, GaugeIsEvictedOnlyAtZero) {
  createWasm("enable_eviction: true");
  const uint32_t g = define(MetricType::Gauge, "g");
  record(g, 3);
  for (int i = 0; i < 3; ++i) {
    evict();
  }
  ASSERT_NE(nullptr, gauge("g"));
  EXPECT_EQ(3, get(g));

  record(g, 0);
  evictTwice();
  EXPECT_EQ(nullptr, gauge("g"));
  EXPECT_EQ(0, get(g));
  EXPECT_EQ(nullptr, gauge("g"));
}

TEST_F(CustomMetricsTest, PairedGaugeAcrossEvictionPasses) {
  createWasm("enable_eviction: true");
  const uint32_t g = define(MetricType::Gauge, "g");
  increment(g, 1);
  evictTwice();
  ASSERT_NE(nullptr, gauge("g"));
  increment(g, -1);
  EXPECT_EQ(0, get(g));
  evictTwice();
  EXPECT_EQ(nullptr, gauge("g"));
  increment(g, 2);
  increment(g, -2);
  EXPECT_EQ(0, gauge("g")->value());
}

TEST_F(CustomMetricsTest, HistogramIsEvictedAndRecreated) {
  createWasm("enable_eviction: true");
  const uint32_t h = define(MetricType::Histogram, "h");
  record(h, 1);
  store_->mergeHistograms([]() {});
  evict();
  EXPECT_TRUE(hasHistogram("h"));
  evict();
  EXPECT_FALSE(hasHistogram("h"));
  record(h, 1);
  EXPECT_TRUE(hasHistogram("h"));
}

TEST_F(CustomMetricsTest, EnvoyStatsAreNotEvicted) {
  createWasm("enable_eviction: true");
  const std::vector<std::string> before = envoyStatNames();
  EXPECT_FALSE(before.empty());
  define(MetricType::Counter, "c");
  for (int i = 0; i < 3; ++i) {
    evict();
  }
  EXPECT_EQ(before, envoyStatNames());
}

TEST_F(CustomMetricsTest, ClonesAreEvictable) {
  createWasm("enable_eviction: true");
  cloneWasm();
  const uint32_t c = define(MetricType::Counter, "c", 1);
  increment(c, 1, 1);
  evictTwice();
  EXPECT_EQ(nullptr, counter("c"));
  EXPECT_EQ(0, get(c, 1));
}

TEST_F(CustomMetricsTest, LimitsWithEviction) {
  createWasm(R"EOF(
enable_eviction: true
max_counters: 1
)EOF");
  const uint32_t c1 = define(MetricType::Counter, "c1");
  const uint32_t c2 = define(MetricType::Counter, "c2");
  EXPECT_EQ(1, overflow("counter"));
  EXPECT_EQ(nullptr, counter("c2"));

  // While c2 cannot be created, its updates are discarded, and each one looks up the store again.
  for (int i = 0; i < 10; ++i) {
    increment(c2, 1);
  }
  EXPECT_EQ(11, overflow("counter"));
  EXPECT_EQ(0, get(c2));
  EXPECT_EQ(11, overflow("counter"));

  // Once c1 is evicted, c2 can be created.
  evictTwice();
  EXPECT_EQ(nullptr, counter("c1"));
  increment(c2, 1);
  ASSERT_NE(nullptr, counter("c2"));
  EXPECT_EQ(1, get(c2));
  EXPECT_EQ(11, overflow("counter"));
  // And now c1 is over the limit.
  increment(c1, 1);
  EXPECT_EQ(12, overflow("counter"));
  EXPECT_EQ(0, get(c1));
}

TEST_F(CustomMetricsTest, HistogramLimitsWithEviction) {
  createWasm(R"EOF(
enable_eviction: true
max_histograms: 1
)EOF");
  define(MetricType::Histogram, "h1");
  const uint32_t h2 = define(MetricType::Histogram, "h2");
  EXPECT_EQ(1, overflow("histogram"));
  record(h2, 1);
  EXPECT_EQ(2, overflow("histogram"));
  EXPECT_FALSE(hasHistogram("h2"));
}

// A gauge increment that is discarded because of a limit is paired with the next decrement, so
// that the decrement does not underflow the gauge once it can be created.
TEST_F(CustomMetricsTest, DiscardedGaugeIncrementIsPairedWithDecrement) {
  createWasm(R"EOF(
enable_eviction: true
max_gauges: 1
)EOF");
  const uint32_t g1 = define(MetricType::Gauge, "g1");
  const uint32_t g2 = define(MetricType::Gauge, "g2");
  record(g1, 1);
  increment(g2, 2);
  EXPECT_EQ(nullptr, gauge("g2"));
  EXPECT_EQ(2, overflow("gauge"));

  // Free the slot, so that g2 can be created.
  record(g1, 0);
  evictTwice();
  EXPECT_EQ(nullptr, gauge("g1"));

  // In debug builds, Gauge::sub() would assert if the decrement was applied.
  increment(g2, -1);
  ASSERT_NE(nullptr, gauge("g2"));
  EXPECT_EQ(0, get(g2));
  increment(g2, 3);
  increment(g2, -4);
  EXPECT_EQ(0, get(g2));
}

TEST_F(CustomMetricsTest, GaugeSetResetsDiscardedIncrements) {
  createWasm(R"EOF(
enable_eviction: true
max_gauges: 1
)EOF");
  const uint32_t g1 = define(MetricType::Gauge, "g1");
  const uint32_t g2 = define(MetricType::Gauge, "g2");
  record(g1, 1);
  increment(g2, 2);
  record(g1, 0);
  evictTwice();

  record(g2, 5);
  increment(g2, -2);
  EXPECT_EQ(3, get(g2));
}

// An unpaired decrement asserts in debug builds, with or without eviction. This is not changed.
TEST_F(CustomMetricsTest, UnpairedGaugeDecrementAssertsInDebug) {
  createWasm();
  const uint32_t g = define(MetricType::Gauge, "g");
  EXPECT_DEBUG_DEATH(increment(g, -1), "");
}

// Documents the known limitation inherited from Envoy's stats eviction: an increment that lands
// on a gauge after it was removed from the central cache, but before the gauge is released, is
// lost. A worker can do this through its thread local cache; the test holds a reference instead.
// The paired decrement then applies to a new gauge with value zero.
TEST_F(CustomMetricsTest, GaugeIncrementDuringEvictionIsLost) {
  createWasm("enable_eviction: true");
  const uint32_t g = define(MetricType::Gauge, "g");
  Stats::GaugeSharedPtr stale = gauge("g");
  // The gauge is unused and zero, so one pass removes it from the central cache.
  evict();
  stale->add(1);
  stale.reset();
  EXPECT_EQ(0, get(g));
  EXPECT_DEBUG_DEATH(increment(g, -1), "");
}

} // namespace
} // namespace Wasm
} // namespace Common
} // namespace Extensions
} // namespace Envoy
