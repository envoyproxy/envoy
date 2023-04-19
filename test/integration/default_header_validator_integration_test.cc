#include "test/integration/http_protocol_integration.h"

namespace Envoy {

using DownstreamUhvIntegrationTest = HttpProtocolIntegrationTest;
INSTANTIATE_TEST_SUITE_P(Protocols, DownstreamUhvIntegrationTest,
                         testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
                             {Http::CodecType::HTTP1, Http::CodecType::HTTP2,
                              Http::CodecType::HTTP3},
                             {Http::CodecType::HTTP2})),
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
                                     {":path", "/path\\with%5Cback%5Cslashes"},
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

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5Cback%5Cslashes");

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
                                     {":path", "/path\\with%5Cback%5Cslashes"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%5Cback%5Cslashes");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// By default the `uhv_preserve_url_encoded_case` == true and UHV behaves just like legacy path
// normalization.
TEST_P(DownstreamUhvIntegrationTest, UrlEncodedTripletsCasePreserved) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with%3bmixed%5Ccase%Fesequences"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3bmixed%5Ccase%Fesequences");

  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}

// Without the `uhv_preserve_url_encoded_case` override UHV changes all percent encoded
// sequences to use uppercase characters.
TEST_P(DownstreamUhvIntegrationTest, UrlEncodedTripletsCasePreservedWithUhvOverride) {
  config_helper_.addRuntimeOverride("envoy.reloadable_features.uhv_preserve_url_encoded_case",
                                    "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with%3bmixed%5Ccase%Fesequences"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  waitForNextUpstreamRequest();

#ifdef ENVOY_ENABLE_UHV
  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3Bmixed%5Ccase%FEsequences");
#else
  EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with%3bmixed%5Ccase%Fesequences");
#endif
  // Send a headers only response.
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(response->waitForEndStream());
}
// By default the `uhv_allow_extended_ascii_in_path_for_http2` == true and UHV behaves just like
// legacy path normalization for H/2.
TEST_P(DownstreamUhvIntegrationTest, ExtendedAsciiUriPathAllowedInHttp2) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with/\xA7_extended/\xFE_ascii"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  if (downstream_protocol_ != Http::CodecType::HTTP2) {
    // All codecs, except H/2 reject unencoded extended ASCII in URL path
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      EXPECT_EQ("400", response->headers().getStatusValue());
    } else {
      EXPECT_TRUE(response->reset());
    }
  } else {
    waitForNextUpstreamRequest();
    EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with/%A7_extended/%FE_ascii");

    // Send a headers only response.
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
  }
}

// Envoy does not URL encode extended ASCII in query by default in HTTP/2.
TEST_P(DownstreamUhvIntegrationTest, ExtendedAsciiInUriQueryAllowedInHttp2) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with/extended/ascii?in=que\xB6ry"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  if (downstream_protocol_ != Http::CodecType::HTTP2) {
    // All codecs, except H/2 reject unencoded extended ASCII in URL query
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      EXPECT_EQ("400", response->headers().getStatusValue());
    } else {
      EXPECT_TRUE(response->reset());
    }
  } else {
    waitForNextUpstreamRequest();
    // Envoy does not URL encode extended ASCII in HTTP/2 URL query by default.
    EXPECT_EQ(upstream_request_->headers().getPathValue(),
              "/path/with/extended/ascii?in=que\xB6ry");

    // Send a headers only response.
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
  }
}

TEST_P(DownstreamUhvIntegrationTest, ControlAsciiInUriPathRejected) {
  // Prevent test client codec from rejecting invalid request headers.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with/con\x03trol/asc\x16ii"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    EXPECT_TRUE(response->reset());
  }
}

TEST_P(DownstreamUhvIntegrationTest, ControlAsciiInUriQueryRejected) {
  // Prevent test client codec from rejecting invalid request headers.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with?con\x01trol=asc\x1Fii"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    EXPECT_TRUE(response->reset());
  }
}

TEST_P(DownstreamUhvIntegrationTest, DelInUriPathRejected) {
  // Prevent test client codec from rejecting invalid request headers.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with/del\x7F/ascii"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    EXPECT_TRUE(response->reset());
  }
}

TEST_P(DownstreamUhvIntegrationTest, DelInUriQueryRejected) {
  // Prevent test client codec from rejecting invalid request headers.
  config_helper_.addRuntimeOverride("envoy.reloadable_features.validate_upstream_headers", "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with?de\x7Fl=ascii"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    EXPECT_TRUE(response->reset());
  }
}

// Without the `uhv_allow_extended_ascii_in_path_for_http2` override UHV rejects extended ASCII
// for all codecs.
TEST_P(DownstreamUhvIntegrationTest, ExtendedAsciiUriPathRejectedWithOverride) {
  config_helper_.addRuntimeOverride(
      "envoy.reloadable_features.uhv_allow_extended_ascii_in_path_for_http2", "false");
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) -> void { hcm.mutable_normalize_path()->set_value(true); });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  // Start the request.
  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/path/with/\xA7_extended/\xFE_ascii"},
                                     {":scheme", "http"},
                                     {":authority", "host"}});
#ifdef ENVOY_ENABLE_UHV
  ASSERT_TRUE(codec_client_->waitForDisconnect());
  if (downstream_protocol_ == Http::CodecType::HTTP1) {
    EXPECT_EQ("400", response->headers().getStatusValue());
  } else {
    EXPECT_TRUE(response->reset());
  }
#else
  if (downstream_protocol_ != Http::CodecType::HTTP2) {
    // All codecs, except H/2 reject unencoded extended ASCII in URL path
    ASSERT_TRUE(codec_client_->waitForDisconnect());
    if (downstream_protocol_ == Http::CodecType::HTTP1) {
      EXPECT_EQ("400", response->headers().getStatusValue());
    } else {
      EXPECT_TRUE(response->reset());
    }
  } else {
    waitForNextUpstreamRequest();
    EXPECT_EQ(upstream_request_->headers().getPathValue(), "/path/with/%A7_extended/%FE_ascii");

    // Send a headers only response.
    upstream_request_->encodeHeaders(default_response_headers_, true);
    ASSERT_TRUE(response->waitForEndStream());
  }
#endif
}

} // namespace Envoy
