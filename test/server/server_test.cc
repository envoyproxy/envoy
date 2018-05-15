#include "common/common/version.h"
#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::Property;
using testing::Ref;
using testing::SaveArg;
using testing::StrictMock;
using testing::_;

namespace Envoy {
namespace Server {

TEST(ServerInstanceUtil, flushHelper) {
  InSequence s;

  Stats::IsolatedStoreImpl store;
  Stats::SourceImpl source(store);
  store.counter("hello").inc();
  store.gauge("world").set(5);
  std::unique_ptr<Stats::MockSink> sink(new StrictMock<Stats::MockSink>());
  EXPECT_CALL(*sink, flush(Ref(source))).WillOnce(Invoke([](Stats::Source& source) {
    ASSERT_EQ(source.cachedCounters().size(), 1);
    EXPECT_EQ(source.cachedCounters().front()->name(), "hello");
    EXPECT_EQ(source.cachedCounters().front()->latch(), 1);

    ASSERT_EQ(source.cachedGauges().size(), 1);
    EXPECT_EQ(source.cachedGauges().front()->name(), "world");
    EXPECT_EQ(source.cachedGauges().front()->value(), 5);
  }));

  std::list<Stats::SinkPtr> sinks;
  sinks.emplace_back(std::move(sink));
  InstanceUtil::flushMetricsToSinks(sinks, source);
}

class RunHelperTest : public testing::Test {
public:
  RunHelperTest() {
    InSequence s;

    sigterm_ = new Event::MockSignalEvent(&dispatcher_);
    sigusr1_ = new Event::MockSignalEvent(&dispatcher_);
    sighup_ = new Event::MockSignalEvent(&dispatcher_);
    EXPECT_CALL(cm_, setInitializedCb(_)).WillOnce(SaveArg<0>(&cm_init_callback_));

    helper_.reset(new RunHelper(dispatcher_, cm_, hot_restart_, access_log_manager_, init_manager_,
                                [this] { start_workers_.ready(); }));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<MockHotRestart> hot_restart_;
  NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  InitManagerImpl init_manager_;
  ReadyWatcher start_workers_;
  std::unique_ptr<RunHelper> helper_;
  std::function<void()> cm_init_callback_;
  Event::MockSignalEvent* sigterm_;
  Event::MockSignalEvent* sigusr1_;
  Event::MockSignalEvent* sighup_;
};

TEST_F(RunHelperTest, Normal) {
  EXPECT_CALL(start_workers_, ready());
  cm_init_callback_();
}

TEST_F(RunHelperTest, ShutdownBeforeCmInitialize) {
  EXPECT_CALL(start_workers_, ready()).Times(0);
  sigterm_->callback_();
  cm_init_callback_();
}

TEST_F(RunHelperTest, ShutdownBeforeInitManagerInit) {
  EXPECT_CALL(start_workers_, ready()).Times(0);
  Init::MockTarget target;
  init_manager_.registerTarget(target);
  EXPECT_CALL(target, initialize(_));
  cm_init_callback_();
  sigterm_->callback_();
  target.callback_();
}

// Class creates minimally viable server instance for testing.
class ServerInstanceImplTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ServerInstanceImplTest() : version_(GetParam()) {}

  void initialize(const std::string& bootstrap_path) {
    if (bootstrap_path.empty()) {
      options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
          "test/config/integration/server.json", {{"upstream_0", 0}, {"upstream_1", 0}}, version_);
    } else {
      options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
          bootstrap_path, {{"upstream_0", 0}, {"upstream_1", 0}}, version_);
    }
    server_.reset(new InstanceImpl(
        options_,
        Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
        hooks_, restart_, stats_store_, fakelock_, component_factory_,
        std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), thread_local_));

    EXPECT_TRUE(server_->api().fileExists("/dev/null"));
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
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(ServerInstanceImplTest, V2ConfigOnly) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.v2_config_only_ = true;
  try {
    initialize(std::string());
    FAIL();
  } catch (const EnvoyException& e) {
    EXPECT_THAT(e.what(), HasSubstr("Unable to parse JSON as proto"));
  }
}

TEST_P(ServerInstanceImplTest, V1ConfigFallback) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.v2_config_only_ = false;
  initialize(std::string());
}

TEST_P(ServerInstanceImplTest, Stats) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  initialize(std::string());
  EXPECT_NE(nullptr, TestUtility::findCounter(stats_store_, "server.watchdog_miss"));
}

// Validate server localInfo() from bootstrap Node.
TEST_P(ServerInstanceImplTest, BootstrapNode) {
  initialize("test/server/node_bootstrap.yaml");
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
  initialize("test/server/node_bootstrap.yaml");
  EXPECT_EQ("some_zone_name", server_->localInfo().zoneName());
  EXPECT_EQ("some_cluster_name", server_->localInfo().clusterName());
  EXPECT_EQ("some_node_name", server_->localInfo().nodeName());
  EXPECT_EQ("bootstrap_sub_zone", server_->localInfo().node().locality().sub_zone());
  EXPECT_EQ(VersionInfo::version(), server_->localInfo().node().build_version());
}

// Regression test for segfault when server initialization fails prior to
// ClusterManager initialization.
TEST_P(ServerInstanceImplTest, BootstrapClusterManagerInitializationFail) {
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/cluster_dupe_bootstrap.yaml"), EnvoyException,
                            "cluster manager: duplicate cluster 'service_google'");
}

// Negative test for protoc-gen-validate constraints.
TEST_P(ServerInstanceImplTest, ValidateFail) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.v2_config_only_ = true;
  try {
    initialize("test/server/empty_bootstrap.yaml");
    FAIL();
  } catch (const EnvoyException& e) {
    EXPECT_THAT(e.what(), HasSubstr("Proto constraint validation failed"));
  }
}

TEST_P(ServerInstanceImplTest, LogToFile) {
  const std::string path =
      TestEnvironment::temporaryPath("ServerInstanceImplTest_LogToFile_Test.log");
  options_.log_path_ = path;
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  initialize(std::string());
  EXPECT_TRUE(server_->api().fileExists(path));

  GET_MISC_LOGGER().set_level(spdlog::level::info);
  ENVOY_LOG_MISC(warn, "LogToFile test string");
  Logger::Registry::getSink()->flush();
  std::string log = server_->api().fileReadToEnd(path);
  EXPECT_GT(log.size(), 0);
  EXPECT_TRUE(log.find("LogToFile test string") != std::string::npos);

  // Test that critical messages get immediately flushed
  ENVOY_LOG_MISC(critical, "LogToFile second test string");
  log = server_->api().fileReadToEnd(path);
  EXPECT_TRUE(log.find("LogToFile second test string") != std::string::npos);
}

TEST_P(ServerInstanceImplTest, LogToFileError) {
  options_.log_path_ = "/this/path/does/not/exist";
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  try {
    initialize(std::string());
    FAIL();
  } catch (const EnvoyException& e) {
    EXPECT_THAT(e.what(), HasSubstr("Failed to open log-file"));
  }
}

TEST_P(ServerInstanceImplTest, NoOptionsPassed) {
  EXPECT_THROW_WITH_MESSAGE(
      server_.reset(new InstanceImpl(
          options_,
          Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
          hooks_, restart_, stats_store_, fakelock_, component_factory_,
          std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), thread_local_)),
      EnvoyException, "unable to read file: ")
}
} // namespace Server
} // namespace Envoy
