#include <memory>

#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stats/stat_merger.h"
#include "source/common/stats/thread_local_store.h"

#include "test/test_common/utility.h"

#include "absl/strings/str_replace.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatMergerTest : public testing::Test {
public:
  StatMergerTest()
      : stat_merger_(store_), whywassixafraidofseven_(store_.gaugeFromString(
                                  "whywassixafraidofseven", Gauge::ImportMode::Accumulate)) {
    whywassixafraidofseven_.set(678);
  }

  void mergeTest(const std::string& name, Gauge::ImportMode initial, Gauge::ImportMode merge) {
    Gauge& g1 = store_.gaugeFromString(name, initial);
    EXPECT_EQ(initial, g1.importMode()) << name;
    g1.mergeImportMode(merge);
    EXPECT_EQ(merge, g1.importMode()) << name;
  }

  void dynamicEncodeDecodeTest(absl::string_view input_name) {
    SymbolTable& symbol_table = store_.symbolTable();

    // Encode the input name into a joined StatName, using "D:" to indicate
    // a dynamic component.
    StatNameVec components;
    StatNamePool symbolic_pool(symbol_table);
    StatNameDynamicPool dynamic_pool(symbol_table);

    for (absl::string_view segment : absl::StrSplit(input_name, '.')) {
      if (absl::StartsWith(segment, "D:")) {
        std::string hacked = absl::StrReplaceAll(segment.substr(2), {{",", "."}});
        components.push_back(dynamic_pool.add(hacked));
      } else {
        components.push_back(symbolic_pool.add(segment));
      }
    }
    SymbolTable::StoragePtr joined = symbol_table.join(components);
    StatName stat_name(joined.get());

    std::string name = symbol_table.toString(stat_name);
    StatMerger::DynamicsMap dynamic_map;
    dynamic_map[name] = symbol_table.getDynamicSpans(stat_name);
    StatMerger::DynamicContext dynamic_context(symbol_table);
    StatName decoded = dynamic_context.makeDynamicStatName(name, dynamic_map);
    EXPECT_EQ(stat_name, decoded) << name;
  }

  IsolatedStoreImpl store_;
  StatMerger stat_merger_;
  Gauge& whywassixafraidofseven_;
  Protobuf::Map<std::string, uint64_t> empty_counter_deltas_;
  Protobuf::Map<std::string, uint64_t> empty_gauges_;
};

TEST_F(StatMergerTest, CounterMerge) {
  // Child's value of the counter might already be non-zero by the first merge.
  store_.counterFromString("draculaer").inc();
  EXPECT_EQ(1, store_.counterFromString("draculaer").latch());

  Protobuf::Map<std::string, uint64_t> counter_deltas;
  counter_deltas["draculaer"] = 1;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  // Initial combined value: 1+1.
  EXPECT_EQ(2, store_.counterFromString("draculaer").value());
  EXPECT_EQ(1, store_.counterFromString("draculaer").latch());

  // The parent's counter increases by 1.
  counter_deltas["draculaer"] = 1;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  EXPECT_EQ(3, store_.counterFromString("draculaer").value());
  EXPECT_EQ(1, store_.counterFromString("draculaer").latch());

  // Our own counter increases by 4, while the parent's stays constant. Total increase of 4.
  store_.counterFromString("draculaer").add(4);
  counter_deltas["draculaer"] = 0;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  EXPECT_EQ(7, store_.counterFromString("draculaer").value());
  EXPECT_EQ(4, store_.counterFromString("draculaer").latch());

  // Our counter and the parent's counter both increase by 2, total increase of 4.
  store_.counterFromString("draculaer").add(2);
  counter_deltas["draculaer"] = 2;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  EXPECT_EQ(11, store_.counterFromString("draculaer").value());
  EXPECT_EQ(4, store_.counterFromString("draculaer").latch());
}

TEST_F(StatMergerTest, BasicDefaultAccumulationImport) {
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["whywassixafraidofseven"] = 111;
  stat_merger_.mergeStats(empty_counter_deltas_, gauges);
  EXPECT_EQ(789, whywassixafraidofseven_.value());
}

TEST_F(StatMergerTest, MultipleImportsWithAccumulationLogic) {
  {
    Protobuf::Map<std::string, uint64_t> gauges;
    gauges["whywassixafraidofseven"] = 100;
    stat_merger_.mergeStats(empty_counter_deltas_, gauges);
    // Initial combined values: 678+100 and 1+2.
    EXPECT_EQ(778, whywassixafraidofseven_.value());
  }
  {
    Protobuf::Map<std::string, uint64_t> gauges;
    // The parent's gauge drops by 1, and its counter increases by 1.
    gauges["whywassixafraidofseven"] = 99;
    stat_merger_.mergeStats(empty_counter_deltas_, gauges);
    EXPECT_EQ(777, whywassixafraidofseven_.value());
  }
  {
    Protobuf::Map<std::string, uint64_t> gauges;
    // Our own gauge increases by 12, while the parent's stays constant. Total increase of 12.
    // Our own counter increases by 4, while the parent's stays constant. Total increase of 4.
    whywassixafraidofseven_.add(12);
    stat_merger_.mergeStats(empty_counter_deltas_, gauges);
    EXPECT_EQ(789, whywassixafraidofseven_.value());
  }
  {
    Protobuf::Map<std::string, uint64_t> gauges;
    // Our gauge decreases by 5, parent's increases by 5. Net zero change.
    // Our counter and the parent's counter both increase by 1, total increase of 2.
    whywassixafraidofseven_.sub(5);
    gauges["whywassixafraidofseven"] = 104;
    stat_merger_.mergeStats(empty_counter_deltas_, gauges);
    EXPECT_EQ(789, whywassixafraidofseven_.value());
  }
}

// Stat names that have NoImport logic should leave the child gauge value alone upon import, even if
// the child has that gauge undefined.
TEST_F(StatMergerTest, ExclusionsNotImported) {
  Gauge& some_sort_of_version =
      store_.gaugeFromString("some.sort.of.version", Gauge::ImportMode::NeverImport);
  some_sort_of_version.set(12345);

  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["some.sort.of.version"] = 67890;
  gauges["child.doesnt.have.this.version"] = 111; // This should never be populated.

  // Check defined values are not changed, and undefined remain undefined.
  stat_merger_.mergeStats(empty_counter_deltas_, gauges);
  EXPECT_EQ(12345, some_sort_of_version.value());
  EXPECT_FALSE(
      store_.gaugeFromString("child.doesnt.have.this.version", Gauge::ImportMode::NeverImport)
          .used());

  // Check the "undefined remains undefined" behavior for a bunch of other names.
  gauges["runtime.admin_overrides_active"] = 111;
  gauges["runtime.num_keys"] = 111;
  gauges["runtime.num_layers"] = 111;
  gauges["listener_manager.total_listeners_draining"] = 111;
  gauges["listener_manager.total_listeners_warming"] = 111;
  gauges["server.hot_restart_epoch"] = 111;
  gauges["server.live"] = 1;
  gauges["server.concurrency"] = 1;
  gauges["some.control_plane.connected_state"] = 1;
  gauges["cluster_manager.active_clusters"] = 33;
  gauges["cluster_manager.warming_clusters"] = 33;
  gauges["cluster.rds.membership_total"] = 33;
  gauges["cluster.rds.membership_healthy"] = 33;
  gauges["cluster.rds.membership_degraded"] = 33;
  gauges["cluster.rds.max_host_weight"] = 33;
  gauges["anything.total_principals"] = 33;
  gauges["listener_manager.total_listeners_active"] = 33;
  gauges["overload.something.pressure"] = 33;

  stat_merger_.mergeStats(empty_counter_deltas_, gauges);
#define EXPECT_GAUGE_NOT_USED(name)                                                                \
  EXPECT_FALSE(store_.gaugeFromString(name, Gauge::ImportMode::NeverImport).used())

  EXPECT_GAUGE_NOT_USED("child.doesnt.have.this.version");
  EXPECT_GAUGE_NOT_USED("runtime.admin_overrides_active");
  EXPECT_GAUGE_NOT_USED("runtime.num_keys");
  EXPECT_GAUGE_NOT_USED("runtime.num_layers");
  EXPECT_GAUGE_NOT_USED("listener_manager.total_listeners_draining");
  EXPECT_GAUGE_NOT_USED("listener_manager.total_listeners_warming");
  EXPECT_GAUGE_NOT_USED("server.hot_restart_epoch");
  EXPECT_GAUGE_NOT_USED("server.live");
  EXPECT_GAUGE_NOT_USED("server.concurrency");
  EXPECT_GAUGE_NOT_USED("some.control_plane.connected_state");
  EXPECT_GAUGE_NOT_USED("cluster_manager.active_clusters");
  EXPECT_GAUGE_NOT_USED("cluster_manager.warming_clusters");
  EXPECT_GAUGE_NOT_USED("cluster.rds.membership_total");
  EXPECT_GAUGE_NOT_USED("cluster.rds.membership_healthy");
  EXPECT_GAUGE_NOT_USED("cluster.rds.membership_degraded");
  EXPECT_GAUGE_NOT_USED("cluster.rds.max_host_weight");
  EXPECT_GAUGE_NOT_USED("anything.total_principals");
  EXPECT_GAUGE_NOT_USED("listener_manager.total_listeners_active");
  EXPECT_GAUGE_NOT_USED("overload.something.pressure");
#undef EXPECT_GAUGE_NOT_USED
}

// Targeted test of GaugeImpl::mergeImportMode().
TEST_F(StatMergerTest, GaugeMergeImportMode) {
  mergeTest("newgauge1", Gauge::ImportMode::Accumulate, Gauge::ImportMode::Accumulate);
  mergeTest("s1.version", Gauge::ImportMode::NeverImport, Gauge::ImportMode::NeverImport);
  mergeTest("newgauge2", Gauge::ImportMode::Uninitialized, Gauge::ImportMode::Accumulate);
  mergeTest("s2.version", Gauge::ImportMode::Uninitialized, Gauge::ImportMode::NeverImport);
}

class StatMergerDynamicTest : public testing::Test {
public:
  void init(SymbolTablePtr&& symbol_table) { symbol_table_ = std::move(symbol_table); }

  /**
   * Test helper function takes an input_descriptor. And input_descriptor is
   * mostly like the stringified StatName, but each segment that is prefixed by
   * "D:" is dynamic, and within a segment, we map "," to ".". The "D:" hack
   * restricts the stat names we can test by making a prefix special. The ","
   * hack does that too, allowing us to represent a single multi-segment dynamic
   * token in the tests. These hacks were easy to implement (~ 3 lines of code)
   * and provide a reasonably concise way to make a few test-cases.
   *
   * The test-helper ensures that a StatName created from a descriptor can
   * be encoded into a DynamicsMap, and also decoded back into a StatName
   * that compares as expected.
   *
   * @param a pattern describing a stat-name with dynamic and symbolic components.
   * @return the number of elements in the dynamic map.
   */
  uint32_t dynamicEncodeDecodeTest(absl::string_view input_descriptor) {
    // Encode the input name into a joined StatName, using "D:" to indicate
    // a dynamic component.
    StatNameVec components;
    StatNamePool symbolic_pool(*symbol_table_);
    StatNameDynamicPool dynamic_pool(*symbol_table_);

    for (absl::string_view segment : absl::StrSplit(input_descriptor, '.')) {
      if (absl::StartsWith(segment, "D:")) {
        std::string hacked = absl::StrReplaceAll(segment.substr(2), {{",", "."}});
        components.push_back(dynamic_pool.add(hacked));
      } else {
        components.push_back(symbolic_pool.add(segment));
      }
    }
    StatName stat_name;
    SymbolTable::StoragePtr joined;

    if (components.size() == 1) {
      stat_name = components[0];
    } else {
      joined = symbol_table_->join(components);
      stat_name = StatName(joined.get());
    }

    std::string name = symbol_table_->toString(stat_name);
    StatMerger::DynamicsMap dynamic_map;
    DynamicSpans spans = symbol_table_->getDynamicSpans(stat_name);
    uint32_t size = 0;
    if (!spans.empty()) {
      dynamic_map[name] = spans;
      size = spans.size();
    }
    StatMerger::DynamicContext dynamic_context(*symbol_table_);
    StatName decoded = dynamic_context.makeDynamicStatName(name, dynamic_map);
    EXPECT_EQ(name, symbol_table_->toString(decoded)) << "input=" << input_descriptor;
    EXPECT_TRUE(stat_name == decoded) << "input=" << input_descriptor << ", name=" << name;

    return size;
  }

  SymbolTablePtr symbol_table_;
};

TEST_F(StatMergerDynamicTest, DynamicsWithRealSymbolTable) {
  init(std::make_unique<SymbolTableImpl>());

  for (uint32_t i = 1; i < 256; ++i) {
    char ch = static_cast<char>(i);
    absl::string_view one_char(&ch, 1);
    EXPECT_EQ(1, dynamicEncodeDecodeTest(absl::StrCat("D:", one_char))) << "dynamic=" << one_char;
    EXPECT_EQ(0, dynamicEncodeDecodeTest(one_char)) << "symbolic=" << one_char;
  }
  EXPECT_EQ(0, dynamicEncodeDecodeTest("normal"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("D:dynamic"));
  EXPECT_EQ(0, dynamicEncodeDecodeTest("hello.world"));
  EXPECT_EQ(0, dynamicEncodeDecodeTest("hello..world"));
  EXPECT_EQ(0, dynamicEncodeDecodeTest("hello...world"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("D:hello.world"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("hello.D:world"));
  EXPECT_EQ(2, dynamicEncodeDecodeTest("D:hello.D:world"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("D:hello,world"));
  EXPECT_EQ(4, dynamicEncodeDecodeTest("one.D:two.three.D:four.D:five.six.D:seven,eight.nine"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("D:one,two,three"));
  EXPECT_EQ(0, dynamicEncodeDecodeTest("hello..world"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("D:hello..world"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("hello..D:world"));
  EXPECT_EQ(2, dynamicEncodeDecodeTest("D:hello..D:world"));
  EXPECT_EQ(3, dynamicEncodeDecodeTest("D:hello.D:.D:world"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("D:hello,,world"));
  EXPECT_EQ(1, dynamicEncodeDecodeTest("D:hello,,,world"));
}

class StatMergerThreadLocalTest : public testing::Test {
protected:
  SymbolTableImpl symbol_table_;
  AllocatorImpl alloc_{symbol_table_};
  ThreadLocalStoreImpl store_{alloc_};
};

TEST_F(StatMergerThreadLocalTest, DontFilterOutUninitializedGauges) {
  Gauge& g1 = store_.gaugeFromString("newgauge1", Gauge::ImportMode::Uninitialized);
  Gauge& g2 = store_.gaugeFromString("newgauge2", Gauge::ImportMode::Accumulate);
  std::vector<GaugeSharedPtr> gauges = store_.gauges();
  ASSERT_EQ(2, gauges.size());
  auto gauge1_it = std::find_if(gauges.begin(), gauges.end(), [](const GaugeSharedPtr& gauge) {
    return gauge->name().compare("newgauge1") == 0;
  });
  ASSERT_TRUE(gauge1_it != gauges.end());
  EXPECT_EQ(&g1, gauge1_it->get());
  auto gauge2_it = std::find_if(gauges.begin(), gauges.end(), [](const GaugeSharedPtr& gauge) {
    return gauge->name().compare("newgauge2") == 0;
  });
  ASSERT_TRUE(gauge2_it != gauges.end());
  EXPECT_EQ(&g2, gauge2_it->get());

  // We get "newgauge1" in the aggregated list and if we try to find it by name.
  GaugeOptConstRef find = store_.rootScope()->findGauge(g1.statName());
  ASSERT_TRUE(find);
  EXPECT_EQ(&g1, &(find->get()));
}

// When the parent sends us counters we haven't ourselves instantiated, they should be stored
// temporarily, but then uninstantiated if hot restart ends without the child accessing them.
TEST_F(StatMergerThreadLocalTest, NewStatFromParent) {
  {
    StatMerger stat_merger(store_);

    Protobuf::Map<std::string, uint64_t> counter_deltas;
    Protobuf::Map<std::string, uint64_t> gauges;
    counter_deltas["newcounter0"] = 0;
    counter_deltas["newcounter1"] = 1;
    counter_deltas["newcounter2"] = 2;
    gauges["newgauge1"] = 1;
    gauges["newgauge2"] = 2;
    stat_merger.mergeStats(counter_deltas, gauges);
    EXPECT_EQ(0, store_.counterFromString("newcounter0").value());
    EXPECT_EQ(0, store_.counterFromString("newcounter0").latch());
    EXPECT_EQ(1, store_.counterFromString("newcounter1").value());
    EXPECT_EQ(1, store_.counterFromString("newcounter1").latch());
    EXPECT_EQ(1, store_.gaugeFromString("newgauge1", Gauge::ImportMode::Accumulate).value());
  }
  // We accessed 0 and 1 above, but not 2. Now that StatMerger has been destroyed,
  // 2 should be gone.
  EXPECT_TRUE(TestUtility::findCounter(store_, "newcounter0"));
  EXPECT_TRUE(TestUtility::findCounter(store_, "newcounter1"));
  EXPECT_FALSE(TestUtility::findCounter(store_, "newcounter2"));
  EXPECT_TRUE(TestUtility::findGauge(store_, "newgauge1"));
  EXPECT_FALSE(TestUtility::findGauge(store_, "newgauge2"));
}

// Verify that if we create a stat in the child process which then gets merged
// from the parent, that we retain the import-mode, accumulating the updated
// value. https://github.com/envoyproxy/envoy/issues/7227
TEST_F(StatMergerThreadLocalTest, RetainImportModeAfterMerge) {
  Gauge& gauge = store_.gaugeFromString("mygauge", Gauge::ImportMode::Accumulate);
  gauge.set(42);
  EXPECT_EQ(Gauge::ImportMode::Accumulate, gauge.importMode());
  EXPECT_EQ(42, gauge.value());
  {
    StatMerger stat_merger(store_);
    Protobuf::Map<std::string, uint64_t> counter_deltas;
    Protobuf::Map<std::string, uint64_t> gauges;
    gauges["mygauge"] = 789;
    stat_merger.mergeStats(counter_deltas, gauges);
    EXPECT_EQ(789 + 42, gauge.value());
  }
  EXPECT_EQ(42, gauge.value());
  EXPECT_EQ(Gauge::ImportMode::Accumulate, gauge.importMode());
}

// Verify that if we create a never import stat in the child process which then gets merged
// from the parent, that we retain the import-mode, and don't accumulate the updated
// value. https://github.com/envoyproxy/envoy/issues/7227
TEST_F(StatMergerThreadLocalTest, RetainNeverImportModeAfterMerge) {
  Gauge& gauge = store_.gaugeFromString("mygauge", Gauge::ImportMode::NeverImport);
  gauge.set(42);
  EXPECT_EQ(Gauge::ImportMode::NeverImport, gauge.importMode());
  EXPECT_EQ(42, gauge.value());
  {
    StatMerger stat_merger(store_);
    Protobuf::Map<std::string, uint64_t> counter_deltas;
    Protobuf::Map<std::string, uint64_t> gauges;
    gauges["mygauge"] = 789;
    stat_merger.mergeStats(counter_deltas, gauges);
  }
  EXPECT_EQ(Gauge::ImportMode::NeverImport, gauge.importMode());
  EXPECT_EQ(42, gauge.value());
}

} // namespace
} // namespace Stats
} // namespace Envoy
