#include <vector>

#include "envoy/server/filter_config.h"

#include "server/config_validation/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/options.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/registry.h"
#include "test/test_common/test_time.h"

namespace Envoy {
namespace Server {
namespace {

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
class ValidationServerTest_1 : public ValidationServerTest {
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

    auto files = TestUtility::listFiles(ValidationServerTest::directory_, false);

    // Strip directory part. options_ adds it for each test.
    for (auto& file : files) {
      file = file.substr(directory_.length() + 1);
    }
    return files;
  }

  class TestConfigFactory : public Configuration::NamedNetworkFilterConfigFactory {
  public:
    std::string name() const override { return "envoy.filters.network.test"; }

    Network::FilterFactoryCb createFilterFactoryFromProto(const Protobuf::Message&,
                                                          Configuration::FactoryContext&) override {
      // Validate that the validation server loaded the runtime data and installed the singleton.
      auto* runtime = Runtime::LoaderSingleton::getExisting();
      if (runtime == nullptr) {
        throw EnvoyException("Runtime::LoaderSingleton == nullptr");
      }

      if (!runtime->threadsafeSnapshot()->getBoolean("test.runtime.loaded", false)) {
        throw EnvoyException(
            "Found Runtime::LoaderSingleton, got wrong value for test.runtime.loaded");
      }

      return [](Network::FilterManager&) {};
    }

    ProtobufTypes::MessagePtr createEmptyConfigProto() override {
      return ProtobufTypes::MessagePtr{new ProtobufWkt::Struct()};
    }

    bool isTerminalFilter() override { return true; }
  };
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
  server.shutdown();
}

// TODO(rlazarus): We'd like use this setup to replace //test/config_test (that is, run it against
// all the example configs) but can't until light validation is implemented, mocking out access to
// the filesystem for TLS certs, etc. In the meantime, these are the example configs that work
// as-is. (Note, /dev/stdout as an access log file is invalid on Windows, no equivalent /dev/
// exists.)

auto testing_values = ::testing::Values("front-proxy_front-envoy.yaml", "google_com_proxy.v2.yaml",
#ifndef WIN32
                                        "grpc-bridge_server_envoy-proxy.yaml",
#endif
                                        "front-proxy_service-envoy.yaml");

INSTANTIATE_TEST_SUITE_P(ValidConfigs, ValidationServerTest, testing_values);

// Just make sure that all configs can be ingested without a crash. Processing of config files
// may not be successful, but there should be no crash.
TEST_P(ValidationServerTest_1, RunWithoutCrash) {
  auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
  validateConfig(options_, local_address, component_factory_, Thread::threadFactoryForTest(),
                 Filesystem::fileSystemForTest());
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(AllConfigs, ValidationServerTest_1,
                         ::testing::ValuesIn(ValidationServerTest_1::getAllConfigFiles()));

TEST_P(RuntimeFeatureValidationServerTest, ValidRuntimeLoaderSingleton) {
  TestConfigFactory factory;
  Registry::InjectFactory<Configuration::NamedNetworkFilterConfigFactory> registration(factory);

  auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());

  // If this fails, it's likely because TestConfigFactory threw an exception related to the
  // runtime loader.
  ASSERT_TRUE(validateConfig(options_, local_address, component_factory_,
                             Thread::threadFactoryForTest(), Filesystem::fileSystemForTest()));
}

INSTANTIATE_TEST_SUITE_P(
    AllConfigs, RuntimeFeatureValidationServerTest,
    ::testing::ValuesIn(RuntimeFeatureValidationServerTest::getAllConfigFiles()));

} // namespace
} // namespace Server
} // namespace Envoy
