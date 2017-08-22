#include "common/common/version.h"
#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

using testing::InSequence;
using testing::StrictMock;

namespace Envoy {
namespace Server {

TEST(ServerInstanceUtil, flushHelper) {
  InSequence s;

  Stats::IsolatedStoreImpl store;
  store.counter("hello").inc();
  store.gauge("world").set(5);
  std::unique_ptr<Stats::MockSink> sink(new StrictMock<Stats::MockSink>());
  EXPECT_CALL(*sink, beginFlush());
  EXPECT_CALL(*sink, flushCounter("hello", 1));
  EXPECT_CALL(*sink, flushGauge("world", 5));
  EXPECT_CALL(*sink, endFlush());

  std::list<Stats::SinkPtr> sinks;
  sinks.emplace_back(std::move(sink));
  InstanceUtil::flushCountersAndGaugesToSinks(sinks, store);
}

// Class creates minimally viable server instance for testing.
class ServerInstanceImplTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ServerInstanceImplTest() : version_(GetParam()) {}

  void initialize(const std::string& bootstrap_path) {
    options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
        "test/config/integration/server.json", {{"upstream_0", 0}, {"upstream_1", 0}}, version_);
    options_.bootstrap_path_ = TestEnvironment::runfilesPath(bootstrap_path);
    server_.reset(new InstanceImpl(
        options_,
        Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
        hooks_, restart_, stats_store_, fakelock_, component_factory_, thread_local_));
  }

  void TearDown() override {
    server_->threadLocal().shutdownGlobalThreading();
    server_->clusterManager().shutdown();
    server_->threadLocal().shutdownThread();
  }

  Network::Address::IpVersion version_;
  testing::NiceMock<MockOptions> options_;
  DefaultTestHooks hooks_;
  testing::NiceMock<MockHotRestart> restart_;
  ThreadLocal::InstanceImpl thread_local_;
  Stats::TestIsolatedStoreImpl stats_store_;
  Thread::MutexBasicLockable fakelock_;
  TestComponentFactory component_factory_;
  std::unique_ptr<InstanceImpl> server_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, ServerInstanceImplTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

TEST_P(ServerInstanceImplTest, Stats) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  initialize("test/server/empty_bootstrap.json");
  EXPECT_NE(nullptr, TestUtility::findCounter(stats_store_, "server.watchdog_miss"));
}

// Validate server localInfo() from bootstrap Node.
TEST_P(ServerInstanceImplTest, BootstrapNode) {
  initialize("test/server/node_bootstrap.json");
  EXPECT_EQ("bootstrap_zone", server_->localInfo().zoneName());
  EXPECT_EQ("bootstrap_cluster", server_->localInfo().clusterName());
  EXPECT_EQ("bootstrap_id", server_->localInfo().nodeName());
  EXPECT_EQ("bootstrap_sub_zone", server_->localInfo().node().locality().sub_zone());
  EXPECT_EQ(VersionInfo::version(), server_->localInfo().node().build_version());
}

// Validate server localInfo() from bootstrap Node with CLI overrides.
TEST_P(ServerInstanceImplTest, BootstrapNodeWithOptionsOverride) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.service_zone_name_ = "some_zone_name";
  initialize("test/server/node_bootstrap.json");
  EXPECT_EQ("some_zone_name", server_->localInfo().zoneName());
  EXPECT_EQ("some_cluster_name", server_->localInfo().clusterName());
  EXPECT_EQ("some_node_name", server_->localInfo().nodeName());
  EXPECT_EQ("bootstrap_sub_zone", server_->localInfo().node().locality().sub_zone());
  EXPECT_EQ(VersionInfo::version(), server_->localInfo().node().build_version());
}

} // namespace Server
} // namespace Envoy
