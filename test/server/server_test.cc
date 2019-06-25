#include <memory>

#include "common/common/assert.h"
#include "common/common/version.h"
#include "common/network/address_impl.h"
#include "common/thread_local/thread_local_impl.h"

#include "server/process_context_impl.h"
#include "server/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Assign;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::SaveArg;
using testing::StrictMock;

namespace Envoy {
namespace Server {
namespace {

TEST(ServerInstanceUtil, flushHelper) {
  InSequence s;

  Stats::IsolatedStoreImpl store;
  Stats::Counter& c = store.counter("hello");
  c.inc();
  store.gauge("world", Stats::Gauge::ImportMode::Accumulate).set(5);
  store.histogram("histogram");

  std::list<Stats::SinkPtr> sinks;
  InstanceUtil::flushMetricsToSinks(sinks, store);
  // Make sure that counters have been latched even if there are no sinks.
  EXPECT_EQ(1UL, c.value());
  EXPECT_EQ(0, c.latch());

  Stats::MockSink* sink = new StrictMock<Stats::MockSink>();
  sinks.emplace_back(sink);
  EXPECT_CALL(*sink, flush(_)).WillOnce(Invoke([](Stats::MetricSnapshot& snapshot) {
    ASSERT_EQ(snapshot.counters().size(), 1);
    EXPECT_EQ(snapshot.counters()[0].counter_.get().name(), "hello");
    EXPECT_EQ(snapshot.counters()[0].delta_, 1);

    ASSERT_EQ(snapshot.gauges().size(), 1);
    EXPECT_EQ(snapshot.gauges()[0].get().name(), "world");
    EXPECT_EQ(snapshot.gauges()[0].get().value(), 5);
  }));
  c.inc();
  InstanceUtil::flushMetricsToSinks(sinks, store);

  // Histograms don't currently work with the isolated store so test those with a mock store.
  NiceMock<Stats::MockStore> mock_store;
  Stats::ParentHistogramSharedPtr parent_histogram(new Stats::MockParentHistogram());
  std::vector<Stats::ParentHistogramSharedPtr> parent_histograms = {parent_histogram};
  ON_CALL(mock_store, histograms).WillByDefault(Return(parent_histograms));
  EXPECT_CALL(*sink, flush(_)).WillOnce(Invoke([](Stats::MetricSnapshot& snapshot) {
    EXPECT_TRUE(snapshot.counters().empty());
    EXPECT_TRUE(snapshot.gauges().empty());
    EXPECT_EQ(snapshot.histograms().size(), 1);
  }));
  InstanceUtil::flushMetricsToSinks(sinks, mock_store);
}

class RunHelperTest : public testing::Test {
public:
  RunHelperTest() {
    InSequence s;

    sigterm_ = new Event::MockSignalEvent(&dispatcher_);
    sigint_ = new Event::MockSignalEvent(&dispatcher_);
    sigusr1_ = new Event::MockSignalEvent(&dispatcher_);
    sighup_ = new Event::MockSignalEvent(&dispatcher_);
    EXPECT_CALL(overload_manager_, start());
    EXPECT_CALL(cm_, setInitializedCb(_)).WillOnce(SaveArg<0>(&cm_init_callback_));
    ON_CALL(server_, shutdown()).WillByDefault(Assign(&shutdown_, true));

    helper_ = std::make_unique<RunHelper>(server_, options_, dispatcher_, cm_, access_log_manager_,
                                          init_manager_, overload_manager_,
                                          [this] { start_workers_.ready(); });
  }

  NiceMock<MockInstance> server_;
  testing::NiceMock<MockOptions> options_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  NiceMock<MockOverloadManager> overload_manager_;
  Init::ManagerImpl init_manager_{""};
  ReadyWatcher start_workers_;
  std::unique_ptr<RunHelper> helper_;
  std::function<void()> cm_init_callback_;
  Event::MockSignalEvent* sigterm_;
  Event::MockSignalEvent* sigint_;
  Event::MockSignalEvent* sigusr1_;
  Event::MockSignalEvent* sighup_;
  bool shutdown_ = false;
};

TEST_F(RunHelperTest, Normal) {
  EXPECT_CALL(start_workers_, ready());
  cm_init_callback_();
}

TEST_F(RunHelperTest, ShutdownBeforeCmInitialize) {
  EXPECT_CALL(start_workers_, ready()).Times(0);
  sigterm_->callback_();
  EXPECT_CALL(server_, isShutdown()).WillOnce(Return(shutdown_));
  cm_init_callback_();
}

TEST_F(RunHelperTest, ShutdownBeforeInitManagerInit) {
  EXPECT_CALL(start_workers_, ready()).Times(0);
  Init::ExpectableTargetImpl target;
  init_manager_.add(target);
  EXPECT_CALL(target, initialize());
  cm_init_callback_();
  sigterm_->callback_();
  EXPECT_CALL(server_, isShutdown()).WillOnce(Return(shutdown_));
  target.ready();
}

class InitializingInitManager : public Init::ManagerImpl {
public:
  InitializingInitManager(absl::string_view name) : Init::ManagerImpl(name) {}

  State state() const override { return State::Initializing; }
};

class InitializingInstanceImpl : public InstanceImpl {
private:
  InitializingInitManager init_manager_{"Server"};

public:
  InitializingInstanceImpl(const Options& options, Event::TimeSystem& time_system,
                           Network::Address::InstanceConstSharedPtr local_address,
                           ListenerHooks& hooks, HotRestart& restarter, Stats::StoreRoot& store,
                           Thread::BasicLockable& access_log_lock,
                           ComponentFactory& component_factory,
                           Runtime::RandomGeneratorPtr&& random_generator,
                           ThreadLocal::Instance& tls, Thread::ThreadFactory& thread_factory,
                           Filesystem::Instance& file_system,
                           std::unique_ptr<ProcessContext> process_context)
      : InstanceImpl(options, time_system, local_address, hooks, restarter, store, access_log_lock,
                     component_factory, std::move(random_generator), tls, thread_factory,
                     file_system, std::move(process_context)) {}

  Init::Manager& initManager() override { return init_manager_; }
};

// Class creates minimally viable server instance for testing.
class ServerInstanceImplTest : public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ServerInstanceImplTest() : version_(GetParam()) {}

  void initialize(const std::string& bootstrap_path) { initialize(bootstrap_path, false); }

  void initialize(const std::string& bootstrap_path, const bool use_intializing_instance) {
    if (bootstrap_path.empty()) {
      options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
          "test/config/integration/server.json", {{"upstream_0", 0}, {"upstream_1", 0}}, version_);
    } else {
      options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
          bootstrap_path, {{"upstream_0", 0}, {"upstream_1", 0}}, version_);
    }
    thread_local_ = std::make_unique<ThreadLocal::InstanceImpl>();
    if (process_object_ != nullptr) {
      process_context_ = std::make_unique<ProcessContextImpl>(*process_object_);
    }
    if (use_intializing_instance) {
      server_ = std::make_unique<InitializingInstanceImpl>(
          options_, test_time_.timeSystem(),
          Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
          hooks_, restart_, stats_store_, fakelock_, component_factory_,
          std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), *thread_local_,
          Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
          std::move(process_context_));

    } else {
      server_ = std::make_unique<InstanceImpl>(
          options_, test_time_.timeSystem(),
          Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
          hooks_, restart_, stats_store_, fakelock_, component_factory_,
          std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), *thread_local_,
          Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
          std::move(process_context_));
    }

    EXPECT_TRUE(server_->api().fileSystem().fileExists("/dev/null"));
  }

  void initializeWithHealthCheckParams(const std::string& bootstrap_path, const double timeout,
                                       const double interval) {
    options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
        bootstrap_path,
        {{"health_check_timeout", fmt::format("{}", timeout).c_str()},
         {"health_check_interval", fmt::format("{}", interval).c_str()}},
        TestEnvironment::PortMap{}, version_);
    thread_local_ = std::make_unique<ThreadLocal::InstanceImpl>();
    server_ = std::make_unique<InstanceImpl>(
        options_, test_time_.timeSystem(),
        Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
        hooks_, restart_, stats_store_, fakelock_, component_factory_,
        std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), *thread_local_,
        Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(), nullptr);

    EXPECT_TRUE(server_->api().fileSystem().fileExists("/dev/null"));
  }

  Thread::ThreadPtr startTestServer(const std::string& bootstrap_path,
                                    const bool use_intializing_instance) {
    absl::Notification started;

    auto server_thread = Thread::threadFactoryForTest().createThread([&] {
      initialize(bootstrap_path, use_intializing_instance);
      auto startup_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::Startup,
                                                      [&] { started.Notify(); });
      auto shutdown_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                                       [&](Event::PostCb) { FAIL(); });
      shutdown_handle = nullptr; // unregister callback
      server_->run();
      startup_handle = nullptr;
      server_ = nullptr;
      thread_local_ = nullptr;
    });

    started.WaitForNotification();
    return server_thread;
  }

  // Returns the server's tracer as a pointer, for use in dynamic_cast tests.
  Tracing::HttpTracer* tracer() { return &server_->httpContext().tracer(); };

  Network::Address::IpVersion version_;
  testing::NiceMock<MockOptions> options_;
  DefaultListenerHooks hooks_;
  testing::NiceMock<MockHotRestart> restart_;
  std::unique_ptr<ThreadLocal::InstanceImpl> thread_local_;
  Stats::TestIsolatedStoreImpl stats_store_;
  Thread::MutexBasicLockable fakelock_;
  TestComponentFactory component_factory_;
  DangerousDeprecatedTestTime test_time_;
  ProcessObject* process_object_ = nullptr;
  std::unique_ptr<ProcessContextImpl> process_context_;
  std::unique_ptr<InstanceImpl> server_;
};

// Custom StatsSink that just increments a counter when flush is called.
class CustomStatsSink : public Stats::Sink {
public:
  CustomStatsSink(Stats::Scope& scope) : stats_flushed_(scope.counter("stats.flushed")) {}

  // Stats::Sink
  void flush(Stats::MetricSnapshot&) override { stats_flushed_.inc(); }

  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  Stats::Counter& stats_flushed_;
};

// Custom StatsSinFactory that creates CustomStatsSink.
class CustomStatsSinkFactory : public Server::Configuration::StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message&, Server::Instance& server) override {
    return std::make_unique<CustomStatsSink>(server.stats());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }

  std::string name() override { return "envoy.custom_stats_sink"; }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ServerInstanceImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

/**
 * Static registration for the custom sink factory. @see RegisterFactory.
 */
REGISTER_FACTORY(CustomStatsSinkFactory, Server::Configuration::StatsSinkFactory);

// Validates that server stats are flushed even when server is stuck with initialization.
TEST_P(ServerInstanceImplTest, StatsFlushWhenServerIsStillInitializing) {
  auto server_thread = startTestServer("test/server/stats_sink_bootstrap.yaml", true);

  // Wait till stats are flushed to custom sink and validate that the actual flush happens.
  TestUtility::waitForCounterEq(stats_store_, "stats.flushed", 1, test_time_.timeSystem());
  EXPECT_EQ(3L, TestUtility::findGauge(stats_store_, "server.state")->value());
  EXPECT_EQ(Init::Manager::State::Initializing, server_->initManager().state());

  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

TEST_P(ServerInstanceImplTest, EmptyShutdownLifecycleNotifications) {
  auto server_thread = startTestServer("test/server/node_bootstrap.yaml", false);
  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

TEST_P(ServerInstanceImplTest, LifecycleNotifications) {
  bool startup = false, shutdown = false, shutdown_with_completion = false;
  absl::Notification started, shutdown_begin, completion_block, completion_done;

  // Run the server in a separate thread so we can test different lifecycle stages.
  auto server_thread = Thread::threadFactoryForTest().createThread([&] {
    initialize("test/server/node_bootstrap.yaml");
    auto handle1 = server_->registerCallback(ServerLifecycleNotifier::Stage::Startup, [&] {
      startup = true;
      started.Notify();
    });
    auto handle2 = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit, [&] {
      shutdown = true;
      shutdown_begin.Notify();
    });
    auto handle3 = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                             [&](Event::PostCb completion_cb) {
                                               // Block till we're told to complete
                                               completion_block.WaitForNotification();
                                               shutdown_with_completion = true;
                                               server_->dispatcher().post(completion_cb);
                                               completion_done.Notify();
                                             });
    auto handle4 =
        server_->registerCallback(ServerLifecycleNotifier::Stage::Startup, [&] { FAIL(); });
    handle4 = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                        [&](Event::PostCb) { FAIL(); });
    handle4 = nullptr;

    server_->run();
    handle1 = nullptr;
    handle2 = nullptr;
    handle3 = nullptr;
    server_ = nullptr;
    thread_local_ = nullptr;
  });

  started.WaitForNotification();
  EXPECT_TRUE(startup);
  EXPECT_FALSE(shutdown);

  server_->dispatcher().post([&] { server_->shutdown(); });
  shutdown_begin.WaitForNotification();
  EXPECT_TRUE(shutdown);

  // Expect the server to block waiting for the completion callback to be invoked
  EXPECT_FALSE(completion_done.WaitForNotificationWithTimeout(absl::Seconds(1)));

  completion_block.Notify();
  completion_done.WaitForNotification();
  EXPECT_TRUE(shutdown_with_completion);

  server_thread->join();
}

TEST_P(ServerInstanceImplTest, V2ConfigOnly) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  try {
    initialize(std::string());
    FAIL();
  } catch (const EnvoyException& e) {
    EXPECT_THAT(e.what(), HasSubstr("Unable to parse JSON as proto"));
  }
}

TEST_P(ServerInstanceImplTest, Stats) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.concurrency_ = 2;
  options_.hot_restart_epoch_ = 3;
  EXPECT_NO_THROW(initialize("test/server/empty_bootstrap.yaml"));
  EXPECT_NE(nullptr, TestUtility::findCounter(stats_store_, "server.watchdog_miss"));
  EXPECT_EQ(2L, TestUtility::findGauge(stats_store_, "server.concurrency")->value());
  EXPECT_EQ(3L, TestUtility::findGauge(stats_store_, "server.hot_restart_epoch")->value());

// This stat only works in this configuration.
#if defined(NDEBUG) && defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
  ASSERT(false, "Testing debug assertion failure detection in release build.");
  EXPECT_EQ(1L, TestUtility::findCounter(stats_store_, "server.debug_assertion_failures")->value());
#else
  EXPECT_EQ(0L, TestUtility::findCounter(stats_store_, "server.debug_assertion_failures")->value());
#endif
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

// Validate server runtime is parsed from bootstrap and that we can read from
// service cluster specified disk-based overrides.
TEST_P(ServerInstanceImplTest, BootstrapRuntime) {
  options_.service_cluster_name_ = "some_service";
  initialize("test/server/runtime_bootstrap.yaml");
  EXPECT_EQ("bar", server_->runtime().snapshot().get("foo"));
  // This should access via the override/some_service overlay.
  EXPECT_EQ("fozz", server_->runtime().snapshot().get("fizz"));
}

// Validate that a runtime absent an admin layer will fail mutating operations
// but still support inspection of runtime values.
TEST_P(ServerInstanceImplTest, RuntimeNoAdminLayer) {
  options_.service_cluster_name_ = "some_service";
  initialize("test/server/runtime_bootstrap.yaml");
  Http::TestHeaderMapImpl response_headers;
  std::string response_body;
  EXPECT_EQ(Http::Code::OK,
            server_->admin().request("/runtime", "GET", response_headers, response_body));
  EXPECT_THAT(response_body, HasSubstr("fozz"));
  EXPECT_EQ(
      Http::Code::ServiceUnavailable,
      server_->admin().request("/runtime_modify?foo=bar", "POST", response_headers, response_body));
  EXPECT_EQ("No admin layer specified", response_body);
}

// Validate invalid runtime in bootstrap is rejected.
TEST_P(ServerInstanceImplTest, InvalidBootstrapRuntime) {
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/invalid_runtime_bootstrap.yaml"),
                            EnvoyException, "Invalid runtime entry value for foo");
}

// Validate invalid layered runtime missing a name is rejected.
TEST_P(ServerInstanceImplTest, InvalidLayeredBootstrapMissingName) {
  EXPECT_THROW_WITH_REGEX(initialize("test/server/invalid_layered_runtime_missing_name.yaml"),
                          EnvoyException,
                          "RuntimeLayerValidationError.Name: \\[\"value length must be at least");
}

// Validate invalid layered runtime with duplicate names is rejected.
TEST_P(ServerInstanceImplTest, InvalidLayeredBootstrapDuplicateName) {
  EXPECT_THROW_WITH_REGEX(initialize("test/server/invalid_layered_runtime_duplicate_name.yaml"),
                          EnvoyException, "Duplicate layer name: some_static_laye");
}

// Regression test for segfault when server initialization fails prior to
// ClusterManager initialization.
TEST_P(ServerInstanceImplTest, BootstrapClusterManagerInitializationFail) {
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/cluster_dupe_bootstrap.yaml"), EnvoyException,
                            "cluster manager: duplicate cluster 'service_google'");
}

// Test for protoc-gen-validate constraint on invalid timeout entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidTimeout) {
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams("test/server/cluster_health_check_bootstrap.yaml", 0, 0.25),
      EnvoyException,
      "HealthCheckValidationError.Timeout: \\[\"value must be greater than \" \"0s\"\\]");
}

// Test for protoc-gen-validate constraint on invalid interval entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidInterval) {
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams("test/server/cluster_health_check_bootstrap.yaml", 0.5, 0),
      EnvoyException,
      "HealthCheckValidationError.Interval: \\[\"value must be greater than \" \"0s\"\\]");
}

// Test for protoc-gen-validate constraint on invalid timeout and interval entry of a health check
// config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidTimeoutAndInterval) {
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams("test/server/cluster_health_check_bootstrap.yaml", 0, 0),
      EnvoyException,
      "HealthCheckValidationError.Timeout: \\[\"value must be greater than \" \"0s\"\\]");
}

// Test for protoc-gen-validate constraint on valid interval entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckValidTimeoutAndInterval) {
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

// Empty bootstrap succeeds.
TEST_P(ServerInstanceImplTest, EmptyBootstrap) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/empty_bootstrap.yaml"));
}

// Negative test for protoc-gen-validate constraints.
TEST_P(ServerInstanceImplTest, ValidateFail) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  try {
    initialize("test/server/invalid_bootstrap.yaml");
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
  EXPECT_NO_THROW(initialize("test/server/empty_bootstrap.yaml"));
  EXPECT_TRUE(server_->api().fileSystem().fileExists(path));

  GET_MISC_LOGGER().set_level(spdlog::level::info);
  ENVOY_LOG_MISC(warn, "LogToFile test string");
  Logger::Registry::getSink()->flush();
  std::string log = server_->api().fileSystem().fileReadToEnd(path);
  EXPECT_GT(log.size(), 0);
  EXPECT_TRUE(log.find("LogToFile test string") != std::string::npos);

  // Test that critical messages get immediately flushed
  ENVOY_LOG_MISC(critical, "LogToFile second test string");
  log = server_->api().fileSystem().fileReadToEnd(path);
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
  thread_local_ = std::make_unique<ThreadLocal::InstanceImpl>();
  EXPECT_THROW_WITH_MESSAGE(
      server_.reset(new InstanceImpl(
          options_, test_time_.timeSystem(),
          Network::Address::InstanceConstSharedPtr(new Network::Address::Ipv4Instance("127.0.0.1")),
          hooks_, restart_, stats_store_, fakelock_, component_factory_,
          std::make_unique<NiceMock<Runtime::MockRandomGenerator>>(), *thread_local_,
          Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(), nullptr)),
      EnvoyException, "At least one of --config-path and --config-yaml should be non-empty");
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

TEST_P(ServerInstanceImplTest, MutexContentionEnabled) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.mutex_tracing_enabled_ = true;
  EXPECT_NO_THROW(initialize("test/server/empty_bootstrap.yaml"));
}

TEST_P(ServerInstanceImplTest, NoHttpTracing) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/empty_bootstrap.yaml"));
  EXPECT_NE(nullptr, dynamic_cast<Tracing::HttpNullTracer*>(tracer()));
  EXPECT_EQ(nullptr, dynamic_cast<Tracing::HttpTracerImpl*>(tracer()));
}

TEST_P(ServerInstanceImplTest, ZipkinHttpTracingEnabled) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/zipkin_tracing.yaml"));
  EXPECT_EQ(nullptr, dynamic_cast<Tracing::HttpNullTracer*>(tracer()));

  // Note: there is no ZipkinTracerImpl object;
  // source/extensions/tracers/zipkin/config.cc instantiates the tracer with
  //     std::make_unique<Tracing::HttpTracerImpl>(std::move(zipkin_driver), server.localInfo());
  // so we look for a successful dynamic cast to HttpTracerImpl, rather
  // than HttpNullTracer.
  EXPECT_NE(nullptr, dynamic_cast<Tracing::HttpTracerImpl*>(tracer()));
}

class TestObject : public ProcessObject {
public:
  void setFlag(bool value) { boolean_flag_ = value; }

  bool boolean_flag_ = true;
};

TEST_P(ServerInstanceImplTest, WithProcessContext) {
  TestObject object;
  process_object_ = &object;

  EXPECT_NO_THROW(initialize("test/server/empty_bootstrap.yaml"));

  ProcessContext& context = server_->processContext();
  auto& object_from_context = dynamic_cast<TestObject&>(context.get());
  EXPECT_EQ(&object_from_context, &object);
  EXPECT_TRUE(object_from_context.boolean_flag_);

  object.boolean_flag_ = false;
  EXPECT_FALSE(object_from_context.boolean_flag_);
}

} // namespace
} // namespace Server
} // namespace Envoy
