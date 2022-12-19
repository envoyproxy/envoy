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

  void initialize() override {
    config_helper_.addRuntimeOverride("envoy.disallow_global_stats", "true");
    BaseClientIntegrationTest::initialize();
  }

  void TearDown() override {
    cleanup();
    BaseClientIntegrationTest::TearDown();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, MultiEnvoyTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Sets up 20 engines, tests data plane access for the first one.
TEST_P(MultiEnvoyTest, Basic) {
  num_engines_for_test_ = 20;
  initialize();

  Buffer::OwnedImpl request_data = Buffer::OwnedImpl("request body");
  default_request_headers_.addCopy(AutonomousStream::EXPECT_REQUEST_SIZE_BYTES,
                                   std::to_string(request_data.length()));
  for (int i = 0; i<num_engines_for_test_; i++){
    multi_stream_prototypes_[i]->setOnData([=](envoy_data c_data, bool end_stream) {
      if (end_stream) {
        EXPECT_EQ(Data::Utility::copyToString(c_data), "");
      } else {
        EXPECT_EQ(c_data.length, 10);
      }
      multi_cc_[i]->on_data_calls++;
      release_envoy_data(c_data);
    });

    multi_streams_[i]->sendHeaders(envoyToMobileHeaders(default_request_headers_), false);

    envoy_data c_data = Data::Utility::toBridgeData(request_data);
    multi_streams_[i]->sendData(c_data);

    Platform::RequestTrailersBuilder builder;
    std::shared_ptr<Platform::RequestTrailers> trailers =
        std::make_shared<Platform::RequestTrailers>(builder.build());
    multi_streams_[i]->close(trailers);

    multi_terminal_callbacks_[i]->waitReady();

    ASSERT_EQ(multi_cc_[i]->on_headers_calls, 1);
    ASSERT_EQ(multi_cc_[i]->status, "200");
    ASSERT_EQ(multi_cc_[i]->on_data_calls, 2);
    ASSERT_EQ(multi_cc_[i]->on_complete_calls, 1);
    ASSERT_EQ(multi_cc_[i]->on_header_consumed_bytes_from_response, 27);
    ASSERT_EQ(multi_cc_[i]->on_complete_received_byte_count, 67);
    // Request is freed by the engine and must be recreated
    request_data = Buffer::OwnedImpl("request body");
  }
}

} // namespace
} // namespace Envoy
