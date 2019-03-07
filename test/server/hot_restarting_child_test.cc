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
  HotRestartingChildTest() :
  os_calls_injector_(InitOsCalls()),
  hot_restarting_child_(123, 456) {
    store_.counter("draculaer").inc();
    store_.gauge("whywassixafraidofseven").set(678);
    store_.gauge("some.sort.of.version").set(12345);
  }
  
  Api::MockOsSysCalls* InitOsCalls() {
    EXPECT_CALL(os_sys_calls_, bind(_, _, _)).Times(AnyNumber());
    return &os_sys_calls_;
  }

  Stats::IsolatedStoreImpl store_;
  HotRestartMessage::Reply::Stats stats_;
  Api::MockOsSysCalls os_sys_calls_;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls_injector_;
  HotRestartingChild hot_restarting_child_;
};

TEST_F(HotRestartingChildTest, basicDefaultImport) {
  auto* gauge_proto = stats_.mutable_gauges()->Add();
  gauge_proto->set_name("whywassixafraidofseven");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(111);

  auto* counter_proto = stats_.mutable_counters()->Add();
  counter_proto->set_name("draculaer");
  counter_proto->set_type(SimpleMetric::COUNTER);
  counter_proto->set_value(3);
  
  hot_restarting_child_.mergeParentStats(store_, stats_);
  
  EXPECT_EQ(789, store_.gauge("whywassixafraidofseven").value());
  EXPECT_EQ(4, store_.counter("draculaer").value());
}

TEST_F(HotRestartingChildTest, versionNotImported) {
  auto* gauge_proto = stats_.mutable_gauges()->Add();
  gauge_proto->set_name("some.sort.of.version");
  gauge_proto->set_type(SimpleMetric::GAUGE);
  gauge_proto->set_value(2345678);
  
  hot_restarting_child_.mergeParentStats(store_, stats_);
  
  EXPECT_EQ(12345, store_.gauge("some.sort.of.version").value());
}

} // namespace
} // namespace Server
} // namespace Envoy
