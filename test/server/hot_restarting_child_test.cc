#include <memory>

#include "common/api/os_sys_calls_impl.h"

#include "server/hot_restarting_child.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;
using testing::WithArg;

namespace Envoy {
namespace Server {
namespace {

class HotRestartingChildTest : public testing::Test {
public:
  HotRestartingChildTest() : os_calls_injector_(InitOsCalls()), hot_restarting_child_(123, 456) {
    store_.counter("draculaer").inc();
    store_.gauge("whywassixafraidofseven").set(678);
  }

  Api::MockOsSysCalls* InitOsCalls() {
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(AnyNumber());
    return &os_sys_calls_;
  }

  void addGaugeProto(envoy::HotRestartMessage::Reply::Stats& stats, const std::string& name,
                     uint64_t value) {
    auto* gauge_proto = stats.mutable_gauges()->Add();
    gauge_proto->set_name(name);
    gauge_proto->set_value(value);
  }

  void addCounterProto(envoy::HotRestartMessage::Reply::Stats& stats, const std::string& name,
                       uint64_t value) {
    auto* counter_proto = stats.mutable_counters()->Add();
    counter_proto->set_name(name);
    counter_proto->set_value(value);
  }

  Stats::IsolatedStoreImpl store_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_injector_;
  HotRestartingChild hot_restarting_child_;
};

TEST_F(HotRestartingChildTest, basicDefaultAccumulationImport) {
  envoy::HotRestartMessage::Reply::Stats stats;
  addGaugeProto(stats, "whywassixafraidofseven", 111);
  addCounterProto(stats, "draculaer", 3);

  hot_restarting_child_.mergeParentStats(store_, stats);

  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(4, store_.counter("draculaer").value());
}

TEST_F(HotRestartingChildTest, multipleImportsWithAccumulationLogic) {
  envoy::HotRestartMessage::Reply::Stats stats1;
  addGaugeProto(stats1, "whywassixafraidofseven", 100);
  addCounterProto(stats1, "draculaer", 2);
  hot_restarting_child_.mergeParentStats(store_, stats1);
  // Initial combined values: 678+100 and 1+2.
  EXPECT_EQ(778, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(3, store_.counter("draculaer").value());

  // The parent's gauge drops by 1, and its counter increases by 1.
  envoy::HotRestartMessage::Reply::Stats stats2;
  addGaugeProto(stats2, "whywassixafraidofseven", 99);
  addCounterProto(stats2, "draculaer", 3);
  hot_restarting_child_.mergeParentStats(store_, stats2);
  EXPECT_EQ(777, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(4, store_.counter("draculaer").value());

  // Our own gauge increases by 12, while the parent's stays constant. Total increase of 12.
  // Our own counter increases by 4, while the parent's stays constant. Total increase of 4.
  store_.gauge("whywassixafraidofseven").add(12);
  store_.counter("draculaer").add(4);
  hot_restarting_child_.mergeParentStats(store_, stats2);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(8, store_.counter("draculaer").value());

  // Our gauge decreases by 5, parent's increases by 5. Net zero change.
  // Our counter and the parent's counter both increase by 1, total increase of 2.
  store_.gauge("whywassixafraidofseven").sub(5);
  store_.counter("draculaer").add(1);
  envoy::HotRestartMessage::Reply::Stats stats3;
  addGaugeProto(stats3, "whywassixafraidofseven", 104);
  addCounterProto(stats3, "draculaer", 4);
  hot_restarting_child_.mergeParentStats(store_, stats3);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(10, store_.counter("draculaer").value());
}

// Stat names that have NoImport logic should leave the child gauge value alone upon import, even if
// the child has that gauge undefined.
TEST_F(HotRestartingChildTest, exclusionsNotImported) {
  store_.gauge("some.sort.of.version").set(12345);

  envoy::HotRestartMessage::Reply::Stats stats;
  addGaugeProto(stats, "some.sort.of.version", 67890);
  addGaugeProto(stats, "child.doesnt.have.this.version", 111);

  // Check defined values are not changed, and undefined remain undefined.
  hot_restarting_child_.mergeParentStats(store_, stats);
  EXPECT_EQ(12345, store_.gauge("some.sort.of.version").value());
  EXPECT_FALSE(store_.gauge("child.doesnt.have.this.version").used());

  // Check the "undefined remains undefined" behavior for a bunch of other names.
  addGaugeProto(stats, "runtime.admin_overrides_active", 111);
  addGaugeProto(stats, "runtime.num_keys", 111);
  addGaugeProto(stats, "listener_manager.total_listeners_draining", 111);
  addGaugeProto(stats, "server.hot_restart_epoch", 111);

  hot_restarting_child_.mergeParentStats(store_, stats);
  EXPECT_FALSE(store_.gauge("child.doesnt.have.this.version").used());
  EXPECT_FALSE(store_.gauge("runtime.admin_overrides_active").used());
  EXPECT_FALSE(store_.gauge("runtime.num_keys").used());
  EXPECT_FALSE(store_.gauge("listener_manager.total_listeners_draining").used());
  EXPECT_FALSE(store_.gauge("server.hot_restart_epoch").used());
}

// The OnlyImportWhenUnused logic should overwrite an undefined gauge, but not a defined one.
TEST_F(HotRestartingChildTest, onlyImportWhenUnused) {
  envoy::HotRestartMessage::Reply::Stats stats;
  addGaugeProto(stats, "cluster_manager.active_clusters", 33);
  addGaugeProto(stats, "cluster_manager.warming_clusters", 33);
  addGaugeProto(stats, "cluster.rds.membership_total", 33);
  addGaugeProto(stats, "cluster.rds.membership_healthy", 33);
  addGaugeProto(stats, "cluster.rds.membership_degraded", 33);
  addGaugeProto(stats, "cluster.rds.max_host_weight", 33);
  addGaugeProto(stats, "anything.total_principals", 33);
  addGaugeProto(stats, "listener_manager.total_listeners_warming", 33);
  addGaugeProto(stats, "listener_manager.total_listeners_active", 33);
  addGaugeProto(stats, "some_sort_of_pressure", 33);
  addGaugeProto(stats, "server.concurrency", 33);
  // 33 is stored into the child's until-now-undefined gauges
  hot_restarting_child_.mergeParentStats(store_, stats);
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
  hot_restarting_child_.mergeParentStats(store_, stats);
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

// Tests that the substrings "connected_state" and "server.live" get OR'd.
TEST_F(HotRestartingChildTest, booleanOr) {
  store_.gauge("some.connected_state").set(0);
  store_.gauge("server.live").set(0);

  envoy::HotRestartMessage::Reply::Stats stats_with_value0;
  addGaugeProto(stats_with_value0, "some.connected_state", 0);
  addGaugeProto(stats_with_value0, "server.live", 0);
  envoy::HotRestartMessage::Reply::Stats stats_with_value1;
  addGaugeProto(stats_with_value1, "some.connected_state", 1);
  addGaugeProto(stats_with_value1, "server.live", 1);

  // 0 || 0 == 0
  hot_restarting_child_.mergeParentStats(store_, stats_with_value0);
  EXPECT_EQ(0, store_.gauge("some.connected_state").value());
  EXPECT_EQ(0, store_.gauge("server.live").value());

  // 0 || 1 == 1
  hot_restarting_child_.mergeParentStats(store_, stats_with_value1);
  EXPECT_EQ(1, store_.gauge("some.connected_state").value());
  EXPECT_EQ(1, store_.gauge("server.live").value());

  // 1 || 0 == 1
  hot_restarting_child_.mergeParentStats(store_, stats_with_value0);
  EXPECT_EQ(1, store_.gauge("some.connected_state").value());
  EXPECT_EQ(1, store_.gauge("server.live").value());

  // 1 || 1 == 1
  hot_restarting_child_.mergeParentStats(store_, stats_with_value1);
  EXPECT_EQ(1, store_.gauge("some.connected_state").value());
  EXPECT_EQ(1, store_.gauge("server.live").value());
}

} // namespace
} // namespace Server
} // namespace Envoy
