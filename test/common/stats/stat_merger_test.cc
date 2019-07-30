#include <memory>

#include "common/stats/fake_symbol_table_impl.h"
#include "common/stats/isolated_store_impl.h"
#include "common/stats/stat_merger.h"
#include "common/stats/thread_local_store.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatMergerTest : public testing::Test {
public:
  StatMergerTest()
      : stat_merger_(store_), whywassixafraidofseven_(store_.gauge("whywassixafraidofseven",
                                                                   Gauge::ImportMode::Accumulate)) {
    whywassixafraidofseven_.set(678);
  }

  void mergeTest(const std::string& name, Gauge::ImportMode initial, Gauge::ImportMode merge) {
    Gauge& g1 = store_.gauge(name, initial);
    EXPECT_EQ(initial, g1.importMode()) << name;
    g1.mergeImportMode(merge);
    EXPECT_EQ(merge, g1.importMode()) << name;
  }

  IsolatedStoreImpl store_;
  StatMerger stat_merger_;
  Gauge& whywassixafraidofseven_;
  Protobuf::Map<std::string, uint64_t> empty_counter_deltas_;
  Protobuf::Map<std::string, uint64_t> empty_gauges_;
};

TEST_F(StatMergerTest, counterMerge) {
  // Child's value of the counter might already be non-zero by the first merge.
  store_.counter("draculaer").inc();
  EXPECT_EQ(1, store_.counter("draculaer").latch());

  Protobuf::Map<std::string, uint64_t> counter_deltas;
  counter_deltas["draculaer"] = 1;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  // Initial combined value: 1+1.
  EXPECT_EQ(2, store_.counter("draculaer").value());
  EXPECT_EQ(1, store_.counter("draculaer").latch());

  // The parent's counter increases by 1.
  counter_deltas["draculaer"] = 1;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  EXPECT_EQ(3, store_.counter("draculaer").value());
  EXPECT_EQ(1, store_.counter("draculaer").latch());

  // Our own counter increases by 4, while the parent's stays constant. Total increase of 4.
  store_.counter("draculaer").add(4);
  counter_deltas["draculaer"] = 0;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  EXPECT_EQ(7, store_.counter("draculaer").value());
  EXPECT_EQ(4, store_.counter("draculaer").latch());

  // Our counter and the parent's counter both increase by 2, total increase of 4.
  store_.counter("draculaer").add(2);
  counter_deltas["draculaer"] = 2;
  stat_merger_.mergeStats(counter_deltas, empty_gauges_);
  EXPECT_EQ(11, store_.counter("draculaer").value());
  EXPECT_EQ(4, store_.counter("draculaer").latch());
}

TEST_F(StatMergerTest, basicDefaultAccumulationImport) {
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["whywassixafraidofseven"] = 111;
  stat_merger_.mergeStats(empty_counter_deltas_, gauges);
  EXPECT_EQ(789, whywassixafraidofseven_.value());
}

TEST_F(StatMergerTest, multipleImportsWithAccumulationLogic) {
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
TEST_F(StatMergerTest, exclusionsNotImported) {
  Gauge& some_sort_of_version =
      store_.gauge("some.sort.of.version", Gauge::ImportMode::NeverImport);
  some_sort_of_version.set(12345);

  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["some.sort.of.version"] = 67890;
  gauges["child.doesnt.have.this.version"] = 111; // This should never be populated.

  // Check defined values are not changed, and undefined remain undefined.
  stat_merger_.mergeStats(empty_counter_deltas_, gauges);
  EXPECT_EQ(12345, some_sort_of_version.value());
  EXPECT_FALSE(
      store_.gauge("child.doesnt.have.this.version", Gauge::ImportMode::NeverImport).used());

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
  EXPECT_FALSE(store_.gauge(name, Gauge::ImportMode::NeverImport).used())

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
TEST_F(StatMergerTest, gaugeMergeImportMode) {
  mergeTest("newgauge1", Gauge::ImportMode::Accumulate, Gauge::ImportMode::Accumulate);
  mergeTest("s1.version", Gauge::ImportMode::NeverImport, Gauge::ImportMode::NeverImport);
  mergeTest("newgauge2", Gauge::ImportMode::Uninitialized, Gauge::ImportMode::Accumulate);
  mergeTest("s2.version", Gauge::ImportMode::Uninitialized, Gauge::ImportMode::NeverImport);
}

class StatMergerThreadLocalTest : public testing::Test {
protected:
  FakeSymbolTableImpl symbol_table_;
  AllocatorImpl alloc_{symbol_table_};
  ThreadLocalStoreImpl store_{alloc_};
};

TEST_F(StatMergerThreadLocalTest, filterOutUninitializedGauges) {
  Gauge& g1 = store_.gauge("newgauge1", Gauge::ImportMode::Uninitialized);
  Gauge& g2 = store_.gauge("newgauge2", Gauge::ImportMode::Accumulate);
  std::vector<GaugeSharedPtr> gauges = store_.gauges();
  ASSERT_EQ(1, gauges.size());
  EXPECT_EQ(&g2, gauges[0].get());

  // We don't get "newgauge1" in the aggregated list, but we *do* get it if we try to
  // find it by name.
  OptionalGauge find = store_.findGauge(g1.statName());
  ASSERT_TRUE(find);
  EXPECT_EQ(&g1, &(find->get()));
}

// When the parent sends us counters we haven't ourselves instantiated, they should be stored
// temporarily, but then uninstantiated if hot restart ends without the child accessing them.
TEST_F(StatMergerThreadLocalTest, newStatFromParent) {
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
    EXPECT_EQ(0, store_.counter("newcounter0").value());
    EXPECT_EQ(0, store_.counter("newcounter0").latch());
    EXPECT_EQ(1, store_.counter("newcounter1").value());
    EXPECT_EQ(1, store_.counter("newcounter1").latch());
    EXPECT_EQ(1, store_.gauge("newgauge1", Gauge::ImportMode::Accumulate).value());
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
TEST_F(StatMergerThreadLocalTest, retainImportModeAfterMerge) {
  Gauge& gauge = store_.gauge("mygauge", Gauge::ImportMode::Accumulate);
  gauge.set(42);
  EXPECT_EQ(Gauge::ImportMode::Accumulate, gauge.importMode());
  EXPECT_EQ(42, gauge.value());
  {
    StatMerger stat_merger(store_);
    Protobuf::Map<std::string, uint64_t> counter_deltas;
    Protobuf::Map<std::string, uint64_t> gauges;
    gauges["mygauge"] = 789;
    stat_merger.mergeStats(counter_deltas, gauges);
  }
  EXPECT_EQ(Gauge::ImportMode::Accumulate, gauge.importMode());
  EXPECT_EQ(789 + 42, gauge.value());
}

// Verify that if we create a never import stat in the child process which then gets merged
// from the parent, that we retain the import-mode, and don't accumulate the updated
// value. https://github.com/envoyproxy/envoy/issues/7227
TEST_F(StatMergerThreadLocalTest, retainNeverImportModeAfterMerge) {
  Gauge& gauge = store_.gauge("mygauge", Gauge::ImportMode::NeverImport);
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
