#include "common/common/version.h"
#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::_;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Property;
using testing::Ref;
using testing::SaveArg;
using testing::StrictMock;

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
    EXPECT_CALL(overload_manager_, start());

    helper_.reset(new RunHelper(dispatcher_, cm_, hot_restart_, access_log_manager_, init_manager_,
                                overload_manager_, [this] { start_workers_.ready(); }));
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<MockHotRestart> hot_restart_;
  NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  NiceMock<MockOverloadManager> overload_manager_;
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
        options_, test_time_.timeSystem(),
        Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
        hooks_, restart_, stats_store_, fakelock_, component_factory_,
        std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), thread_local_));

    EXPECT_TRUE(server_->api().fileExists("/dev/null"));
  }

  void initializeWithHealthCheckParams(const std::string& bootstrap_path, const double timeout,
                                       const double interval) {
    options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
        bootstrap_path,
        {{"health_check_timeout", fmt::format("{}", timeout).c_str()},
         {"health_check_interval", fmt::format("{}", interval).c_str()}},
        TestEnvironment::PortMap{}, version_);
    server_.reset(new InstanceImpl(
        options_, test_time_.timeSystem(),
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
  DangerousDeprecatedTestTime test_time_;
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
  options_.concurrency_ = 2;
  options_.hot_restart_epoch_ = 3;
  initialize(std::string());
  EXPECT_NE(nullptr, TestUtility::findCounter(stats_store_, "server.watchdog_miss"));
  EXPECT_EQ(2L, TestUtility::findGauge(stats_store_, "server.concurrency")->value());
  EXPECT_EQ(3L, TestUtility::findGauge(stats_store_, "server.hot_restart_epoch")->value());
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

// Test for protoc-gen-validate constraint on invalid timeout entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidTimeout) {
  options_.v2_config_only_ = true;
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams("test/server/cluster_health_check_bootstrap.yaml", 0, 0.25),
      EnvoyException,
      "HealthCheckValidationError.Timeout: \\[\"value must be greater than \" \"0s\"\\]");
}

// Test for protoc-gen-validate constraint on invalid interval entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidInterval) {
  options_.v2_config_only_ = true;
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams("test/server/cluster_health_check_bootstrap.yaml", 0.5, 0),
      EnvoyException,
      "HealthCheckValidationError.Interval: \\[\"value must be greater than \" \"0s\"\\]");
}

// Test for protoc-gen-validate constraint on invalid timeout and interval entry of a health check
// config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidTimeoutAndInterval) {
  options_.v2_config_only_ = true;
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams("test/server/cluster_health_check_bootstrap.yaml", 0, 0),
      EnvoyException,
      "HealthCheckValidationError.Timeout: \\[\"value must be greater than \" \"0s\"\\]");
}

// Test for protoc-gen-validate constraint on valid interval entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckValidTimeoutAndInterval) {
  options_.v2_config_only_ = true;
  EXPECT_NO_THROW(initializeWithHealthCheckParams("test/server/cluster_health_check_bootstrap.yaml",
                                                  0.25, 0.5));
}

// Test that a Bootstrap proto with no address specified in its Admin field can go through
// initialization properly, but without starting an admin listener.
TEST_P(ServerInstanceImplTest, BootstrapNodeNoAdmin) {
  EXPECT_NO_THROW(initialize("test/server/node_bootstrap_no_admin_port.yaml"));
  // Admin::addListenerToHandler() calls one of handler's methods after checking that the Admin
  // has a listener. So, the fact that passing a nullptr doesn't cause a segfault establishes
  // that there is no listener.
  server_->admin().addListenerToHandler(/*handler=*/nullptr);
}

// Validate that an admin config with a server address but no access log path is rejected.
TEST_P(ServerInstanceImplTest, BootstrapNodeWithoutAccessLog) {
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/node_bootstrap_without_access_log.yaml"),
                            EnvoyException,
                            "An admin access log path is required for a listening server.");
}

// Empty bootstrap succeeeds.
TEST_P(ServerInstanceImplTest, EmptyBootstrap) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.v2_config_only_ = true;
  EXPECT_NO_THROW(initialize("test/server/empty_bootstrap.yaml"));
}

// Negative test for protoc-gen-validate constraints.
TEST_P(ServerInstanceImplTest, ValidateFail) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.v2_config_only_ = true;
  try {
    initialize("test/server/empty_runtime.yaml");
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

// When there are no bootstrap CLI options, either for content or path, we can load the server with
// an empty config.
TEST_P(ServerInstanceImplTest, NoOptionsPassed) {
  EXPECT_NO_THROW(server_.reset(new InstanceImpl(
      options_, test_time_.timeSystem(),
      Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
      hooks_, restart_, stats_store_, fakelock_, component_factory_,
      std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), thread_local_)));
}

// Validate that when std::exception is unexpectedly thrown, we exit safely.
// This is a regression test for when we used to crash.
TEST_P(ServerInstanceImplTest, StdExceptionThrowInConstructor) {
  EXPECT_CALL(restart_, initialize(_, _)).WillOnce(InvokeWithoutArgs([] {
    throw(std::runtime_error("foobar"));
  }));
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/node_bootstrap.yaml"), std::runtime_error,
                            "foobar");
}

// Neither EnvoyException nor std::exception derived.
class FakeException {
public:
  FakeException(const std::string& what) : what_(what) {}
  const std::string& what() const { return what_; }

  const std::string what_;
};

// Validate that when a totally unknown exception is unexpectedly thrown, we
// exit safely. This is a regression test for when we used to crash.
TEST_P(ServerInstanceImplTest, UnknownExceptionThrowInConstructor) {
  EXPECT_CALL(restart_, initialize(_, _)).WillOnce(InvokeWithoutArgs([] {
    throw(FakeException("foobar"));
  }));
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/node_bootstrap.yaml"), FakeException, "foobar");
}

} // namespace Server
} // namespace Envoy
