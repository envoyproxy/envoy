#include <string>

#include "common/http/message_impl.h"

#include "extensions/filters/http/well_known_names.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gtest/gtest.h"

using Envoy::Http::HeaderValueOf;

namespace Envoy {
namespace {

// Tests a downstream HTTP2 client sending gRPC requests that are converted into HTTP/1.1 for a
// HTTP1 upstream.
class ReverseBridgeIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  ReverseBridgeIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {}

  void SetUp() override {
    setUpstreamProtocol(FakeHttpConnection::Type::HTTP1);

    const std::string filter =
        R"EOF(
name: grpc_http1_reverse_bridge
typed_config:
  "@type": type.googleapis.com/envoy.config.filter.http.grpc_http1_reverse_bridge.v2alpha1.FilterConfig
  content_type: application/x-protobuf
  withhold_grpc_frames: true
            )EOF";
    config_helper_.addFilter(filter);

    HttpIntegrationTest::initialize();
  }

  void TearDown() override {
    test_server_.reset();
    fake_upstream_connection_.reset();
    fake_upstreams_.clear();
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ReverseBridgeIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that we don't do anything with the request when it's hitting a route that
// doesn't enable the bridge.
TEST_P(ReverseBridgeIntegrationTest, EnabledRoute) {
  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/testing.ExampleService/Print"},
                                                  {"content-type", "application/grpc"}});
  auto encoder_decoder = codec_client_->startRequest(request_headers);
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);

  Buffer::OwnedImpl request_data{"abcdef"};
  codec_client_->sendData(*request_encoder_, request_data, true);

  // Wait for upstream to finish the request.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Ensure that we stripped the length prefix and set the appropriate headers.
  EXPECT_EQ("f", upstream_request_->body().toString());

  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::Headers::get().Accept, "application/x-protobuf"));

  // Respond to the request.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/x-protobuf");
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data{"defgh"};
  upstream_request_->encodeData(response_data, false);

  Http::TestResponseTrailerMapImpl response_trailers;
  response_trailers.setGrpcStatus(std::string("0"));
  upstream_request_->encodeTrailers(response_trailers);

  response->waitForEndStream();
  EXPECT_TRUE(response->complete());

  // Ensure that we restored the content-type and that we added the length prefix.
  EXPECT_EQ(response_data.length() + 5, response->body().size());
  EXPECT_TRUE(absl::EndsWith(response->body(), response_data.toString()));

  // Comparing strings embedded zero literals is hard. Use string literal and std::equal to avoid
  // truncating the string when it's converted to const char *.
  using namespace std::string_literals;

  const auto expected_prefix = "\0\0\0\0\4"s;
  EXPECT_TRUE(
      std::equal(response->body().begin(), response->body().begin() + 4, expected_prefix.begin()));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(*response->trailers(), HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

} // namespace
} // namespace Envoy
