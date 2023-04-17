#include "test/integration/http_protocol_integration.h"

namespace Envoy {

using DownstreamUhvIntegrationTest = HttpProtocolIntegrationTest;
INSTANTIATE_TEST_SUITE_P(Protocols, DownstreamUhvIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP1})),
                         HttpProtocolIntegrationTest::protocolTestParamsToString);

// Without the `uhv_translate_backslash_to_slash` override UHV rejects requests with backslash in
// the path.
TEST_P(DownstreamUhvIntegrationTest, BackslashInUriPathConversionWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.uhv_translate_backslash_to_slash",
                                    "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path\\with%5cback%5Cslashes"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
#ifdef ENVOY_ENABLE_UHV
  // By default Envoy disconnects connection on protocol errors
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ != Http::CodecType::HTTP2) {
    ASSERT_TRUE(response->complete());
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    ASSERT_TRUE(response->reset());
    EXPECT_EQ(Http::StreamResetReason::ConnectionTermination, response->resetReason());
  }
#else
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5cback%5Cslashes");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
#endif
}

// By default the `uhv_translate_backslash_to_slash` == true and UHV behaves just like legacy path
// normalization.
TEST_P(DownstreamUhvIntegrationTest, BackslashInUriPathConversion) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path\\with%5cback%5Cslashes"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

#ifdef ENVOY_ENABLE_UHV
  // UHV by default changes all % encoded sequences to uppercase
  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5Cback%5Cslashes");
#else
  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5cback%5Cslashes");
#endif

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

} // namespace Envoy
