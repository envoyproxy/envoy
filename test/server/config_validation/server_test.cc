#include <vector>

#include "server/config_validation/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Server {
namespace {

// Test param is the path to the config file to validate.
class ValidationServerTest : public testing::TestWithParam<std::string> {
public:
  static void SetupTestDirectory() {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});
    directory_ = TestEnvironment::temporaryDirectory() + "/test/config_test/";
  }

  static void SetUpTestSuite() { SetupTestDirectory(); }

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
  static const std::vector<std::string> GetAllConfigFiles() {
    SetupTestDirectory();

    auto files = TestUtility::listFiles(ValidationServerTest::directory_, false);

    // Strip directory part. options_ adds it for each test.
    for (auto& file : files) {
      file = file.substr(directory_.length() + 1);
    }
    return files;
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
  server.shutdown();
}

// TODO(rlazarus): We'd like use this setup to replace //test/config_test (that is, run it against
// all the example configs) but can't until light validation is implemented, mocking out access to
// the filesystem for TLS certs, etc. In the meantime, these are the example configs that work
// as-is.
INSTANTIATE_TEST_SUITE_P(ValidConfigs, ValidationServerTest,
                         ::testing::Values("front-proxy_front-envoy.yaml",
                                           "google_com_proxy.v2.yaml",
                                           "grpc-bridge_server_envoy-proxy.yaml",
                                           "front-proxy_service-envoy.yaml"));

// Just make sure that all configs can be ingested without a crash. Processing of config files
// may not be successful, but there should be no crash.
TEST_P(ValidationServerTest_1, RunWithoutCrash) {
  auto local_address = Network::Utility::getLocalAddress(options_.localAddressIpVersion());
  validateConfig(options_, local_address, component_factory_, Thread::threadFactoryForTest(),
                 Filesystem::fileSystemForTest());
  SUCCEED();
}

INSTANTIATE_TEST_SUITE_P(AllConfigs, ValidationServerTest_1,
                         ::testing::ValuesIn(ValidationServerTest_1::GetAllConfigFiles()));

} // namespace
} // namespace Server
} // namespace Envoy
