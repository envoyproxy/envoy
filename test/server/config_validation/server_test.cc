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

    std::string all_file_names =
        TestEnvironment::readFileToStringForTest(directory_ + "all_config_files.txt");
    std::vector<std::string> files;
    // all_config_files.txt contains a list of all config files from tar file created by
    // example_configs_test_setup.sh Names of the files are separated by EOL. Tokenize those  names
    // and put them into a std container.
    size_t pos = 0;
    size_t prev_pos = pos;
    while (pos != std::string::npos) {
      pos = all_file_names.find('\n', pos);
      if (pos != std::string::npos) {
        files.emplace_back(all_file_names.substr(prev_pos, pos - prev_pos));
        pos += 1;
        prev_pos = pos;
      } else {
        // last item. Process it if not empty line.
        if (prev_pos < all_file_names.length()) {
          files.emplace_back(all_file_names.substr(prev_pos));
        }
      }
    }
    // Make sure that reading file with config file names and tokenizing was successful.
    // If something went wrong, files container will be empty and parameterized tests will not be
    // instantiated but there will be no warning.
    ASSERT(!files.empty());
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
                        ::testing::Values("front-envoy.yaml", "google_com_proxy.json",
                                          "google_com_proxy.yaml", "google_com_proxy.v2.yaml",
                                          "s2s-grpc-envoy.yaml", "service-envoy.yaml"));

// Just make sure that all configs can be injested without a crash. Processing of config files
// may not be successful, but there should be no crash.
TEST_P(ValidationServerTest_1, RunWithoutCrash) {
  validateConfig(options_, Network::Address::InstanceConstSharedPtr(), component_factory_);
  SUCCEED();
}

INSTANTIATE_TEST_CASE_P(AllConfigs, ValidationServerTest_1,
                        ::testing::ValuesIn(ValidationServerTest_1::GetAllConfigFiles()));

} // namespace Server
} // namespace Envoy
