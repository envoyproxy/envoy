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

using HotRestartMessage = envoy::api::v2::core::HotRestartMessage;
using SimpleMetric = envoy::admin::v2alpha::SimpleMetric;

class HotRestartingChildTest : public testing::Test {
public:
  HotRestartingChildTest() : os_calls_injector_(InitOsCalls()), hot_restarting_child_(123, 456) {
    store_.counter("draculaer").inc();
    store_.gauge("whywassixafraidofseven").set(678);
    store_.gauge("some.sort.of.version").set(12345);
  }

  Api::MockOsSysCalls* InitOsCalls() {
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(AnyNumber());
    return &os_sys_calls_;
  }

  Stats::IsolatedStoreImpl store_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_injector_;
  HotRestartingChild hot_restarting_child_;
};

TEST_F(HotRestartingChildTest, basicDefaultImport) {
  HotRestartMessage::Reply::Stats stats;
  auto* gauge_proto = stats.mutable_gauges()->Add();
  gauge_proto->set_name("whywassixafraidofseven");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(111);

  auto* counter_proto = stats.mutable_counters()->Add();
  counter_proto->set_name("draculaer");
  counter_proto->set_type(SimpleMetric::COUNTER);
  counter_proto->set_value(3);

  hot_restarting_child_.mergeParentStats(store_, stats);

  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(4, store_.counter("draculaer").value());
}

TEST_F(HotRestartingChildTest, multipleImports) {
  HotRestartMessage::Reply::Stats stats1;
  auto* gauge_proto = stats1.mutable_gauges()->Add();
  gauge_proto->set_name("whywassixafraidofseven");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(100);
  auto* counter_proto = stats1.mutable_counters()->Add();
  counter_proto->set_name("draculaer");
  counter_proto->set_type(SimpleMetric::COUNTER);
  counter_proto->set_value(2);
  hot_restarting_child_.mergeParentStats(store_, stats1);
  // Initial combined values: 678+100 and 1+2.
  EXPECT_EQ(778, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(3, store_.counter("draculaer").value());

  // The parent's gauge drops by 1, and its counter increases by 1.
  HotRestartMessage::Reply::Stats stats2;
  gauge_proto = stats2.mutable_gauges()->Add();
  gauge_proto->set_name("whywassixafraidofseven");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(99);
  counter_proto = stats2.mutable_counters()->Add();
  counter_proto->set_name("draculaer");
  counter_proto->set_type(SimpleMetric::COUNTER);
  counter_proto->set_value(3);
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
  HotRestartMessage::Reply::Stats stats3;
  gauge_proto = stats3.mutable_gauges()->Add();
  gauge_proto->set_name("whywassixafraidofseven");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(104);
  counter_proto = stats3.mutable_counters()->Add();
  counter_proto->set_name("draculaer");
  counter_proto->set_type(SimpleMetric::COUNTER);
  counter_proto->set_value(4);
  hot_restarting_child_.mergeParentStats(store_, stats3);
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(10, store_.counter("draculaer").value());
}

// Tests that the substring ".version" excludes a parent stat from being imported, along
// with any bool indicator with the default combination logic (which defaults to NoImport).
TEST_F(HotRestartingChildTest, exclusionsNotImported) {
  HotRestartMessage::Reply::Stats stats;
  auto* gauge_proto = stats.mutable_gauges()->Add();
  gauge_proto->set_name("some.sort.of.version");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(2345678);
  gauge_proto = stats.mutable_gauges()->Add();
  gauge_proto->set_name("child.doesnt.have.this.version");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(111);
  auto* indicator_proto = stats.mutable_indicators()->Add();
  indicator_proto->set_name("my.connected_state");
  indicator_proto->set_type(SimpleMetric::BOOL_INDICATOR);
  indicator_proto->set_value(true);
  indicator_proto = stats.mutable_indicators()->Add();
  indicator_proto->set_name("my.absolutely_any_bool_indicator");
  indicator_proto->set_type(SimpleMetric::BOOL_INDICATOR);
  indicator_proto->set_value(true);

  hot_restarting_child_.mergeParentStats(store_, stats);

  EXPECT_EQ(12345, store_.gauge("some.sort.of.version").value());
  EXPECT_FALSE(store_.gauge("child.doesnt.have.this.version").used());
  EXPECT_FALSE(store_.boolIndicator("my.connected_state").used());
  EXPECT_FALSE(store_.boolIndicator("my.absolutely_any_bool_indicator").used());
}

} // namespace
} // namespace Server
} // namespace Envoy
