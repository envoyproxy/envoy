#include <algorithm>
#include <memory>
#include <vector>

#include "envoy/common/scope_tracker.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"
#include "envoy/server/bootstrap_extension_config.h"
#include "envoy/server/fatal_action_config.h"

#include "source/common/common/assert.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/common/version/version.h"
#include "source/server/instance_impl.h"
#include "source/server/process_context_impl.h"

#include "test/common/config/dummy_config.pb.h"
#include "test/common/stats/stat_test_utility.h"
#include "test/config/v2_link_hacks.h"
#include "test/integration/server.h"
#include "test/mocks/common.h"
#include "test/mocks/server/bootstrap_extension_factory.h"
#include "test/mocks/server/fatal_action_factory.h"
#include "test/mocks/server/hot_restart.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/options.h"
#include "test/mocks/server/overload_manager.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/logging.h"
#include "test/test_common/registry.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "openssl/crypto.h"

using testing::_;
using testing::Assign;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::SaveArg;
using testing::StrictMock;

namespace Envoy {
namespace Server {
namespace {

TEST(ServerInstanceUtil, flushHelper) {
  InSequence s;

  NiceMock<Upstream::MockClusterManager> cm;
  Stats::TestUtil::TestStore store;
  Event::SimulatedTimeSystem time_system;
  Stats::Counter& c = store.counter("hello");
  c.inc();
  store.gauge("world", Stats::Gauge::ImportMode::Accumulate).set(5);
  store.histogram("histogram", Stats::Histogram::Unit::Unspecified);
  store.textReadout("text").set("is important");

  std::list<Stats::SinkPtr> sinks;
  InstanceUtil::flushMetricsToSinks(sinks, store, cm, time_system);
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

    ASSERT_EQ(snapshot.textReadouts().size(), 1);
    EXPECT_EQ(snapshot.textReadouts()[0].get().name(), "text");
    EXPECT_EQ(snapshot.textReadouts()[0].get().value(), "is important");
  }));
  c.inc();
  InstanceUtil::flushMetricsToSinks(sinks, store, cm, time_system);

  // Histograms don't currently work with the isolated store so test those with a mock store.
  NiceMock<Stats::MockStore> mock_store;
  Stats::ParentHistogramSharedPtr parent_histogram(new Stats::MockParentHistogram());
  std::vector<Stats::ParentHistogramSharedPtr> parent_histograms = {parent_histogram};
  ON_CALL(mock_store, forEachSinkedHistogram)
      .WillByDefault([&](std::function<void(std::size_t)> f_size,
                         std::function<void(Stats::ParentHistogram&)> f_stat) {
        if (f_size != nullptr) {
          f_size(parent_histograms.size());
        }
        for (auto& histogram : parent_histograms) {
          f_stat(*histogram);
        }
      });
  EXPECT_CALL(*sink, flush(_)).WillOnce(Invoke([](Stats::MetricSnapshot& snapshot) {
    EXPECT_TRUE(snapshot.counters().empty());
    EXPECT_TRUE(snapshot.gauges().empty());
    EXPECT_EQ(snapshot.histograms().size(), 1);
    EXPECT_TRUE(snapshot.textReadouts().empty());
  }));
  InstanceUtil::flushMetricsToSinks(sinks, mock_store, cm, time_system);
}

TEST(ServerInstanceUtil, flushImportModeUninitializedGauges) {
  InSequence s;

  NiceMock<Upstream::MockClusterManager> cm;
  Stats::TestUtil::TestStore store;
  Event::SimulatedTimeSystem time_system;
  Stats::Counter& c = store.counter("hello");
  c.inc();
  store.gauge("world", Stats::Gauge::ImportMode::Accumulate).set(5);
  store.gauge("again", Stats::Gauge::ImportMode::Uninitialized).set(10);

  std::list<Stats::SinkPtr> sinks;
  InstanceUtil::flushMetricsToSinks(sinks, store, cm, time_system);
  // Make sure that counters have been latched even if there are no sinks.
  EXPECT_EQ(1UL, c.value());
  EXPECT_EQ(0, c.latch());

  Stats::MockSink* sink = new StrictMock<Stats::MockSink>();
  sinks.emplace_back(sink);
  EXPECT_CALL(*sink, flush(_)).WillOnce(Invoke([](Stats::MetricSnapshot& snapshot) {
    ASSERT_EQ(snapshot.counters().size(), 1);
    EXPECT_EQ(snapshot.counters()[0].counter_.get().name(), "hello");
    EXPECT_EQ(snapshot.counters()[0].delta_, 1);

    ASSERT_EQ(snapshot.gauges().size(), 2);
    auto world_it = std::find_if(snapshot.gauges().begin(), snapshot.gauges().end(),
                                 [](const std::reference_wrapper<const Stats::Gauge>& gauge) {
                                   return gauge.get().name().compare("world") == 0;
                                 });
    ASSERT_TRUE(world_it != snapshot.gauges().end());
    EXPECT_EQ(world_it->get().value(), 5);

    auto again_it = std::find_if(snapshot.gauges().begin(), snapshot.gauges().end(),
                                 [](const std::reference_wrapper<const Stats::Gauge>& gauge) {
                                   return gauge.get().name().compare("again") == 0;
                                 });
    ASSERT_TRUE(again_it != snapshot.gauges().end());
    EXPECT_EQ(again_it->get().value(), 10);

    ASSERT_EQ(snapshot.textReadouts().size(), 0);
  }));
  c.inc();
  InstanceUtil::flushMetricsToSinks(sinks, store, cm, time_system);
}

class RunHelperTest : public testing::Test {
public:
  RunHelperTest() {
    InSequence s;

#ifndef WIN32
    sigterm_ = new Event::MockSignalEvent(&dispatcher_);
    sigint_ = new Event::MockSignalEvent(&dispatcher_);
    sigusr1_ = new Event::MockSignalEvent(&dispatcher_);
    sighup_ = new Event::MockSignalEvent(&dispatcher_);
#endif
    EXPECT_CALL(overload_manager_, start());
    EXPECT_CALL(cm_, setInitializedCb(_)).WillOnce(SaveArg<0>(&cm_init_callback_));
    ON_CALL(server_, shutdown()).WillByDefault(Assign(&shutdown_, true));

    helper_ = std::make_unique<RunHelper>(server_, options_, dispatcher_, cm_, access_log_manager_,
                                          init_manager_, overload_manager_, null_overload_manager_,
                                          [this] { start_workers_.ready(); });
  }

  NiceMock<MockInstance> server_;
  testing::NiceMock<MockOptions> options_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cm_;
  NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;
  NiceMock<MockOverloadManager> overload_manager_;
  NiceMock<MockOverloadManager> null_overload_manager_;
  Init::ManagerImpl init_manager_{""};
  ReadyWatcher start_workers_;
  std::unique_ptr<RunHelper> helper_;
  std::function<void()> cm_init_callback_;
#ifndef WIN32
  Event::MockSignalEvent* sigterm_;
  Event::MockSignalEvent* sigint_;
  Event::MockSignalEvent* sigusr1_;
  Event::MockSignalEvent* sighup_;
#endif
  bool shutdown_ = false;
};

TEST_F(RunHelperTest, Normal) {
  EXPECT_CALL(start_workers_, ready());
  cm_init_callback_();
}

// no signals on Windows
#ifndef WIN32
TEST_F(RunHelperTest, ShutdownBeforeCmInitialize) {
  EXPECT_CALL(start_workers_, ready()).Times(0);
  sigterm_->callback_();
  EXPECT_CALL(server_, isShutdown()).WillOnce(Return(shutdown_));
  cm_init_callback_();
}
#endif

// no signals on Windows
#ifndef WIN32
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
#endif

class InitializingInitManager : public Init::ManagerImpl {
public:
  InitializingInitManager(absl::string_view name) : Init::ManagerImpl(name) {}

  State state() const override { return State::Initializing; }
};

// Class creates minimally viable server instance for testing.
class ServerInstanceImplTestBase {
protected:
  void initialize(const std::string& bootstrap_path) { initialize(bootstrap_path, false); }

  void initialize(const std::string& bootstrap_path, const bool use_intializing_instance) {
    initialize(bootstrap_path, use_intializing_instance, hooks_);
  }

  void initialize(const std::string& bootstrap_path, const bool use_intializing_instance,
                  ListenerHooks& hooks) {
    if (options_.config_path_.empty()) {
      options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
          bootstrap_path, {{"upstream_0", 0}, {"upstream_1", 0}}, version_);
    }
    thread_local_ = std::make_unique<ThreadLocal::InstanceImpl>();
    if (process_object_ != nullptr) {
      process_context_ = std::make_unique<ProcessContextImpl>(*process_object_);
    }
    init_manager_ = use_intializing_instance ? std::make_unique<InitializingInitManager>("Server")
                                             : std::make_unique<Init::ManagerImpl>("Server");

    server_ = std::make_unique<InstanceImpl>(
        *init_manager_, options_, time_system_, hooks, restart_, stats_store_, fakelock_,
        std::make_unique<NiceMock<Random::MockRandomGenerator>>(), *thread_local_,
        Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(),
        std::move(process_context_));
    server_->initialize(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"),
                        component_factory_);
    EXPECT_TRUE(server_->api().fileSystem().fileExists(std::string(Platform::null_device_path)));
  }

  void initializeWithHealthCheckParams(const std::string& bootstrap_path, const double timeout,
                                       const double interval) {
    options_.config_path_ = TestEnvironment::temporaryFileSubstitute(
        bootstrap_path,
        {{"health_check_timeout", fmt::format("{}", timeout).c_str()},
         {"health_check_interval", fmt::format("{}", interval).c_str()}},
        TestEnvironment::PortMap{}, version_);
    thread_local_ = std::make_unique<ThreadLocal::InstanceImpl>();
    init_manager_ = std::make_unique<Init::ManagerImpl>("Server");
    server_ = std::make_unique<InstanceImpl>(
        *init_manager_, options_, time_system_, hooks_, restart_, stats_store_, fakelock_,
        std::make_unique<NiceMock<Random::MockRandomGenerator>>(), *thread_local_,
        Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(), nullptr);
    server_->initialize(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"),
                        component_factory_);

    EXPECT_TRUE(server_->api().fileSystem().fileExists(std::string(Platform::null_device_path)));
  }

  Thread::ThreadPtr startTestServer(const std::string& bootstrap_path,
                                    const bool use_intializing_instance) {
    absl::Notification started;
    absl::Notification post_init;

    auto server_thread = Thread::threadFactoryForTest().createThread([&] {
      initialize(bootstrap_path, use_intializing_instance);
      auto startup_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::Startup,
                                                      [&] { started.Notify(); });
      auto post_init_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::PostInit,
                                                        [&] { post_init.Notify(); });
      auto shutdown_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                                       [&](Event::PostCb) { FAIL(); });
      shutdown_handle = nullptr; // unregister callback
      server_->run();
      startup_handle = nullptr;
      post_init_handle = nullptr;
      server_ = nullptr;
      thread_local_ = nullptr;
    });

    started.WaitForNotification();
    post_init.WaitForNotification();
    return server_thread;
  }

  void expectCorrectBuildVersion(const envoy::config::core::v3::BuildVersion& build_version) {
    std::string version_string =
        absl::StrCat(build_version.version().major_number(), ".",
                     build_version.version().minor_number(), ".", build_version.version().patch());
    const auto& fields = build_version.metadata().fields();
    if (fields.find(BuildVersionMetadataKeys::get().BuildLabel) != fields.end()) {
      absl::StrAppend(&version_string, "-",
                      fields.at(BuildVersionMetadataKeys::get().BuildLabel).string_value());
    }
    EXPECT_EQ(BUILD_VERSION_NUMBER, version_string);
  }

  Network::Address::IpVersion version_;
  testing::NiceMock<MockOptions> options_;
  DefaultListenerHooks hooks_;
  testing::NiceMock<MockHotRestart> restart_;
  ThreadLocal::InstanceImplPtr thread_local_;
  Stats::TestIsolatedStoreImpl stats_store_;
  Thread::MutexBasicLockable fakelock_;
  TestComponentFactory component_factory_;
  Event::GlobalTimeSystem time_system_;
  ProcessObject* process_object_ = nullptr;
  std::unique_ptr<ProcessContextImpl> process_context_;
  std::unique_ptr<Init::Manager> init_manager_;

  std::unique_ptr<InstanceImpl> server_;
};

class ServerInstanceImplTest : public ServerInstanceImplTestBase,
                               public testing::TestWithParam<Network::Address::IpVersion> {
protected:
  ServerInstanceImplTest() { version_ = GetParam(); }
};

// Custom StatsSink that just increments a counter when flush is called.
class CustomStatsSink : public Stats::Sink {
public:
  CustomStatsSink(Stats::Scope& scope) : stats_flushed_(scope.counterFromString("stats.flushed")) {}

  // Stats::Sink
  void flush(Stats::MetricSnapshot&) override { stats_flushed_.inc(); }

  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

private:
  Stats::Counter& stats_flushed_;
};

// Custom StatsSinkFactory that creates CustomStatsSink.
class CustomStatsSinkFactory : public Server::Configuration::StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message&,
                                 Server::Configuration::ServerFactoryContext& server) override {
    return std::make_unique<CustomStatsSink>(server.scope());
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "envoy.custom_stats_sink"; }
};

// CustomListenerHooks is used for synchronization between test thread and server thread.
class CustomListenerHooks : public DefaultListenerHooks {
public:
  CustomListenerHooks(std::function<void()> workers_started_cb)
      : on_workers_started_cb_(workers_started_cb) {}

  void onWorkersStarted() override { on_workers_started_cb_(); }

private:
  std::function<void()> on_workers_started_cb_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ServerInstanceImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// The finalize() of Http::CustomInlineHeaderRegistry will be called When InstanceImpl is
// instantiated. For this reason, the test case for inline headers must precede all other test
// cases.

// Test the case where inline headers contain names that cannot be registered as inline headers.
TEST_P(ServerInstanceImplTest, WithErrorCustomInlineHeaders) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize("test/server/test_data/server/bootstrap_inline_headers_error.yaml"),
      EnvoyException, "Header set-cookie cannot be registered as an inline header.");
}

TEST_P(ServerInstanceImplTest, WithDeathCustomInlineHeaders) {
#if !defined(NDEBUG)
  // The finalize() of Http::CustomInlineHeaderRegistry will be called after the first successful
  // instantiation of InstanceImpl. If InstanceImpl is instantiated again and the inline header is
  // registered again, the assertion will be triggered.
  EXPECT_DEATH(
      {
        initialize("test/server/test_data/server/bootstrap_inline_headers.yaml");
        initialize("test/server/test_data/server/bootstrap_inline_headers.yaml");
      },
      "");
#endif // !defined(NDEBUG)
}

// Test whether the custom inline headers can be registered correctly.
TEST_P(ServerInstanceImplTest, WithCustomInlineHeaders) {
  static bool is_registered = false;

  if (!is_registered) {
    // Avoid repeated registration of custom inline headers in the current process after the
    // finalize() of Http::CustomInlineHeaderRegistry has already been called.
    EXPECT_NO_THROW(initialize("test/server/test_data/server/bootstrap_inline_headers.yaml"));
    is_registered = true;
  }

  EXPECT_TRUE(
      Http::CustomInlineHeaderRegistry::getInlineHeader<Http::RequestHeaderMap::header_map_type>(
          Http::LowerCaseString("test1"))
          .has_value());
  EXPECT_TRUE(
      Http::CustomInlineHeaderRegistry::getInlineHeader<Http::RequestTrailerMap::header_map_type>(
          Http::LowerCaseString("test2"))
          .has_value());
  EXPECT_TRUE(
      Http::CustomInlineHeaderRegistry::getInlineHeader<Http::ResponseHeaderMap::header_map_type>(
          Http::LowerCaseString("test3"))
          .has_value());
  EXPECT_TRUE(
      Http::CustomInlineHeaderRegistry::getInlineHeader<Http::ResponseTrailerMap::header_map_type>(
          Http::LowerCaseString("test4"))
          .has_value());

  {
    Http::TestRequestHeaderMapImpl headers{
        {"test1", "test1_value1"},
        {"test1", "test1_value2"},
        {"test3", "test3_value1"},
        {"test3", "test3_value2"},
    };

    // 'test1' is registered as the inline request header.
    auto test1_headers = headers.get(Http::LowerCaseString("test1"));
    EXPECT_EQ(1, test1_headers.size());
    EXPECT_EQ("test1_value1,test1_value2", headers.get_("test1"));

    // 'test3' is not registered as an inline request header.
    auto test3_headers = headers.get(Http::LowerCaseString("test3"));
    EXPECT_EQ(2, test3_headers.size());
    EXPECT_EQ("test3_value1", headers.get_("test3"));
  }

  {
    Http::TestResponseHeaderMapImpl headers{
        {"test1", "test1_value1"},
        {"test1", "test1_value2"},
        {"test3", "test3_value1"},
        {"test3", "test3_value2"},
    };

    // 'test1' is not registered as the inline response header.
    auto test1_headers = headers.get(Http::LowerCaseString("test1"));
    EXPECT_EQ(2, test1_headers.size());
    EXPECT_EQ("test1_value1", headers.get_("test1"));

    // 'test3' is registered as an inline response header.
    auto test3_headers = headers.get(Http::LowerCaseString("test3"));
    EXPECT_EQ(1, test3_headers.size());
    EXPECT_EQ("test3_value1,test3_value2", headers.get_("test3"));
  }
}

// Validates that server stats are flushed even when server is stuck with initialization.
TEST_P(ServerInstanceImplTest, StatsFlushWhenServerIsStillInitializing) {
  CustomStatsSinkFactory factory;
  Registry::InjectFactory<Server::Configuration::StatsSinkFactory> registered(factory);

  auto server_thread =
      startTestServer("test/server/test_data/server/stats_sink_bootstrap.yaml", true);

  // Wait till stats are flushed to custom sink and validate that the actual flush happens.
  EXPECT_TRUE(TestUtility::waitForCounterEq(stats_store_, "stats.flushed", 1, time_system_));
  EXPECT_EQ(3L, TestUtility::findGauge(stats_store_, "server.state")->value());
  EXPECT_EQ(Init::Manager::State::Initializing, server_->initManager().state());

  EXPECT_TRUE(server_->enableReusePortDefault());
  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

// Validates that the "server.version" is updated with stats_server_version_override from bootstrap.
TEST_P(ServerInstanceImplTest, ProxyVersionOveridesFromBootstrap) {
  auto server_thread =
      startTestServer("test/server/test_data/server/proxy_version_bootstrap.yaml", true);

  EXPECT_EQ(100012001, TestUtility::findGauge(stats_store_, "server.version")->value());

  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

// Validates that the "server.fips_mode" stat indicates the FIPS compliance from the Envoy Build
TEST_P(ServerInstanceImplTest, ValidateFIPSModeStat) {
  auto server_thread =
      startTestServer("test/server/test_data/server/proxy_version_bootstrap.yaml", true);

  if (VersionInfo::sslFipsCompliant()) {
    EXPECT_EQ(
        1L, TestUtility::findGauge(stats_store_, "server.compilation_settings.fips_mode")->value());
  } else {
    EXPECT_EQ(
        0L, TestUtility::findGauge(stats_store_, "server.compilation_settings.fips_mode")->value());
  }

  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

// Validate the the Envoy FIPS compilation flags are consistent with the FIPS
// mode of the underlying BoringSSL build.
TEST_P(ServerInstanceImplTest, ValidateFIPSModeConsistency) {
  bool isFIPS = (FIPS_mode() != 0);
  EXPECT_EQ(isFIPS, VersionInfo::sslFipsCompliant());
}

TEST_P(ServerInstanceImplTest, EmptyShutdownLifecycleNotifications) {
  auto server_thread = startTestServer("test/server/test_data/server/node_bootstrap.yaml", false);
  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
  // Validate that initialization_time histogram value has been set.
  EXPECT_TRUE(stats_store_
                  .histogramFromString("server.initialization_time_ms",
                                       Stats::Histogram::Unit::Milliseconds)
                  .used());
  EXPECT_EQ(0L, TestUtility::findGauge(stats_store_, "server.state")->value());
}

// Catch exceptions in secondary cluster initialization callbacks. These are not caught in the main
// initialization try/catch.
TEST_P(ServerInstanceImplTest, SecondaryClusterExceptions) {
  // Error in reading illegal file path for channel credentials in secondary cluster.
  EXPECT_LOG_CONTAINS("warn",
                      "Skipping initialization of secondary cluster: API configs must have either "
                      "a gRPC service or a cluster name defined",
                      {
                        initialize("test/server/test_data/server/health_check_nullptr.yaml");
                        server_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
                      });
}

// Catch exceptions in HDS cluster initialization callbacks.
TEST_P(ServerInstanceImplTest, HdsClusterException) {
  // Error in reading illegal file path for channel credentials in secondary cluster.
  EXPECT_LOG_CONTAINS("warn",
                      "Skipping initialization of HDS cluster: API configs must have either a gRPC "
                      "service or a cluster name defined",
                      {
                        initialize("test/server/test_data/server/hds_exception.yaml");
                        server_->dispatcher().run(Event::Dispatcher::RunType::NonBlock);
                      });
}

TEST_P(ServerInstanceImplTest, LifecycleNotifications) {
  bool startup = false, post_init = false, shutdown = false, shutdown_with_completion = false;
  absl::Notification started, post_init_fired, shutdown_begin, completion_block, completion_done;

  // Run the server in a separate thread so we can test different lifecycle stages.
  auto server_thread = Thread::threadFactoryForTest().createThread([&] {
    initialize("test/server/test_data/server/node_bootstrap.yaml");
    auto handle1 = server_->registerCallback(ServerLifecycleNotifier::Stage::Startup, [&] {
      startup = true;
      started.Notify();
    });
    auto handle2 = server_->registerCallback(ServerLifecycleNotifier::Stage::PostInit, [&] {
      post_init = true;
      post_init_fired.Notify();
    });
    ServerLifecycleNotifier::HandlePtr handle3 =
        server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit, [&] {
          shutdown = true;
          shutdown_begin.Notify();
          handle3 = nullptr;
        });
    auto handle4 = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                             [&](Event::PostCb completion_cb) {
                                               // Block till we're told to complete
                                               completion_block.WaitForNotification();
                                               shutdown_with_completion = true;
                                               server_->dispatcher().post(std::move(completion_cb));
                                               completion_done.Notify();
                                             });
    auto handle5 =
        server_->registerCallback(ServerLifecycleNotifier::Stage::Startup, [&] { FAIL(); });
    handle5 = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                        [&](Event::PostCb) { FAIL(); });
    handle5 = nullptr;

    server_->run();
    handle1 = nullptr;
    handle2 = nullptr;
    // handle3 is nulled out in the callback itself, to test that works as well
    handle4 = nullptr;
    server_ = nullptr;
    thread_local_ = nullptr;
  });

  started.WaitForNotification();
  EXPECT_TRUE(startup);
  EXPECT_FALSE(shutdown);
  EXPECT_TRUE(TestUtility::findGauge(stats_store_, "server.state")->used());
  EXPECT_EQ(0L, TestUtility::findGauge(stats_store_, "server.state")->value());

  post_init_fired.WaitForNotification();
  EXPECT_TRUE(post_init);
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

class ServerInstanceImplWorkersTest : public ServerInstanceImplTest {
protected:
  ServerInstanceImplWorkersTest() {
    bool workers_started = false;

    // Expect drainParentListeners not to be called before workers start.
    EXPECT_CALL(restart_, drainParentListeners).Times(0);

    // Run the server in a separate thread so we can test different lifecycle stages.
    server_thread_ = Thread::threadFactoryForTest().createThread([&] {
      auto hooks = CustomListenerHooks([&]() {
        workers_started = true;
        workers_started_fired_.Notify();
        workers_started_block_.WaitForNotification();
      });
      initialize("test/server/test_data/server/node_bootstrap.yaml", false, hooks);
      server_->run();
      server_ = nullptr;
      thread_local_ = nullptr;
    });

    workers_started_fired_.WaitForNotification();
    EXPECT_TRUE(workers_started);
    EXPECT_CALL(restart_, drainParentListeners);
  }

  ~ServerInstanceImplWorkersTest() override {
    server_->dispatcher().post([&] { server_->shutdown(); });
    server_thread_->join();
  }

  absl::Notification workers_started_fired_;
  absl::Notification workers_started_block_;
  Thread::ThreadPtr server_thread_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ServerInstanceImplWorkersTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ServerInstanceImplWorkersTest, DrainParentListenerAfterWorkersStarted) {
  EXPECT_TRUE(TestUtility::findGauge(stats_store_, "server.state")->used());
  EXPECT_EQ(0L, TestUtility::findGauge(stats_store_, "server.state")->value());
  workers_started_block_.Notify();
}

TEST_P(ServerInstanceImplWorkersTest, DrainCloseAfterWorkersStarted) {
  absl::Notification drain_closes_started, drain_complete;
  workers_started_block_.Notify();

  DrainManager& drain_manager = server_->drainManager();

  // To reproduce the race condition from
  // https://github.com/envoyproxy/envoy/issues/31457 we make sure that the
  // infinite drainClose spin-loop (mimicing high traffic) is running before we
  // initiate the drain sequence.
  auto drain_thread = Thread::threadFactoryForTest().createThread([&] {
    bool closed = drain_manager.drainClose();
    drain_closes_started.Notify();
    while (!closed) {
      closed = drain_manager.drainClose();
    }
  });
  drain_closes_started.WaitForNotification();

  // Now that we are starting to try to call drainClose, we'll start the drain sequence, then
  // wait for that to complete.
  server_->dispatcher().post(
      [&] { drain_manager.startDrainSequence([&drain_complete]() { drain_complete.Notify(); }); });

  drain_complete.WaitForNotification();
  drain_thread->join();
}

// A test target which never signals that it is ready.
class NeverReadyTarget : public Init::TargetImpl {
public:
  NeverReadyTarget(absl::Notification& initialized)
      : Init::TargetImpl("test", [this] { initialize(); }), initialized_(initialized) {}

private:
  void initialize() { initialized_.Notify(); }

  absl::Notification& initialized_;
};

TEST_P(ServerInstanceImplTest, NoLifecycleNotificationOnEarlyShutdown) {
  absl::Notification initialized;

  auto server_thread = Thread::threadFactoryForTest().createThread([&] {
    initialize("test/server/test_data/server/node_bootstrap.yaml");

    // This shutdown notification should never be called because we will shutdown
    // early before the init manager finishes initializing and therefore before
    // the server starts worker threads.
    auto shutdown_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                                     [&](Event::PostCb) { FAIL(); });
    NeverReadyTarget target(initialized);
    server_->initManager().add(target);
    server_->run();

    shutdown_handle = nullptr;
    server_ = nullptr;
    thread_local_ = nullptr;
  });

  // Wait until the init manager starts initializing targets...
  initialized.WaitForNotification();

  // Now shutdown the main dispatcher and trigger server lifecycle notifications.
  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

TEST_P(ServerInstanceImplTest, ShutdownBeforeWorkersStarted) {
  // Test that drainParentListeners() should never be called because we will shutdown
  // early before the server starts worker threads.
  EXPECT_CALL(restart_, drainParentListeners).Times(0);

  auto server_thread = Thread::threadFactoryForTest().createThread([&] {
    initialize("test/server/test_data/server/node_bootstrap.yaml");

    auto post_init_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::PostInit,
                                                      [&] { server_->shutdown(); });

    // This shutdown notification should never be called because we will shutdown early.
    auto shutdown_handle = server_->registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                                                     [&](Event::PostCb) { FAIL(); });
    server_->run();

    post_init_handle = nullptr;
    shutdown_handle = nullptr;
    server_ = nullptr;
    thread_local_ = nullptr;
  });

  server_thread->join();
}

TEST_P(ServerInstanceImplTest, Stats) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.concurrency_ = 2;
  options_.hot_restart_epoch_ = 3;
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
  EXPECT_NE(nullptr, TestUtility::findCounter(stats_store_, "main_thread.watchdog_miss"));
  EXPECT_NE(nullptr, TestUtility::findCounter(stats_store_, "workers.watchdog_miss"));
  EXPECT_EQ(2L, TestUtility::findGauge(stats_store_, "server.concurrency")->value());
  EXPECT_EQ(3L, TestUtility::findGauge(stats_store_, "server.hot_restart_epoch")->value());

// The ENVOY_BUG stat works in release mode.
#if defined(NDEBUG)
  // Test exponential back-off on a fixed line ENVOY_BUG.
  for (int i = 0; i < 16; i++) {
    ENVOY_BUG(false, "");
  }
  EXPECT_EQ(5L, TestUtility::findCounter(stats_store_, "server.envoy_bug_failures")->value());
  // Another ENVOY_BUG increments the counter.
  ENVOY_BUG(false, "Testing envoy bug assertion failure detection in release build.");
  EXPECT_EQ(6L, TestUtility::findCounter(stats_store_, "server.envoy_bug_failures")->value());
#else
  // The ENVOY_BUG macro aborts in debug mode.
  EXPECT_DEATH(ENVOY_BUG(false, ""), "");
#endif

// The ASSERT stat only works in this configuration.
#if defined(NDEBUG) && defined(ENVOY_LOG_DEBUG_ASSERT_IN_RELEASE)
  ASSERT(false, "Testing debug assertion failure detection in release build.");
  EXPECT_EQ(1L, TestUtility::findCounter(stats_store_, "server.debug_assertion_failures")->value());
#else
  EXPECT_EQ(0L, TestUtility::findCounter(stats_store_, "server.debug_assertion_failures")->value());
#endif
}

class ServerStatsTest
    : public Event::TestUsingSimulatedTime,
      public ServerInstanceImplTestBase,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
protected:
  ServerStatsTest() {
    version_ = std::get<0>(GetParam());
    manual_flush_ = std::get<1>(GetParam());
  }

  void flushStats() {
    if (manual_flush_) {
      server_->flushStats();
      server_->dispatcher().run(Event::Dispatcher::RunType::Block);
    } else {
      // Default flush interval is 5 seconds.
      simTime().advanceTimeAndRun(std::chrono::seconds(6), server_->dispatcher(),
                                  Event::Dispatcher::RunType::Block);
    }
  }

  bool manual_flush_;
};

std::string ipFlushingModeTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return fmt::format(
      "{}_{}",
      TestUtility::ipTestParamsToString(
          ::testing::TestParamInfo<Network::Address::IpVersion>(std::get<0>(params.param), 0)),
      std::get<1>(params.param) ? "with_manual_flush" : "with_time_based_flush");
}

INSTANTIATE_TEST_SUITE_P(
    IpVersionsFlushingMode, ServerStatsTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    ipFlushingModeTestParamsToString);

TEST_P(ServerStatsTest, FlushStats) {
  initialize("test/server/test_data/server/empty_bootstrap.yaml");
  Stats::Gauge& recent_lookups = stats_store_.gaugeFromString(
      "server.stats_recent_lookups", Stats::Gauge::ImportMode::NeverImport);
  EXPECT_EQ(0, recent_lookups.value());
  flushStats();
  uint64_t strobed_recent_lookups = recent_lookups.value();
  EXPECT_LT(100, strobed_recent_lookups); // Recently this was 319 but exact value not important.
  Stats::StatNameSetPtr test_set = stats_store_.symbolTable().makeSet("test");

  // When we remember a StatNameSet builtin, we charge only for the SymbolTable
  // lookup, which requires a lock.
  test_set->rememberBuiltin("a.b");
  flushStats();
  EXPECT_EQ(1, recent_lookups.value() - strobed_recent_lookups);
  strobed_recent_lookups = recent_lookups.value();

  // When we create a dynamic stat, there are no locks taken.
  Stats::StatNameDynamicStorage dynamic_stat("c.d", stats_store_.symbolTable());
  flushStats();
  EXPECT_EQ(recent_lookups.value(), strobed_recent_lookups);
}

TEST_P(ServerInstanceImplTest, FlushStatsOnAdmin) {
  CustomStatsSinkFactory factory;
  Registry::InjectFactory<Server::Configuration::StatsSinkFactory> registered(factory);
  auto server_thread =
      startTestServer("test/server/test_data/server/stats_sink_manual_flush_bootstrap.yaml", true);
  EXPECT_TRUE(server_->statsConfig().flushOnAdmin());
  EXPECT_EQ(std::chrono::seconds(5), server_->statsConfig().flushInterval());

  auto counter = TestUtility::findCounter(stats_store_, "stats.flushed");

  time_system_.advanceTimeWait(std::chrono::seconds(6));
  EXPECT_EQ(0L, counter->value());

  // Flush via admin.
  Http::TestResponseHeaderMapImpl response_headers;
  std::string body;
  EXPECT_EQ(Http::Code::OK, server_->admin()->request("/stats", "GET", response_headers, body));
  EXPECT_EQ(1L, counter->value());

  time_system_.advanceTimeWait(std::chrono::seconds(6));
  EXPECT_EQ(1L, counter->value());

  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

TEST_P(ServerInstanceImplTest, ConcurrentFlushes) {
  CustomStatsSinkFactory factory;
  Registry::InjectFactory<Server::Configuration::StatsSinkFactory> registered(factory);

  bool workers_started = false;
  absl::Notification workers_started_fired;
  // Run the server in a separate thread so we can test different lifecycle stages.
  auto server_thread = Thread::threadFactoryForTest().createThread([&] {
    auto hooks = CustomListenerHooks([&]() {
      workers_started = true;
      workers_started_fired.Notify();
    });
    initialize("test/server/test_data/server/stats_sink_manual_flush_bootstrap.yaml", false, hooks);
    server_->run();
    server_ = nullptr;
    thread_local_ = nullptr;
  });

  workers_started_fired.WaitForNotification();
  EXPECT_TRUE(workers_started);

  // Flush three times in a row. Two of these should get dropped.
  server_->dispatcher().post([&] {
    server_->flushStats();
    server_->flushStats();
    server_->flushStats();
  });

  EXPECT_TRUE(
      TestUtility::waitForCounterEq(stats_store_, "server.dropped_stat_flushes", 2, time_system_));

  server_->dispatcher().post([&] { stats_store_.runMergeCallback(); });

  EXPECT_TRUE(TestUtility::waitForCounterEq(stats_store_, "stats.flushed", 1, time_system_));

  // Trigger another flush after the first one finished. This should go through an no drops should
  // be recorded.
  server_->dispatcher().post([&] { server_->flushStats(); });

  server_->dispatcher().post([&] { stats_store_.runMergeCallback(); });

  EXPECT_TRUE(TestUtility::waitForCounterEq(stats_store_, "stats.flushed", 2, time_system_));

  EXPECT_TRUE(
      TestUtility::waitForCounterEq(stats_store_, "server.dropped_stat_flushes", 2, time_system_));

  server_->dispatcher().post([&] { server_->shutdown(); });
  server_thread->join();
}

// Default validation mode
TEST_P(ServerInstanceImplTest, ValidationDefault) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
  EXPECT_EQ(
      server_->messageValidationContext().staticValidationVisitor().onUnknownField("foo").message(),
      "Protobuf message (foo) has unknown fields");
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_, "server.static_unknown_fields")->value());
  EXPECT_TRUE(
      server_->messageValidationContext().dynamicValidationVisitor().onUnknownField("bar").ok());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "server.dynamic_unknown_fields")->value());
}

// Validation mode with --allow-unknown-static-fields
TEST_P(ServerInstanceImplTest, ValidationAllowStatic) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.allow_unknown_static_fields_ = true;
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
  EXPECT_TRUE(
      server_->messageValidationContext().staticValidationVisitor().onUnknownField("foo").ok());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "server.static_unknown_fields")->value());
  EXPECT_TRUE(
      server_->messageValidationContext().dynamicValidationVisitor().onUnknownField("bar").ok());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "server.dynamic_unknown_fields")->value());
}

// Validation mode with --reject-unknown-dynamic-fields
TEST_P(ServerInstanceImplTest, ValidationRejectDynamic) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.reject_unknown_dynamic_fields_ = true;
  options_.ignore_unknown_dynamic_fields_ = true; // reject takes precedence over ignore
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
  EXPECT_EQ(
      server_->messageValidationContext().staticValidationVisitor().onUnknownField("foo").message(),
      "Protobuf message (foo) has unknown fields");
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_, "server.static_unknown_fields")->value());
  EXPECT_EQ(server_->messageValidationContext()
                .dynamicValidationVisitor()
                .onUnknownField("bar")
                .message(),
            "Protobuf message (bar) has unknown fields");
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_, "server.dynamic_unknown_fields")->value());
}

// Validation mode with --allow-unknown-static-fields --reject-unknown-dynamic-fields
TEST_P(ServerInstanceImplTest, ValidationAllowStaticRejectDynamic) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.allow_unknown_static_fields_ = true;
  options_.reject_unknown_dynamic_fields_ = true;
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
  EXPECT_TRUE(
      server_->messageValidationContext().staticValidationVisitor().onUnknownField("foo").ok());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_, "server.static_unknown_fields")->value());
  EXPECT_EQ(server_->messageValidationContext()
                .dynamicValidationVisitor()
                .onUnknownField("bar")
                .message(),
            "Protobuf message (bar) has unknown fields");
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_, "server.dynamic_unknown_fields")->value());
}

// Validate server localInfo() from bootstrap Node.
TEST_P(ServerInstanceImplTest, BootstrapNode) {
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  EXPECT_EQ("bootstrap_zone", server_->localInfo().zoneName());
  EXPECT_EQ("bootstrap_cluster", server_->localInfo().clusterName());
  EXPECT_EQ("bootstrap_id", server_->localInfo().nodeName());
  EXPECT_EQ("bootstrap_sub_zone", server_->localInfo().node().locality().sub_zone());
  EXPECT_EQ("envoy", server_->localInfo().node().user_agent_name());
  EXPECT_TRUE(server_->localInfo().node().has_user_agent_build_version());
  expectCorrectBuildVersion(server_->localInfo().node().user_agent_build_version());
}

// Validate server localInfo().zoneStatName() is set from bootstrap config
TEST_P(ServerInstanceImplTest, ZoneStatNameFromBootstrap) {
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  EXPECT_EQ("bootstrap_zone",
            stats_store_.symbolTable().toString(server_->localInfo().zoneStatName()));
}

// Validate server localInfo().zoneStatName() can by overridden by option
TEST_P(ServerInstanceImplTest, ZoneStatNameFromOption) {
  options_.service_zone_name_ = "bootstrap_zone_override";
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  EXPECT_EQ("bootstrap_zone_override",
            stats_store_.symbolTable().toString(server_->localInfo().zoneStatName()));
}

// Validate user agent information from bootstrap Node.
TEST_P(ServerInstanceImplTest, UserAgentOverrideFromNode) {
  initialize("test/server/test_data/server/node_bootstrap_agent_override.yaml");
  EXPECT_EQ("test-ci-user-agent", server_->localInfo().node().user_agent_name());
  EXPECT_TRUE(server_->localInfo().node().has_user_agent_build_version());
  EXPECT_EQ(9, server_->localInfo().node().user_agent_build_version().version().major_number());
  EXPECT_EQ(8, server_->localInfo().node().user_agent_build_version().version().minor_number());
  EXPECT_EQ(7, server_->localInfo().node().user_agent_build_version().version().patch());
}

// Validate that we correctly parse a V3 pb_text file without explicit version configuration.
TEST_P(ServerInstanceImplTest, LoadsV3ConfigFromPbText) {
  EXPECT_LOG_NOT_CONTAINS(
      "trace", "Configuration does not parse cleanly as v3",
      initialize("test/server/test_data/server/valid_v3_but_invalid_v2_bootstrap.pb_text"));
}

// Validate that we correctly parse a V3 YAML file without explicit version configuration.
TEST_P(ServerInstanceImplTest, LoadsV3ConfigFromYaml) {
  EXPECT_LOG_NOT_CONTAINS(
      "trace", "Configuration does not parse cleanly as v3",
      initialize("test/server/test_data/server/valid_v3_but_invalid_v2_bootstrap.yaml"));
}

// Validate that we correctly parse a V3 pb_text file when configured to do so.
TEST_P(ServerInstanceImplTest, LoadsV3ConfigWhenV3SelectedFromPbText) {
  EXPECT_LOG_NOT_CONTAINS(
      "trace", "Configuration does not parse cleanly as v3",
      initialize("test/server/test_data/server/valid_v3_but_invalid_v2_bootstrap.pb_text"));
}

// Validate that we correctly parse a V3 YAML file when configured to do so.
TEST_P(ServerInstanceImplTest, LoadsV3ConfigWhenV3SelectedFromYaml) {
  EXPECT_LOG_NOT_CONTAINS(
      "trace", "Configuration does not parse cleanly as v3",
      initialize("test/server/test_data/server/valid_v3_but_invalid_v2_bootstrap.yaml"));
}

// Validate that bootstrap pb_text loads.
TEST_P(ServerInstanceImplTest, LoadsBootstrapFromPbText) {
  EXPECT_LOG_NOT_CONTAINS("trace", "Configuration does not parse cleanly as v3",
                          initialize("test/server/test_data/server/node_bootstrap.pb_text"));
  EXPECT_EQ("bootstrap_id", server_->localInfo().node().id());
}

TEST_P(ServerInstanceImplTest, LoadsBootstrapFromConfigProtoOptions) {
  options_.config_proto_.mutable_node()->set_id("foo");
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  EXPECT_EQ("foo", server_->localInfo().node().id());
}

TEST_P(ServerInstanceImplTest, LoadsBootstrapFromConfigYamlAfterConfigPath) {
  options_.config_yaml_ = "node:\n  id: 'bar'";
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  EXPECT_EQ("bar", server_->localInfo().node().id());
}

TEST_P(ServerInstanceImplTest, LoadsBootstrapFromConfigProtoOptionsLast) {
  options_.config_yaml_ = "node:\n  id: 'bar'";
  options_.config_proto_.mutable_node()->set_id("foo");
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  EXPECT_EQ("foo", server_->localInfo().node().id());
}

// Validate server localInfo() from bootstrap Node with CLI overrides.
TEST_P(ServerInstanceImplTest, BootstrapNodeWithOptionsOverride) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.service_zone_name_ = "some_zone_name";
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  EXPECT_EQ("some_zone_name", server_->localInfo().zoneName());
  EXPECT_EQ("some_cluster_name", server_->localInfo().clusterName());
  EXPECT_EQ("some_node_name", server_->localInfo().nodeName());
  EXPECT_EQ("bootstrap_sub_zone", server_->localInfo().node().locality().sub_zone());
}

// Validate server runtime is parsed from bootstrap and that we can read from
// service cluster specified disk-based overrides.
TEST_P(ServerInstanceImplTest, BootstrapRuntime) {
  options_.service_cluster_name_ = "some_service";
  initialize("test/server/test_data/server/runtime_bootstrap.yaml");
  EXPECT_EQ("bar", server_->runtime().snapshot().get("foo").value().get());
  // This should access via the override/some_service overlay.
  EXPECT_EQ("fozz", server_->runtime().snapshot().get("fizz").value().get());
}

// Validate that a runtime absent an admin layer will fail mutating operations
// but still support inspection of runtime values.
TEST_P(ServerInstanceImplTest, RuntimeNoAdminLayer) {
  options_.service_cluster_name_ = "some_service";
  initialize("test/server/test_data/server/runtime_bootstrap.yaml");
  Http::TestResponseHeaderMapImpl response_headers;
  std::string response_body;
  EXPECT_EQ(Http::Code::OK,
            server_->admin()->request("/runtime", "GET", response_headers, response_body));
  EXPECT_THAT(response_body, HasSubstr("fozz"));
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            server_->admin()->request("/runtime_modify?foo=bar", "POST", response_headers,
                                      response_body));
  EXPECT_EQ("No admin layer specified", response_body);
}

// Verify that bootstrap fails if RTDS is configured through an EDS cluster
TEST_P(ServerInstanceImplTest, BootstrapRtdsThroughEdsFails) {
  options_.service_cluster_name_ = "some_service";
  options_.service_node_name_ = "some_node_name";
  EXPECT_THROW_WITH_REGEX(initialize("test/server/test_data/server/runtime_bootstrap_eds.yaml"),
                          EnvoyException, "must have a statically defined non-EDS cluster");
}

// Verify that bootstrap fails if RTDS is configured through an ADS using EDS cluster
TEST_P(ServerInstanceImplTest, BootstrapRtdsThroughAdsViaEdsFails) {
  options_.service_cluster_name_ = "some_service";
  options_.service_node_name_ = "some_node_name";
  EXPECT_THROW_WITH_REGEX(initialize("test/server/test_data/server/runtime_bootstrap_ads_eds.yaml"),
                          EnvoyException, "Unknown gRPC client cluster");
}

// Validate invalid runtime in bootstrap is rejected.
TEST_P(ServerInstanceImplTest, InvalidBootstrapRuntime) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize("test/server/test_data/server/invalid_runtime_bootstrap.yaml"), EnvoyException,
      "Invalid runtime entry value for foo");
}

// Validate invalid layered runtime missing a name is rejected.
TEST_P(ServerInstanceImplTest, InvalidLayeredBootstrapMissingName) {
  EXPECT_THROW_WITH_REGEX(
      initialize("test/server/test_data/server/invalid_layered_runtime_missing_name.yaml"),
      EnvoyException, "RuntimeLayerValidationError.Name: value length must be at least");
}

// Validate invalid layered runtime with duplicate names is rejected.
TEST_P(ServerInstanceImplTest, InvalidLayeredBootstrapDuplicateName) {
  EXPECT_THROW_WITH_REGEX(
      initialize("test/server/test_data/server/invalid_layered_runtime_duplicate_name.yaml"),
      EnvoyException, "Duplicate layer name: some_static_laye");
}

// Validate invalid layered runtime with no layer specifier is rejected.
TEST_P(ServerInstanceImplTest, InvalidLayeredBootstrapNoLayerSpecifier) {
  EXPECT_THROW_WITH_REGEX(
      initialize("test/server/test_data/server/invalid_layered_runtime_no_layer_specifier.yaml"),
      EnvoyException, "BootstrapValidationError.LayeredRuntime");
}

// Regression test for segfault when server initialization fails prior to
// ClusterManager initialization.
TEST_P(ServerInstanceImplTest, BootstrapClusterManagerInitializationFail) {
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/test_data/server/cluster_dupe_bootstrap.yaml"),
                            EnvoyException, "cluster manager: duplicate cluster 'service_google'");
}

// Regression tests for SdsApi throwing exceptions in initialize().
TEST_P(ServerInstanceImplTest, BadSdsConfigSource) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize("test/server/test_data/server/bad_sds_config_source.yaml"), EnvoyException,
      "envoy.config.core.v3.ApiConfigSource must have a statically defined non-EDS cluster: "
      "'sds-grpc' does not exist, was added via api, or is an EDS cluster");
}

// Test for protoc-gen-validate constraint on invalid timeout entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidTimeout) {
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams(
          "test/server/test_data/server/cluster_health_check_bootstrap.yaml", 0, 0.25),
      EnvoyException, "HealthCheckValidationError.Timeout: value must be greater than 0s");
}

// Test for protoc-gen-validate constraint on invalid interval entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidInterval) {
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams(
          "test/server/test_data/server/cluster_health_check_bootstrap.yaml", 0.5, 0),
      EnvoyException, "HealthCheckValidationError.Interval: value must be greater than 0s");
}

// Test for protoc-gen-validate constraint on invalid timeout and interval entry of a health check
// config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckInvalidTimeoutAndInterval) {
  EXPECT_THROW_WITH_REGEX(
      initializeWithHealthCheckParams(
          "test/server/test_data/server/cluster_health_check_bootstrap.yaml", 0, 0),
      EnvoyException, "HealthCheckValidationError.Timeout: value must be greater than 0s");
}

// Test for protoc-gen-validate constraint on valid interval entry of a health check config entry.
TEST_P(ServerInstanceImplTest, BootstrapClusterHealthCheckValidTimeoutAndInterval) {
  EXPECT_NO_THROW(initializeWithHealthCheckParams(
      "test/server/test_data/server/cluster_health_check_bootstrap.yaml", 0.25, 0.5));
}

// Test that a Bootstrap proto with no address specified in its Admin field can go through
// initialization properly, but without starting an admin listener.
TEST_P(ServerInstanceImplTest, BootstrapNodeNoAdmin) {
  EXPECT_NO_THROW(initialize("test/server/test_data/server/node_bootstrap_no_admin_port.yaml"));
  // Admin::addListenerToHandler() calls one of handler's methods after checking that the Admin
  // has a listener. So, the fact that passing a nullptr doesn't cause a segfault establishes
  // that there is no listener.
  server_->admin()->addListenerToHandler(/*handler=*/nullptr);
}

namespace {
void bindAndListenTcpSocket(const Network::Address::InstanceConstSharedPtr& address,
                            const Network::Socket::OptionsSharedPtr& options) {
  auto socket = std::make_unique<Network::TcpListenSocket>(address, options, true);
  // Some kernels erroneously allow `bind` without SO_REUSEPORT for addresses
  // with some other socket already listening on it, see #7636.
  if (SOCKET_FAILURE(socket->ioHandle().listen(1).return_value_)) {
    // Mimic bind exception for the test simplicity.
    throw Network::SocketBindException(fmt::format("cannot listen: {}", errorDetails(errno)),
                                       errno);
  }
}
} // namespace

// Test that `socket_options` field in an Admin proto is honored.
TEST_P(ServerInstanceImplTest, BootstrapNodeWithSocketOptions) {
  // Start Envoy instance with admin port with SO_REUSEPORT option.
  ASSERT_NO_THROW(
      initialize("test/server/test_data/server/node_bootstrap_with_admin_socket_options.yaml"));
  const auto address = server_->admin()->socket().connectionInfoProvider().localAddress();

  // First attempt to bind and listen socket should fail due to the lack of SO_REUSEPORT socket
  // options.
  EXPECT_THAT_THROWS_MESSAGE(bindAndListenTcpSocket(address, nullptr), EnvoyException,
                             HasSubstr(errorDetails(SOCKET_ERROR_ADDR_IN_USE)));

  // Second attempt should succeed as kernel allows multiple sockets to listen the same address iff
  // both of them use SO_REUSEPORT socket option.
  auto options = std::make_shared<Network::Socket::Options>();
  options->emplace_back(std::make_shared<Network::SocketOptionImpl>(
      envoy::config::core::v3::SocketOption::STATE_PREBIND,
      ENVOY_MAKE_SOCKET_OPTION_NAME(SOL_SOCKET, SO_REUSEPORT), 1));
  EXPECT_NO_THROW(bindAndListenTcpSocket(address, options));
}

// Empty bootstrap succeeds.
TEST_P(ServerInstanceImplTest, EmptyBootstrap) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
}

// Custom header bootstrap succeeds.
TEST_P(ServerInstanceImplTest, CustomHeaderBootstrap) {
  options_.config_path_ = TestEnvironment::writeStringToFileForTest(
      "custom.yaml", "header_prefix: \"x-envoy\"\nstatic_resources:\n");
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize(options_.config_path_));
}

// Negative test for protoc-gen-validate constraints.
TEST_P(ServerInstanceImplTest, ValidateFail) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  try {
    initialize("test/server/test_data/server/invalid_bootstrap.yaml");
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
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
  EXPECT_TRUE(server_->api().fileSystem().fileExists(path));

  GET_MISC_LOGGER().set_level(spdlog::level::info);
  ENVOY_LOG_MISC(warn, "LogToFile test string");
  Logger::Registry::getSink()->flush();
  std::string log = server_->api().fileSystem().fileReadToEnd(path).value();
  EXPECT_GT(log.size(), 0);
  EXPECT_TRUE(log.find("LogToFile test string") != std::string::npos);

  // Test that critical messages get immediately flushed
  ENVOY_LOG_MISC(critical, "LogToFile second test string");
  log = server_->api().fileSystem().fileReadToEnd(path).value();
  EXPECT_TRUE(log.find("LogToFile second test string") != std::string::npos);
}

TEST_P(ServerInstanceImplTest, LogToFileError) {
  options_.log_path_ = "/this/path/does/not/exist";
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  try {
    initialize("test/server/test_data/server/empty_bootstrap.yaml");
    FAIL();
  } catch (const EnvoyException& e) {
    EXPECT_THAT(e.what(), HasSubstr("Failed to open log-file"));
  }
}

// When there are no bootstrap CLI options, either for content or path, we can load the server with
// an empty config.
TEST_P(ServerInstanceImplTest, NoOptionsPassed) {
  thread_local_ = std::make_unique<ThreadLocal::InstanceImpl>();
  init_manager_ = std::make_unique<Init::ManagerImpl>("Server");
  server_ = std::make_unique<InstanceImpl>(
      *init_manager_, options_, time_system_, hooks_, restart_, stats_store_, fakelock_,
      std::make_unique<NiceMock<Random::MockRandomGenerator>>(), *thread_local_,
      Thread::threadFactoryForTest(), Filesystem::fileSystemForTest(), nullptr);
  EXPECT_THROW_WITH_MESSAGE(
      server_->initialize(std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1"),
                          component_factory_),
      EnvoyException,
      "At least one of --config-path or --config-yaml or Options::configProto() should be "
      "non-empty");
}

// Validate that when std::exception is unexpectedly thrown, we exit safely.
// This is a regression test for when we used to crash.
TEST_P(ServerInstanceImplTest, StdExceptionThrowInConstructor) {
  EXPECT_CALL(restart_, initialize(_, _)).WillOnce(InvokeWithoutArgs([] {
    throw(std::runtime_error("foobar"));
  }));
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/test_data/server/node_bootstrap.yaml"),
                            std::runtime_error, "foobar");
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
  EXPECT_THROW_WITH_MESSAGE(initialize("test/server/test_data/server/node_bootstrap.yaml"),
                            FakeException, "foobar");
}

TEST_P(ServerInstanceImplTest, MutexContentionEnabled) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.mutex_tracing_enabled_ = true;
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
}

TEST_P(ServerInstanceImplTest, CoreDumpEnabled) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  options_.core_dump_enabled_ = true;
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
}

TEST_P(ServerInstanceImplTest, NoHttpTracing) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));
  EXPECT_THAT(envoy::config::trace::v3::Tracing{},
              ProtoEq(server_->httpContext().defaultTracingConfig()));
}

TEST_P(ServerInstanceImplTest, DEPRECATED_FEATURE_TEST(DISABLED_ZipkinHttpTracingEnabled)) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/test_data/server/zipkin_tracing_deprecated_config.yaml"));
  EXPECT_EQ("zipkin", server_->httpContext().defaultTracingConfig().http().name());
}

class TestObject : public ProcessObject {
public:
  void setFlag(bool value) { boolean_flag_ = value; }

  bool boolean_flag_ = true;
};

TEST_P(ServerInstanceImplTest, WithProcessContext) {
  TestObject object;
  process_object_ = &object;

  EXPECT_NO_THROW(initialize("test/server/test_data/server/empty_bootstrap.yaml"));

  auto context = server_->processContext();
  auto& object_from_context = dynamic_cast<TestObject&>(context->get().get());
  EXPECT_EQ(&object_from_context, &object);
  EXPECT_TRUE(object_from_context.boolean_flag_);

  object.boolean_flag_ = false;
  EXPECT_FALSE(object_from_context.boolean_flag_);
}

class FooBootstrapExtension : public BootstrapExtension {};

TEST_P(ServerInstanceImplTest, WithBootstrapExtensions) {
  NiceMock<Configuration::MockBootstrapExtensionFactory> mock_factory;
  EXPECT_CALL(mock_factory, createEmptyConfigProto()).WillRepeatedly(Invoke([]() {
    return std::make_unique<test::common::config::DummyConfig>();
  }));
  EXPECT_CALL(mock_factory, name()).WillRepeatedly(Return("envoy_test.bootstrap.foo"));

  EXPECT_CALL(mock_factory, createBootstrapExtension(_, _))
      .WillOnce(
          Invoke([](const Protobuf::Message& config, Configuration::ServerFactoryContext& ctx) {
            const auto* proto = dynamic_cast<const test::common::config::DummyConfig*>(&config);
            EXPECT_NE(nullptr, proto);
            EXPECT_EQ(proto->a(), "foo");
            auto mock_extension = std::make_unique<MockBootstrapExtension>();
            EXPECT_CALL(*mock_extension, onServerInitialized()).WillOnce(Invoke([&ctx]() {
              // call to cluster manager, to make sure it is not nullptr.
              ctx.clusterManager().clusters();
            }));
            return mock_extension;
          }));

  Registry::InjectFactory<Configuration::BootstrapExtensionFactory> registered_factory(
      mock_factory);

  EXPECT_NO_THROW(initialize("test/server/test_data/server/bootstrap_extensions.yaml"));
}

TEST_P(ServerInstanceImplTest, WithBootstrapExtensionsThrowingError) {
  NiceMock<Configuration::MockBootstrapExtensionFactory> mock_factory;
  EXPECT_CALL(mock_factory, createEmptyConfigProto()).WillRepeatedly(Invoke([]() {
    return std::make_unique<test::common::config::DummyConfig>();
  }));
  EXPECT_CALL(mock_factory, name()).WillRepeatedly(Return("envoy_test.bootstrap.foo"));
  EXPECT_CALL(mock_factory, createBootstrapExtension(_, _))
      .WillOnce(Invoke([](const Protobuf::Message&,
                          Configuration::ServerFactoryContext&) -> BootstrapExtensionPtr {
        throw EnvoyException("Unable to initiate mock_bootstrap_extension.");
      }));

  Registry::InjectFactory<Configuration::BootstrapExtensionFactory> registered_factory(
      mock_factory);

  EXPECT_THROW_WITH_REGEX(initialize("test/server/test_data/server/bootstrap_extensions.yaml"),
                          EnvoyException, "Unable to initiate mock_bootstrap_extension.");
}

TEST_P(ServerInstanceImplTest, WithUnknownBootstrapExtensions) {
  EXPECT_THROW_WITH_REGEX(initialize("test/server/test_data/server/bootstrap_extensions.yaml"),
                          EnvoyException,
                          "Didn't find a registered implementation for 'envoy_test.bootstrap.foo' "
                          "with type URL: 'test.common.config.DummyConfig'");
}

// Insufficient support on Windows.
#ifndef WIN32
class SafeFatalAction : public Configuration::FatalAction {
public:
  void run(absl::Span<const ScopeTrackedObject* const> /*tracked_objects*/) override {
    std::cerr << "Called SafeFatalAction" << std::endl;
  }

  bool isAsyncSignalSafe() const override { return true; }
};

class UnsafeFatalAction : public Configuration::FatalAction {
public:
  void run(absl::Span<const ScopeTrackedObject* const> /*tracked_objects*/) override {
    std::cerr << "Called UnsafeFatalAction" << std::endl;
  }

  bool isAsyncSignalSafe() const override { return false; }
};

TEST_P(ServerInstanceImplTest, WithFatalActions) {
  // Inject Safe Factory.
  NiceMock<Configuration::MockFatalActionFactory> mock_safe_factory;
  EXPECT_CALL(mock_safe_factory, createEmptyConfigProto()).WillRepeatedly(Invoke([]() {
    return std::make_unique<envoy::config::bootstrap::v3::FatalAction>();
  }));
  EXPECT_CALL(mock_safe_factory, name()).WillRepeatedly(Return("envoy_test.fatal_action.safe"));

  Registry::InjectFactory<Configuration::FatalActionFactory> registered_safe_factory(
      mock_safe_factory);

  // Inject Unsafe Factory
  NiceMock<Configuration::MockFatalActionFactory> mock_unsafe_factory;
  EXPECT_CALL(mock_unsafe_factory, createEmptyConfigProto()).WillRepeatedly(Invoke([]() {
    return std::make_unique<ProtobufWkt::Struct>();
  }));
  EXPECT_CALL(mock_unsafe_factory, name()).WillRepeatedly(Return("envoy_test.fatal_action.unsafe"));

  Registry::InjectFactory<Configuration::FatalActionFactory> registered_unsafe_factory(
      mock_unsafe_factory);

  EXPECT_DEATH(
      {
        EXPECT_CALL(mock_safe_factory, createFatalActionFromProto(_, _))
            .WillOnce(
                Invoke([](const envoy::config::bootstrap::v3::FatalAction& /*config*/,
                          Instance* /*server*/) { return std::make_unique<SafeFatalAction>(); }));
        EXPECT_CALL(mock_unsafe_factory, createFatalActionFromProto(_, _))
            .WillOnce(
                Invoke([](const envoy::config::bootstrap::v3::FatalAction& /*config*/,
                          Instance* /*server*/) { return std::make_unique<UnsafeFatalAction>(); }));
        absl::Notification abort_called;
        auto server_thread =
            startTestServer("test/server/test_data/server/fatal_actions.yaml", false);
        // Trigger SIGABRT, wait for the ABORT
        server_->dispatcher().post([&] {
          abort();
          abort_called.Notify();
        });

        abort_called.WaitForNotification();
      },
      "");
}
#endif

// Static configuration validation. We test with both allow/reject settings various aspects of
// configuration from YAML.
class StaticValidationTest
    : public ServerInstanceImplTestBase,
      public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>> {
protected:
  StaticValidationTest() {
    version_ = std::get<0>(GetParam());
    options_.service_cluster_name_ = "some_cluster_name";
    options_.service_node_name_ = "some_node_name";
    options_.allow_unknown_static_fields_ = std::get<1>(GetParam());
    // By inverting the static validation value, we can hopefully catch places we may have confused
    // static/dynamic validation.
    options_.reject_unknown_dynamic_fields_ = options_.allow_unknown_static_fields_;
  }

  AssertionResult validate(absl::string_view yaml_filename) {
    const std::string path =
        absl::StrCat("test/server/test_data/static_validation/", yaml_filename);
    try {
      initialize(path);
    } catch (EnvoyException&) {
      return options_.allow_unknown_static_fields_ ? AssertionFailure() : AssertionSuccess();
    }
    return options_.allow_unknown_static_fields_ ? AssertionSuccess() : AssertionFailure();
  }
};

std::string staticValidationTestParamsToString(
    const ::testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
  return fmt::format(
      "{}_{}",
      TestUtility::ipTestParamsToString(
          ::testing::TestParamInfo<Network::Address::IpVersion>(std::get<0>(params.param), 0)),
      std::get<1>(params.param) ? "with_allow_unknown_static_fields"
                                : "without_allow_unknown_static_fields");
}

INSTANTIATE_TEST_SUITE_P(
    IpVersions, StaticValidationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    staticValidationTestParamsToString);

TEST_P(StaticValidationTest, BootstrapUnknownField) {
  EXPECT_TRUE(validate("bootstrap_unknown_field.yaml"));
}

TEST_P(StaticValidationTest, ListenerUnknownField) {
  EXPECT_TRUE(validate("listener_unknown_field.yaml"));
}

TEST_P(StaticValidationTest, NetworkFilterUnknownField) {
  EXPECT_TRUE(validate("network_filter_unknown_field.yaml"));
}

TEST_P(StaticValidationTest, ClusterUnknownField) {
  EXPECT_TRUE(validate("cluster_unknown_field.yaml"));
}

// Custom StatsSink that registers both a Cluster update callback and Server lifecycle callback.
class CallbacksStatsSink : public Stats::Sink, public Upstream::ClusterUpdateCallbacks {
public:
  CallbacksStatsSink(Server::Configuration::ServerFactoryContext& server)
      : cluster_removal_cb_handle_(
            server.clusterManager().addThreadLocalClusterUpdateCallbacks(*this)),
        lifecycle_cb_handle_(server.lifecycleNotifier().registerCallback(
            ServerLifecycleNotifier::Stage::ShutdownExit,
            [this]() { cluster_removal_cb_handle_.reset(); })) {}

  // Stats::Sink
  void flush(Stats::MetricSnapshot&) override {}
  void onHistogramComplete(const Stats::Histogram&, uint64_t) override {}

  // Upstream::ClusterUpdateCallbacks
  void onClusterAddOrUpdate(absl::string_view, Upstream::ThreadLocalClusterCommand&) override {}
  void onClusterRemoval(const std::string&) override {}

private:
  Upstream::ClusterUpdateCallbacksHandlePtr cluster_removal_cb_handle_;
  ServerLifecycleNotifier::HandlePtr lifecycle_cb_handle_;
};

class CallbacksStatsSinkFactory : public Server::Configuration::StatsSinkFactory {
public:
  // StatsSinkFactory
  Stats::SinkPtr createStatsSink(const Protobuf::Message&,
                                 Server::Configuration::ServerFactoryContext& server) override {
    return std::make_unique<CallbacksStatsSink>(server);
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    // Using Struct instead of a custom per-filter empty config proto
    // This is only allowed in tests.
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Struct()};
  }

  std::string name() const override { return "envoy.callbacks_stats_sink"; }
};

// This test ensures that a stats sink can use cluster update callbacks properly. Using only a
// cluster update callback is insufficient to protect against double-free bugs, so a server
// lifecycle callback is also used to ensure that the cluster update callback is freed during
// Server::Instance's destruction. See issue #9292 for more details.
TEST_P(ServerInstanceImplTest, CallbacksStatsSinkTest) {
  CallbacksStatsSinkFactory factory;
  Registry::InjectFactory<Server::Configuration::StatsSinkFactory> registered(factory);

  initialize("test/server/test_data/server/callbacks_stats_sink_bootstrap.yaml");
  // Necessary to trigger server lifecycle callbacks, otherwise only terminate() is called.
  server_->shutdown();
}

// Validate that disabled extension is reflected in the list of Node extensions.
TEST_P(ServerInstanceImplTest, DisabledExtension) {
  OptionsImplBase::disableExtensions({"envoy.filters.http/envoy.filters.http.buffer"});
  initialize("test/server/test_data/server/node_bootstrap.yaml");
  bool disabled_filter_found = false;
  for (const auto& extension : server_->localInfo().node().extensions()) {
    // TODO(zuercher): remove envoy.buffer when old-style name deprecation is completed.
    if (extension.category() == "envoy.filters.http" &&
        (extension.name() == "envoy.filters.http.buffer" || extension.name() == "envoy.buffer")) {
      ASSERT_TRUE(extension.disabled());
      disabled_filter_found = true;
    } else {
      ASSERT_FALSE(extension.disabled());
    }
  }
  ASSERT_TRUE(disabled_filter_found);
}

TEST_P(ServerInstanceImplTest, NullProcessContextTest) {
  // These are already the defaults. Repeated here for clarity.
  process_object_ = nullptr;
  process_context_ = nullptr;

  initialize("test/server/test_data/server/empty_bootstrap.yaml");
  ProcessContextOptRef context = server_->processContext();
  EXPECT_FALSE(context.has_value());
}

TEST_P(ServerInstanceImplTest, AdminAccessLogFilter) {
  options_.service_cluster_name_ = "some_cluster_name";
  options_.service_node_name_ = "some_node_name";
  EXPECT_NO_THROW(initialize("test/server/test_data/server/access_log_filter_bootstrap.yaml"));
}

TEST_P(ServerInstanceImplTest, BootstrapApplicationLogsAndCLIThrows) {
  EXPECT_CALL(options_, logFormatSet()).WillRepeatedly(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      initialize("test/server/test_data/server/json_application_log.yaml"), EnvoyException,
      "Only one of ApplicationLogConfig.log_format or CLI option --log-format can be specified.");
}

TEST_P(ServerInstanceImplTest, JsonApplicationLog) {
  EXPECT_NO_THROW(initialize("test/server/test_data/server/json_application_log.yaml"));

  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto& log) {
    EXPECT_NO_THROW(Json::Factory::loadFromString(std::string(msg)));
    EXPECT_THAT(msg, HasSubstr("{\"MessageFromProto\":\"hello\"}"));
    EXPECT_EQ(log.logger_name, "misc");
  }));

  ENVOY_LOG_MISC(info, "hello");
}

TEST_P(ServerInstanceImplTest, JsonApplicationLogFailWithForbiddenFlagv) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize("test/server/test_data/server/json_application_log_forbidden_flagv.yaml"),
      EnvoyException,
      "setJsonLogFormat error: INVALID_ARGUMENT: Usage of %v is unavailable for JSON log formats");
}

TEST_P(ServerInstanceImplTest, JsonApplicationLogFailWithForbiddenFlagUnderscore) {
  EXPECT_THROW_WITH_MESSAGE(
      initialize("test/server/test_data/server/json_application_log_forbidden_flag_.yaml"),
      EnvoyException,
      "setJsonLogFormat error: INVALID_ARGUMENT: Usage of %_ is unavailable for JSON log formats");
}

TEST_P(ServerInstanceImplTest, TextApplicationLog) {
  EXPECT_NO_THROW(initialize("test/server/test_data/server/text_application_log.yaml"));

  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto& log) {
    EXPECT_THAT(msg, HasSubstr("[lvl: info][msg: hello]"));
    EXPECT_EQ(log.logger_name, "misc");
  }));

  ENVOY_LOG_MISC(info, "hello");
}

} // namespace
} // namespace Server
} // namespace Envoy
