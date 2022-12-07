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
  num_engines_for_test_ = 2; // passes with two.  fails with 3??
  initialize();

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));

  stream_prototype_->setOnData([this](envoy_data c_data, bool end_stream) {
    if (end_stream) {
      EXPECT_EQ(Data::Utility::copyToString(c_data), "");
    } else {
      EXPECT_EQ(c_data.length, 10);
    }
    cc_.on_data_calls++;
    release_envoy_data(c_data);
  });

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), false);

  envoy_data c_data = Data::Utility::toBridgeData(request_data);
  stream_->sendData(c_data);

  Platform::RequestTrailersBuilder builder;
  std::shared_ptr<Platform::RequestTrailers> trailers =
      std::make_shared<Platform::RequestTrailers>(builder.build());
  stream_->close(trailers);

  terminal_callback_.waitReady();

  ASSERT_EQ(cc_.on_headers_calls, 1);
  ASSERT_EQ(cc_.status, "200");
  ASSERT_EQ(cc_.on_data_calls, 2);
  ASSERT_EQ(cc_.on_complete_calls, 1);
  ASSERT_EQ(cc_.on_header_consumed_bytes_from_response, 27);
  ASSERT_EQ(cc_.on_complete_received_byte_count, 67);
}

} // namespace
} // namespace Envoy
