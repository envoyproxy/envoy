#include <vector>

#include "server/config_validation/server.h"

#include "test/integration/server.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"

namespace Envoy {
namespace Server {

// Test param is the path to the config file to validate.
class ValidationServerTest : public testing::TestWithParam<std::string> {
public:
  static void SetupTestDirectory() {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});
    directory_ = TestEnvironment::temporaryDirectory() + "/test/config_test/";
  }

  static void SetUpTestCase() { SetupTestDirectory(); }

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
    for (std::vector<std::string>::iterator it = files.begin(); it != files.end(); it++) {
      (*it) = it->substr(directory_.length() + 1);
    }
    return files;
  }
};

TEST_P(ValidationServerTest, Validate) {
  EXPECT_TRUE(
      validateConfig(options_, Network::Address::InstanceConstSharedPtr(), component_factory_));
}

// TODO(rlazarus): We'd like use this setup to replace //test/config_test (that is, run it against
// all the example configs) but can't until light validation is implemented, mocking out access to
// the filesystem for TLS certs, etc. In the meantime, these are the example configs that work
// as-is.
INSTANTIATE_TEST_CASE_P(ValidConfigs, ValidationServerTest,
                        ::testing::Values("front-envoy.yaml", "google_com_proxy.v2.yaml",
                                          "s2s-grpc-envoy.yaml", "service-envoy.yaml"));

// Just make sure that all configs can be ingested without a crash. Processing of config files
// may not be successful, but there should be no crash.
TEST_P(ValidationServerTest_1, RunWithoutCrash) {
  validateConfig(options_, Network::Address::InstanceConstSharedPtr(), component_factory_);
  SUCCEED();
}

INSTANTIATE_TEST_CASE_P(AllConfigs, ValidationServerTest_1,
                        ::testing::ValuesIn(ValidationServerTest_1::GetAllConfigFiles()));
} // namespace Server
} // namespace Envoy
