#include <memory>
#include <vector>

#include "envoy/server/filter_config.h"

#include "source/extensions/listener_managers/validation_listener_manager/validation_listener_manager.h"
#include "source/server/admin/admin_filter.h"
#include "source/server/config_validation/server.h"
#include "source/server/process_context_impl.h"

#include "test/integration/server.h"
#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/options.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_time.h"

using testing::HasSubstr;
using testing::Return;

namespace Envoy {
namespace Server {
namespace {

class NullptrComponentFactory : public TestComponentFactory {
public:
  DrainManagerPtr createDrainManager(Instance&) override { return nullptr; }
};

// Test param is the path to the config file to validate.
class ValidationServerTest : public testing::TestWithParam<std::string> {
public:
  static void setupTestDirectory() {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});
    directory_ = TestEnvironment::temporaryDirectory() + "/test/config_test/";
  }

  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    setupTestDirectory();
  }

protected:
  ValidationServerTest() : options_(directory_ + GetParam()) {}

  static std::string directory_;
  testing::NiceMock<MockOptions> options_;
  TestComponentFactory component_factory_;
};

std::string ValidationServerTest::directory_ = "";

// ValidationServerTest_1 is created only to run different set of parameterized
// tests than set of tests for ValidationServerTest.
class ValidationServerTest1 : public ValidationServerTest {
public:
  static const std::vector<std::string> getAllConfigFiles() {
    setupTestDirectory();

    auto files = TestUtility::listFiles(ValidationServerTest::directory_, false);

    // Strip directory part. options_ adds it for each test.
    for (auto& file : files) {
      file = file.substr(directory_.length() + 1);
    }
    return files;
  }
};

// RuntimeFeatureValidationServerTest is used to test validation with non-default runtime
// values.
class RuntimeFeatureValidationServerTest : public ValidationServerTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    setupTestDirectory();
  }

  static void setupTestDirectory() {
    directory_ =
        TestEnvironment::runfilesDirectory("envoy/test/server/config_validation/test_data/");
  }

  static const std::vector<std::string> getAllConfigFiles() {
    setupTestDirectory();
    return {"runtime_config.yaml"};
  }
};

class JsonApplicationLogsValidationServerTest : public ValidationServerTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    setupTestDirectory();
  }

  static void setupTestDirectory() {
    directory_ =
        TestEnvironment::runfilesDirectory("envoy/test/server/config_validation/test_data/");
  }

  static const std::vector<std::string> getAllConfigFiles() {
    setupTestDirectory();
    return {"json_application_logs.yaml"};
  }
};

class JsonApplicationLogsValidationServerForbiddenFlagvTest : public ValidationServerTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    setupTestDirectory();
  }
  static void setupTestDirectory() {
    directory_ =
        TestEnvironment::runfilesDirectory("envoy/test/server/config_validation/test_data/");
  }
  static const std::vector<std::string> getAllConfigFiles() {
    setupTestDirectory();
    return {"json_application_logs_forbidden_flagv.yaml"};
  }
};

class JsonApplicationLogsValidationServerForbiddenFlagUnderscoreTest : public ValidationServerTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    setupTestDirectory();
  }
  static void setupTestDirectory() {
    directory_ =
        TestEnvironment::runfilesDirectory("envoy/test/server/config_validation/test_data/");
  }
  static const std::vector<std::string> getAllConfigFiles() {
    setupTestDirectory();
    return {"json_application_logs_forbidden_flag_.yaml"};
  }
};

class TextApplicationLogsValidationServerTest : public ValidationServerTest {
public:
  static void SetUpTestSuite() { // NOLINT(readability-identifier-naming)
    setupTestDirectory();
  }

  static void setupTestDirectory() {
    directory_ =
        TestEnvironment::runfilesDirectory("envoy/test/server/config_validation/test_data/");
  }

  static const std::vector<std::string> getAllConfigFiles() {
    setupTestDirectory();
    return {"text_application_logs.yaml"};
  }
};

TEST_P(ValidationServerTest, Validate) {
  EXPECT_TRUE(validateConfig(options_, Network::Address::InstanceConstSharedPtr(),
                             component_factory_, Thread::threadFactoryForTest(),
                             Filesystem::fileSystemForTest()));
}

TEST_P(ValidationServerTest, NoopLifecycleNotifier) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  ValidationInstance server(options_, time_system.timeSystem(),
                            Network::Address::InstanceConstSharedPtr(), stats_store,
                            access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                            Filesystem::fileSystemForTest());
  server.registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit, [] { FAIL(); });
  server.registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                          [](Event::PostCb) { FAIL(); });
  server.setSinkPredicates(std::make_unique<testing::NiceMock<Stats::MockSinkPredicates>>());
  server.shutdown();
}

// A test to increase coverage of dummy methods (naively implemented methods
// needed for interface implementation).
TEST_P(ValidationServerTest, DummyMethodsTest) {
  // Setup the server instance.
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  ValidationInstance server(options_, time_system.timeSystem(),
                            Network::Address::InstanceConstSharedPtr(), stats_store,
                            access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                            Filesystem::fileSystemForTest());

  // Execute dummy methods.
  server.drainListeners(absl::nullopt);
  server.failHealthcheck(true);
  server.lifecycleNotifier();
  server.secretManager();
  server.drainManager();
  EXPECT_FALSE(server.isShutdown());
  EXPECT_FALSE(server.healthCheckFailed());
  server.grpcContext();
  EXPECT_FALSE(server.processContext().has_value());
  server.timeSource();
  server.mutexTracer();
  server.flushStats();
  server.statsConfig();
  server.transportSocketFactoryContext();
  server.shutdownAdmin();
  server.shutdown();

  server.admin()->addStreamingHandler("", "", nullptr, false, false);
  server.admin()->addListenerToHandler(nullptr);
  server.admin()->closeSocket();
  server.admin()->startHttpListener({}, nullptr, nullptr);
  AdminFilter filter(*server.admin());
  EXPECT_TRUE(server.admin()->makeRequest(filter) == nullptr);

  Network::MockTcpListenerCallbacks listener_callbacks;
  Network::MockListenerConfig listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;

  server.dnsResolver()->resolve("", Network::DnsLookupFamily::All, nullptr);

  ValidationListenerComponentFactory listener_component_factory(server);
  listener_component_factory.getTcpListenerConfigProviderManager();
}

class TestObject : public ProcessObject {
public:
  void setFlag(bool value) { boolean_flag_ = value; }

  bool boolean_flag_ = true;
};

TEST_P(ValidationServerTest, NoProcessContext) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  ValidationInstance server(options_, time_system.timeSystem(),
                            Network::Address::InstanceConstSharedPtr(), stats_store,
                            access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                            Filesystem::fileSystemForTest());
  EXPECT_FALSE(server.processContext().has_value());
  server.shutdown();
}

TEST_P(ValidationServerTest, WithProcessContext) {
  TestObject object;
  ProcessContextImpl process_context(object);
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  ValidationInstance server(options_, time_system.timeSystem(),
                            Network::Address::InstanceConstSharedPtr(), stats_store,
                            access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                            Filesystem::fileSystemForTest(), process_context);
  EXPECT_TRUE(server.processContext().has_value());
  auto context = server.processContext();
  auto& object_from_context = dynamic_cast<TestObject&>(context->get().get());
  EXPECT_EQ(&object_from_context, &object);
  EXPECT_TRUE(object_from_context.boolean_flag_);

  object.boolean_flag_ = false;
  EXPECT_FALSE(object_from_context.boolean_flag_);
  server.shutdown();
}

// TODO(rlazarus): We'd like use this setup to replace //test/config_test (that is, run it against
// all the example configs) but can't until light validation is implemented, mocking out access to
// the filesystem for TLS certs, etc. In the meantime, these are the example configs that work
// as-is. (Note, /dev/stdout as an access log file is invalid on Windows, no equivalent /dev/
// exists.)

auto testing_values =
    ::testing::Values("front-proxy_envoy.yaml", "envoyproxy_io_proxy.yaml",
#if defined(WIN32) && defined(SO_ORIGINAL_DST)
                      "configs_original-dst-cluster_proxy_config.yaml",
#endif
                      "grpc-bridge_server_envoy-proxy.yaml", "front-proxy_service-envoy.yaml");

INSTANTIATE_TEST_SUITE_P(ValidConfigs, ValidationServerTest, testing_values);

// Just make sure that all configs can be ingested without a crash. Processing of config files
// may not be successful, but there should be no crash.
TEST_P(ValidationServerTest1, RunWithoutCrash) {
  auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
  validateConfig(options_, local_address, component_factory_, Thread::threadFactoryForTest(),
                 Filesystem::fileSystemForTest());
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(AllConfigs, ValidationServerTest1,
                         ::testing::ValuesIn(ValidationServerTest1::getAllConfigFiles()));

// A test to ensure that ENVOY_BUGs are handled when the component factory returns a nullptr for
// the drain manager.
TEST_P(RuntimeFeatureValidationServerTest, DrainManagerNullptrCheck) {
  // Setup the server instance with a component factory that returns a null DrainManager.
  NullptrComponentFactory component_factory;
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  EXPECT_ENVOY_BUG(ValidationInstance server(options_, time_system.timeSystem(),
                                             Network::Address::InstanceConstSharedPtr(),
                                             stats_store, access_log_lock, component_factory,
                                             Thread::threadFactoryForTest(),
                                             Filesystem::fileSystemForTest()),
                   "Component factory should not return nullptr from createDrainManager()");
}

TEST_P(RuntimeFeatureValidationServerTest, ValidRuntimeLoader) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  ValidationInstance server(options_, time_system.timeSystem(),
                            Network::Address::InstanceConstSharedPtr(), stats_store,
                            access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                            Filesystem::fileSystemForTest());
  EXPECT_TRUE(server.runtime().snapshot().getBoolean("test.runtime.loaded", false));
  server.registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit, [] { FAIL(); });
  server.registerCallback(ServerLifecycleNotifier::Stage::ShutdownExit,
                          [](Event::PostCb) { FAIL(); });
  server.setSinkPredicates(std::make_unique<testing::NiceMock<Stats::MockSinkPredicates>>());
  server.shutdown();
}

// Test the admin handler stubs used in validation
TEST(ValidationTest, Admin) {
  auto local_address =
      Network::Test::getCanonicalLoopbackAddress(TestEnvironment::getIpVersionsForTest()[0]);

  ValidationAdmin admin(local_address);
  std::string empty = "";
  Server::Admin::HandlerCb cb;
  EXPECT_TRUE(admin.addHandler(empty, empty, cb, false, false));
  EXPECT_TRUE(admin.removeHandler(empty));
  EXPECT_EQ(1, admin.concurrency());
  admin.socket();
}

INSTANTIATE_TEST_SUITE_P(
    AllConfigs, RuntimeFeatureValidationServerTest,
    ::testing::ValuesIn(RuntimeFeatureValidationServerTest::getAllConfigFiles()));

TEST_P(JsonApplicationLogsValidationServerTest, BootstrapApplicationLogsAndCLIThrows) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  EXPECT_CALL(options_, logFormatSet()).WillRepeatedly(Return(true));
  EXPECT_THROW_WITH_MESSAGE(
      ValidationInstance server(options_, time_system.timeSystem(),
                                Network::Address::InstanceConstSharedPtr(), stats_store,
                                access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                                Filesystem::fileSystemForTest()),
      EnvoyException,
      "Only one of ApplicationLogConfig.log_format or CLI option --log-format can be specified.");
}

TEST_P(JsonApplicationLogsValidationServerTest, JsonApplicationLogs) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  ValidationInstance server(options_, time_system.timeSystem(),
                            Network::Address::InstanceConstSharedPtr(), stats_store,
                            access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                            Filesystem::fileSystemForTest());

  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto& log) {
    EXPECT_THAT(msg, HasSubstr("{\"MessageFromProto\":\"hello\"}"));
    EXPECT_EQ(log.logger_name, "misc");
  }));

  ENVOY_LOG_MISC(info, "hello");
  server.shutdown();
}

INSTANTIATE_TEST_SUITE_P(
    AllConfigs, JsonApplicationLogsValidationServerTest,
    ::testing::ValuesIn(JsonApplicationLogsValidationServerTest::getAllConfigFiles()));

TEST_P(JsonApplicationLogsValidationServerForbiddenFlagvTest, TestForbiddenFlag) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  EXPECT_THROW_WITH_MESSAGE(
      ValidationInstance server(options_, time_system.timeSystem(),
                                Network::Address::InstanceConstSharedPtr(), stats_store,
                                access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                                Filesystem::fileSystemForTest()),
      EnvoyException,
      "setJsonLogFormat error: INVALID_ARGUMENT: Usage of %v is unavailable for JSON log formats");
}

INSTANTIATE_TEST_SUITE_P(
    AllConfigs, JsonApplicationLogsValidationServerForbiddenFlagvTest,
    ::testing::ValuesIn(
        JsonApplicationLogsValidationServerForbiddenFlagvTest::getAllConfigFiles()));

TEST_P(JsonApplicationLogsValidationServerForbiddenFlagUnderscoreTest, TestForbiddenFlag) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  EXPECT_THROW_WITH_MESSAGE(
      ValidationInstance server(options_, time_system.timeSystem(),
                                Network::Address::InstanceConstSharedPtr(), stats_store,
                                access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                                Filesystem::fileSystemForTest()),
      EnvoyException,
      "setJsonLogFormat error: INVALID_ARGUMENT: Usage of %_ is unavailable for JSON log formats");
}

INSTANTIATE_TEST_SUITE_P(
    AllConfigs, JsonApplicationLogsValidationServerForbiddenFlagUnderscoreTest,
    ::testing::ValuesIn(
        JsonApplicationLogsValidationServerForbiddenFlagUnderscoreTest::getAllConfigFiles()));

TEST_P(TextApplicationLogsValidationServerTest, TextApplicationLogs) {
  Thread::MutexBasicLockable access_log_lock;
  Stats::IsolatedStoreImpl stats_store;
  DangerousDeprecatedTestTime time_system;
  ValidationInstance server(options_, time_system.timeSystem(),
                            Network::Address::InstanceConstSharedPtr(), stats_store,
                            access_log_lock, component_factory_, Thread::threadFactoryForTest(),
                            Filesystem::fileSystemForTest());

  Envoy::Logger::Registry::setLogLevel(spdlog::level::info);
  MockLogSink sink(Envoy::Logger::Registry::getSink());
  EXPECT_CALL(sink, log(_, _)).WillOnce(Invoke([](auto msg, auto& log) {
    EXPECT_THAT(msg, HasSubstr("[lvl: info][msg: hello]"));
    EXPECT_EQ(log.logger_name, "misc");
  }));

  ENVOY_LOG_MISC(info, "hello");
  server.shutdown();
}

INSTANTIATE_TEST_SUITE_P(
    AllConfigs, TextApplicationLogsValidationServerTest,
    ::testing::ValuesIn(TextApplicationLogsValidationServerTest::getAllConfigFiles()));

} // namespace
} // namespace Server
} // namespace Envoy
