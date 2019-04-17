#include <memory>

#include "common/stats/isolated_store_impl.h"
#include "common/stats/stat_merger.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {
namespace {

class StatMergerTest : public testing::Test {
public:
  StatMergerTest() : stat_merger_(store_) {
    store_.counter("draculaer").inc();
    store_.gauge("whywassixafraidofseven").set(678);
  }

  Stats::IsolatedStoreImpl store_;
  StatMerger stat_merger_;
};

TEST_F(StatMergerTest, basicDefaultAccumulationImport) {
  Protobuf::Map<std::string, uint64_t> counters;
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["whywassixafraidofseven"] = 111;
  counters["draculaer"] = 3;

  stat_merger_.mergeStats(counters, gauges);

  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(4, store_.counter("draculaer").value());
}

TEST_F(StatMergerTest, multipleImportsWithAccumulationLogic) {
  Protobuf::Map<std::string, uint64_t> counters1;
  Protobuf::Map<std::string, uint64_t> gauges1;
  gauges1["whywassixafraidofseven"] = 100;
  counters1["draculaer"] = 2;
  stat_merger_.mergeStats(counters1, gauges1);
  // Initial combined values: 678+100 and 1+2.
  EXPECT_EQ(778, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(3, store_.counter("draculaer").value());

  // The parent's gauge drops by 1, and its counter increases by 1.
  Protobuf::Map<std::string, uint64_t> counters2;
  Protobuf::Map<std::string, uint64_t> gauges2;
  gauges2["whywassixafraidofseven"] = 99;
  counters2["draculaer"] = 3;
  stat_merger_.mergeStats(counters2, gauges2);
  EXPECT_EQ(777, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(4, store_.counter("draculaer").value());

  // Our own gauge increases by 12, while the parent's stays constant. Total increase of 12.
  // Our own counter increases by 4, while the parent's stays constant. Total increase of 4.
  store_.gauge("whywassixafraidofseven").add(12);
  store_.counter("draculaer").add(4);
  stat_merger_.mergeStats(counters2, gauges2);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(8, store_.counter("draculaer").value());

  // Our gauge decreases by 5, parent's increases by 5. Net zero change.
  // Our counter and the parent's counter both increase by 1, total increase of 2.
  store_.gauge("whywassixafraidofseven").sub(5);
  store_.counter("draculaer").add(1);
  Protobuf::Map<std::string, uint64_t> counters3;
  Protobuf::Map<std::string, uint64_t> gauges3;
  gauges3["whywassixafraidofseven"] = 104;
  counters3["draculaer"] = 4;
  stat_merger_.mergeStats(counters3, gauges3);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(10, store_.counter("draculaer").value());
}

// Stat names that have NoImport logic should leave the child gauge value alone upon import, even if
// the child has that gauge undefined.
TEST_F(StatMergerTest, exclusionsNotImported) {
  store_.gauge("some.sort.of.version").set(12345);

  Protobuf::Map<std::string, uint64_t> counters;
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["some.sort.of.version"] = 67890;
  gauges["child.doesnt.have.this.version"] = 111;

  // Check defined values are not changed, and undefined remain undefined.
  stat_merger_.mergeStats(counters, gauges);
  EXPECT_EQ(12345, store_.gauge("some.sort.of.version").value());
  EXPECT_FALSE(store_.gauge("child.doesnt.have.this.version").used());

  // Check the "undefined remains undefined" behavior for a bunch of other names.
  gauges["runtime.admin_overrides_active"] = 111;
  gauges["runtime.num_keys"] = 111;
  gauges["listener_manager.total_listeners_draining"] = 111;
  gauges["server.hot_restart_epoch"] = 111;
  gauges["server.live"] = 1;
  gauges["some.control_plane.connected_state"] = 1;

  stat_merger_.mergeStats(counters, gauges);
  EXPECT_FALSE(store_.gauge("child.doesnt.have.this.version").used());
  EXPECT_FALSE(store_.gauge("runtime.admin_overrides_active").used());
  EXPECT_FALSE(store_.gauge("runtime.num_keys").used());
  EXPECT_FALSE(store_.gauge("listener_manager.total_listeners_draining").used());
  EXPECT_FALSE(store_.gauge("server.hot_restart_epoch").used());
  EXPECT_FALSE(store_.gauge("server.live").used());
  EXPECT_FALSE(store_.gauge("some.connected_state").used());
}

// The OnlyImportWhenUnused logic should overwrite an undefined gauge, but not a defined one.
TEST_F(StatMergerTest, onlyImportWhenUnused) {
  Protobuf::Map<std::string, uint64_t> counters;
  Protobuf::Map<std::string, uint64_t> gauges;
  gauges["cluster_manager.active_clusters"] = 33;
  gauges["cluster_manager.warming_clusters"] = 33;
  gauges["cluster.rds.membership_total"] = 33;
  gauges["cluster.rds.membership_healthy"] = 33;
  gauges["cluster.rds.membership_degraded"] = 33;
  gauges["cluster.rds.max_host_weight"] = 33;
  gauges["anything.total_principals"] = 33;
  gauges["listener_manager.total_listeners_warming"] = 33;
  gauges["listener_manager.total_listeners_active"] = 33;
  gauges["some_sort_of_pressure"] = 33;
  gauges["server.concurrency"] = 33;
  // 33 is stored into the child's until-now-undefined gauges
  stat_merger_.mergeStats(counters, gauges);
  EXPECT_EQ(33, store_.gauge("cluster_manager.active_clusters").value());
  EXPECT_EQ(33, store_.gauge("cluster_manager.warming_clusters").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.membership_total").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.membership_healthy").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.membership_degraded").value());
  EXPECT_EQ(33, store_.gauge("cluster.rds.max_host_weight").value());
  EXPECT_EQ(33, store_.gauge("anything.total_principals").value());
  EXPECT_EQ(33, store_.gauge("listener_manager.total_listeners_warming").value());
  EXPECT_EQ(33, store_.gauge("listener_manager.total_listeners_active").value());
  EXPECT_EQ(33, store_.gauge("some_sort_of_pressure").value());
  EXPECT_EQ(33, store_.gauge("server.concurrency").value());
  store_.gauge("cluster_manager.active_clusters").set(88);
  store_.gauge("cluster_manager.warming_clusters").set(88);
  store_.gauge("cluster.rds.membership_total").set(88);
  store_.gauge("cluster.rds.membership_healthy").set(88);
  store_.gauge("cluster.rds.membership_degraded").set(88);
  store_.gauge("cluster.rds.max_host_weight").set(88);
  store_.gauge("anything.total_principals").set(88);
  store_.gauge("listener_manager.total_listeners_warming").set(88);
  store_.gauge("listener_manager.total_listeners_active").set(88);
  store_.gauge("some_sort_of_pressure").set(88);
  store_.gauge("server.concurrency").set(88);
  // Now that the child's gauges have been set to 88, merging the "33" values will make no change.
  stat_merger_.mergeStats(counters, gauges);
  EXPECT_EQ(88, store_.gauge("cluster_manager.active_clusters").value());
  EXPECT_EQ(88, store_.gauge("cluster_manager.warming_clusters").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.membership_total").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.membership_healthy").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.membership_degraded").value());
  EXPECT_EQ(88, store_.gauge("cluster.rds.max_host_weight").value());
  EXPECT_EQ(88, store_.gauge("anything.total_principals").value());
  EXPECT_EQ(88, store_.gauge("listener_manager.total_listeners_warming").value());
  EXPECT_EQ(88, store_.gauge("listener_manager.total_listeners_active").value());
  EXPECT_EQ(88, store_.gauge("some_sort_of_pressure").value());
  EXPECT_EQ(88, store_.gauge("server.concurrency").value());
}

} // namespace
} // namespace Stats
} // namespace Envoy
