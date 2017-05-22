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
  static void SetUpTestCase() {
    TestEnvironment::exec(
        {TestEnvironment::runfilesPath("test/config_test/example_configs_test_setup.sh")});
    directory_ = TestEnvironment::temporaryDirectory() + "/test/config_test/";
  }

protected:
  ValidationServerTest() : options_(directory_ + GetParam()) {}

  static std::string directory_;

  testing::NiceMock<MockOptions> options_;
  TestComponentFactory component_factory_;
  testing::NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

std::string ValidationServerTest::directory_ = "";

TEST_P(ValidationServerTest, Validate) {
  EXPECT_EQ(0, validateConfig(options_, component_factory_, local_info_));
}

// TODO(rlazarus): We'd like use this setup to replace //test/config_test (that is, run it against
// all the example configs) but can't until light validation is implemented, mocking out access to
// the filesystem for TLS certs, etc. In the meantime, these are the example configs that work
// as-is.
INSTANTIATE_TEST_CASE_P(ValidConfigs, ValidationServerTest,
                        ::testing::Values("front-envoy.json", "google_com_proxy.json",
                                          "s2s-grpc-envoy.json", "service-envoy.json"));

} // Server
} // Envoy
