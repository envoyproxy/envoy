#include <string>

#include "envoy/extensions/filters/http/grpc_http1_reverse_bridge/v3/config.pb.h"

#include "source/common/http/message_impl.h"

#include "test/integration/http_integration.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "fmt/printf.h"
#include "gtest/gtest.h"

using Envoy::Http::HeaderValueOf;

// for ::operator""s (which Windows compiler does not support):
using namespace std::string_literals;

namespace Envoy {
namespace {

// Tests a downstream HTTP2 client sending gRPC requests that are converted into HTTP/1.1 for a
// HTTP1 upstream.
class ReverseBridgeIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                     public HttpIntegrationTest {
public:
  ReverseBridgeIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  void initialize() override { initialize(absl::nullopt); }

  void initialize(const absl::optional<std::string> response_size_header) {
    setUpstreamProtocol(Http::CodecType::HTTP2);

    const std::string filter = fmt::format(
        R"EOF(
name: grpc_http1_reverse_bridge
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.grpc_http1_reverse_bridge.v3.FilterConfig
  content_type: application/x-protobuf
  withhold_grpc_frames: true
  response_size_header: "{}"
            )EOF",
        response_size_header ? *response_size_header : "");
    config_helper_.prependFilter(filter);

    auto vhost = config_helper_.createVirtualHost("disabled");
    envoy::extensions::filters::http::grpc_http1_reverse_bridge::v3::FilterConfigPerRoute
        route_config;
    route_config.set_disabled(true);
    (*vhost.mutable_routes(0)->mutable_typed_per_filter_config())["grpc_http1_reverse_bridge"]
        .PackFrom(route_config);
    config_helper_.addVirtualHost(vhost);

    HttpIntegrationTest::initialize();
  }

protected:
  Http::CodecType upstream_protocol_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ReverseBridgeIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Verifies that we don't do anything with the request when it's hitting a route that
// doesn't enable the bridge.
// Regression test of https://github.com/envoyproxy/envoy/issues/9922
TEST_P(ReverseBridgeIntegrationTest, DisabledRoute) {
  upstream_protocol_ = Http::CodecType::HTTP2;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "disabled"},
                                                  {":path", "/testing.ExampleService/Print"},
                                                  {"content-type", "application/grpc"}});
  auto response = codec_client_->makeRequestWithBody(request_headers, "abcdef");

  // Wait for upstream to finish the request.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Ensure that we don't do anything
  EXPECT_EQ("abcdef", upstream_request_->body().toString());
  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));

  // Respond to the request.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/grpc");
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data{"defgh"};
  upstream_request_->encodeData(response_data, false);

  Http::TestResponseTrailerMapImpl response_trailers;
  response_trailers.setGrpcStatus(std::string("0"));
  upstream_request_->encodeTrailers(response_trailers);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  EXPECT_EQ(response->body(), response_data.toString());
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(*response->trailers(), HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(ReverseBridgeIntegrationTest, EnabledRoute) {
  upstream_protocol_ = Http::CodecType::HTTP1;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/testing.ExampleService/Print"},
                                                  {"content-type", "application/grpc"}});

  auto response = codec_client_->makeRequestWithBody(request_headers, "abcdef");

  // Wait for upstream to finish the request.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Ensure that we stripped the length prefix and set the appropriate headers.
  EXPECT_EQ("f", upstream_request_->body().toString());

  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));

  // Respond to the request.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/x-protobuf");
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data{"defgh"};
  upstream_request_->encodeData(response_data, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // Ensure that we restored the content-type and that we added the length prefix.
  EXPECT_EQ(response_data.length() + 5, response->body().size());
  EXPECT_TRUE(absl::EndsWith(response->body(), response_data.toString()));

  // Comparing strings embedded zero literals is hard. Use string literal and std::equal to avoid
  // truncating the string when it's converted to const char *. Hex value 0x5 is 5, the message
  // length.
  const auto expected_prefix = "\0\0\0\0\x5"s;
  EXPECT_TRUE(
      std::equal(response->body().begin(), response->body().begin() + 5, expected_prefix.begin()));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(*response->trailers(), HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

TEST_P(ReverseBridgeIntegrationTest, EnabledRouteBadContentType) {
  upstream_protocol_ = Http::CodecType::HTTP1;
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/testing.ExampleService/Print"},
                                                  {"content-type", "application/grpc"}});

  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/x-not-protobuf");

  auto response = sendRequestAndWaitForResponse(request_headers, 5, response_headers, 5);

  EXPECT_TRUE(response->complete());

  // The response should indicate an error.
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(response->headers(), HeaderValueOf(Http::Headers::get().GrpcStatus, "2"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

// Verifies that we stream the response instead of buffering it, using an upstream-provided header
// to get the overall message length.
TEST_P(ReverseBridgeIntegrationTest, EnabledRouteStreamResponse) {
  upstream_protocol_ = FakeHttpConnection::Type::HTTP1;

  initialize(absl::make_optional("custom-response-size-header"));

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/testing.ExampleService/Print"},
                                                  {"content-type", "application/grpc"}});

  auto response = codec_client_->makeRequestWithBody(request_headers, "abcdef");

  // Wait for upstream to finish the request.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Ensure that we stripped the length prefix and set the appropriate headers.
  EXPECT_EQ("f", upstream_request_->body().toString());

  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));

  // Respond to the request.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/x-protobuf");
  const uint32_t upstream_response_size = 10;
  response_headers.addCopy(Http::LowerCaseString("custom-response-size-header"),
                           std::to_string(upstream_response_size));
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data_begin{"hello"};
  upstream_request_->encodeData(response_data_begin, false);
  // The downstream should be able to read this first piece of data before the stream ends.
  response->waitForBodyData(5);

  Buffer::OwnedImpl response_data_end{"world"};
  upstream_request_->encodeData(response_data_end, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());
  response_data_begin.add(response_data_end);

  // Ensure that we restored the content-type and that we added the length prefix.
  EXPECT_EQ(upstream_response_size + 5, response->body().size());
  EXPECT_TRUE(absl::EndsWith(response->body(), response_data_begin.toString()));

  // Comparing strings embedded zero literals is hard. Use string literal and std::equal to avoid
  // truncating the string when it's converted to const char *. Hex value 0xA is 10, the message
  // length.
  const auto expected_prefix = "\0\0\0\0\xA"s;
  EXPECT_TRUE(
      std::equal(response->body().begin(), response->body().begin() + 5, expected_prefix.begin()));
  EXPECT_THAT(response->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/grpc"));
  EXPECT_THAT(*response->trailers(), HeaderValueOf(Http::Headers::get().GrpcStatus, "0"));

  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
}

// Verifies that we can stream the response and proxy a reset.
TEST_P(ReverseBridgeIntegrationTest, EnabledRouteStreamWithholdResponse) {
  upstream_protocol_ = FakeHttpConnection::Type::HTTP1;

  initialize(absl::make_optional("custom-response-size-header"));

  codec_client_ = makeHttpConnection(lookupPort("http"));

  Http::TestRequestHeaderMapImpl request_headers({{":scheme", "http"},
                                                  {":method", "POST"},
                                                  {":authority", "foo"},
                                                  {":path", "/testing.ExampleService/Print"},
                                                  {"content-type", "application/grpc"}});

  auto response = codec_client_->makeRequestWithBody(request_headers, "abcdef");

  // Wait for upstream to finish the request.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Ensure that we stripped the length prefix and set the appropriate headers.
  EXPECT_EQ("f", upstream_request_->body().toString());

  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::Headers::get().ContentType, "application/x-protobuf"));
  EXPECT_THAT(upstream_request_->headers(),
              HeaderValueOf(Http::CustomHeaders::get().Accept, "application/x-protobuf"));

  // Respond to the request.
  Http::TestResponseHeaderMapImpl response_headers;
  response_headers.setStatus(200);
  response_headers.setContentType("application/x-protobuf");
  const uint32_t upstream_response_size = 10;
  response_headers.addCopy(Http::LowerCaseString("custom-response-size-header"),
                           std::to_string(upstream_response_size));
  upstream_request_->encodeHeaders(response_headers, false);

  Buffer::OwnedImpl response_data{"helloworld"};
  upstream_request_->encodeData(response_data, false);
  response->waitForBodyData(10);

  // If the upstream sends the full payload and also a reset, it should result in a reset rather
  // than stream complete.
  upstream_request_->encodeResetStream();
  ASSERT_TRUE(response->waitForReset());
}
} // namespace
} // namespace Envoy
