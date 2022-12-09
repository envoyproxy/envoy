#include "source/extensions/http/header_formatters/preserve_case/preserve_case_formatter.h"

#include "test/common/integration/base_client_integration_test.h"
#include "test/integration/autonomous_upstream.h"

#include "library/common/data/utility.h"
#include "library/common/main_interface.h"
#include "library/common/network/proxy_settings.h"
#include "library/common/types/c_types.h"

#include <chrono>
#include <thread>


using testing::ReturnRef;

// AN EXACT COPY OF client_integration_test.cc with all tests removed except the first

namespace Envoy {
namespace {

class MultiEnvoyTest : public BaseClientIntegrationTest,
                              public testing::TestWithParam<Network::Address::IpVersion> {
public:
  MultiEnvoyTest() : BaseClientIntegrationTest(/*ip_version=*/GetParam()) {}

  void SetUp() override {
    setUpstreamCount(config_helper_.bootstrap().static_resources().clusters_size());
    // TODO(abeyad): Add paramaterized tests for HTTP1, HTTP2, and HTTP3.
    setUpstreamProtocol(Http::CodecType::HTTP1);
  }

  void TearDown() override {
    cleanup();
    BaseClientIntegrationTest::TearDown();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiEnvoyTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(MultiEnvoyTest, Basic) {
  num_engines_for_test_ = 1;
  initialize();
}

} // namespace
} // namespace Envoy
