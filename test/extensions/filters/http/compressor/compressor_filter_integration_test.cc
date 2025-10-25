#include "envoy/event/timer.h"
#include "envoy/extensions/compression/brotli/compressor/v3/brotli.pb.h"
#include "envoy/extensions/compression/gzip/compressor/v3/gzip.pb.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/compression/brotli/decompressor/brotli_decompressor_impl.h"
#include "source/extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

using Envoy::Protobuf::Any;
using Envoy::Protobuf::MapPair;

class CompressorIntegrationTest
    : public testing::TestWithParam<std::tuple<Network::Address::IpVersion, bool>>,
      public Event::SimulatedTimeSystem,
      public HttpIntegrationTest {
public:
  CompressorIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, std::get<0>(GetParam())) {}

  void SetUp() override { decompressor_.init(window_bits); }
  void TearDown() override { cleanupUpstreamAndDownstream(); }

  void initializeFilter(const std::string& config) {
    config_helper_.prependFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  void initializeDefaultFilter() {
    const std::string& filter_to_initialize =
        std::get<1>(GetParam()) ? default_config_with_status_header : default_config;
    initializeFilter(filter_to_initialize);
  }

  void initializeFullFilter() {
    const std::string& filter_to_initialize =
        std::get<1>(GetParam()) ? full_config_with_status_header : full_config;
    initializeFilter(filter_to_initialize);
  }

  void doCompressedRequest(Http::TestRequestHeaderMapImpl&& request_headers,
                           Http::TestResponseHeaderMapImpl&& response_headers) {
    uint64_t response_content_length;
    uint64_t request_content_length;
    ASSERT_TRUE(absl::SimpleAtoi(request_headers.get_("content-length"), &request_content_length));
    ASSERT_TRUE(
        absl::SimpleAtoi(response_headers.get_("content-length"), &response_content_length));
    const Buffer::OwnedImpl expected_request{std::string(request_content_length, 'a')};
    auto response = sendRequestAndWaitForResponse(request_headers, request_content_length,
                                                  response_headers, response_content_length);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(Http::CustomHeaders::get().ContentEncodingValues.Gzip,
              upstream_request_->headers()
                  .get(Http::CustomHeaders::get().ContentEncoding)[0]
                  ->value()
                  .getStringView());
    EXPECT_EQ(Http::Headers::get().TransferEncodingValues.Chunked,
              upstream_request_->headers().getTransferEncodingValue());
    EXPECT_GT(upstream_request_->bodyLength(), 0U);
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
    ASSERT_EQ(response_content_length, response->body().size());
    EXPECT_EQ(response->body(), std::string(response_content_length, 'a'));

    Buffer::OwnedImpl decompressed_request{};
    const Buffer::OwnedImpl compressed_request{upstream_request_->body()};
    decompressor_.decompress(compressed_request, decompressed_request);
    ASSERT_EQ(request_content_length, decompressed_request.length());
    EXPECT_TRUE(TestUtility::buffersEqual(expected_request, decompressed_request));
  }

  void doRequestAndCompression(Http::TestRequestHeaderMapImpl&& request_headers,
                               Http::TestResponseHeaderMapImpl&& response_headers) {
    uint64_t content_length;
    ASSERT_TRUE(absl::SimpleAtoi(response_headers.get_("content-length"), &content_length));
    const Buffer::OwnedImpl expected_response{std::string(content_length, 'a')};
    auto response =
        sendRequestAndWaitForResponse(request_headers, 0, response_headers, content_length);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ("200", response->headers().getStatusValue());
    Http::HeaderMap::GetResult content_encoding =
        response->headers().get(Http::CustomHeaders::get().ContentEncoding);
    ASSERT_FALSE(content_encoding.empty());
    EXPECT_EQ(Http::CustomHeaders::get().ContentEncodingValues.Gzip,
              content_encoding[0]->value().getStringView());
    EXPECT_EQ(Http::Headers::get().TransferEncodingValues.Chunked,
              response->headers().getTransferEncodingValue());

    Buffer::OwnedImpl decompressed_response{};
    const Buffer::OwnedImpl compressed_response{response->body()};
    decompressor_.decompress(compressed_response, decompressed_response);
    ASSERT_EQ(content_length, decompressed_response.length());
    EXPECT_TRUE(TestUtility::buffersEqual(expected_response, decompressed_response));
  }

  void doRequestAndNoCompression(Http::TestRequestHeaderMapImpl&& request_headers,
                                 Http::TestResponseHeaderMapImpl&& response_headers) {
    uint64_t content_length;
    ASSERT_TRUE(absl::SimpleAtoi(response_headers.get_("content-length"), &content_length));
    auto response =
        sendRequestAndWaitForResponse(request_headers, 0, response_headers, content_length);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    EXPECT_TRUE(response->complete());
    EXPECT_EQ(response_headers.getStatusValue(), response->headers().getStatusValue());
    ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
    ASSERT_EQ(content_length, response->body().size());
    EXPECT_EQ(response->body(), std::string(content_length, 'a'));
  }

  const std::string full_config{R"EOF(
      name: compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        response_direction_config:
          disable_on_etag_header: true
          remove_accept_encoding_header: false
          common_config:
            enabled:
              default_value: true
              runtime_key: foo_key
            min_content_length: 100
            content_type:
              - text/html
              - application/json
          uncompressible_response_codes:
            - 206
        request_direction_config:
          common_config:
            enabled:
              default_value: true
              runtime_key: enable_requests
            min_content_length: 100
            content_type:
              - text/html
              - application/json
        compressor_library:
          name: testlib
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
            memory_level: 3
            window_bits: 10
            compression_level: best_compression
            compression_strategy: rle
    )EOF"};

  const std::string full_config_with_status_header{R"EOF(
      name: compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        response_direction_config:
          disable_on_etag_header: true
          remove_accept_encoding_header: false
          status_header_enabled: true
          common_config:
            enabled:
              default_value: true
              runtime_key: foo_key
            min_content_length: 100
            content_type:
              - text/html
              - application/json
          uncompressible_response_codes:
            - 206
        request_direction_config:
          common_config:
            enabled:
              default_value: true
              runtime_key: enable_requests
            min_content_length: 100
            content_type:
              - text/html
              - application/json
        compressor_library:
          name: testlib
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
            memory_level: 3
            window_bits: 10
            compression_level: best_compression
            compression_strategy: rle
    )EOF"};

  const std::string default_config{R"EOF(
      name: envoy.filters.http.compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        compressor_library:
          name: testlib
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
    )EOF"};

  const std::string default_config_with_status_header{R"EOF(
      name: envoy.filters.http.compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        response_direction_config:
          status_header_enabled: true
        compressor_library:
          name: testlib
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
    )EOF"};

  const uint64_t window_bits{15 | 16};

  Stats::IsolatedStoreImpl stats_store_;
  Extensions::Compression::Gzip::Decompressor::ZlibDecompressorImpl decompressor_{
      *stats_store_.rootScope(), "test", 4096, 100};
};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, CompressorIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()), testing::Bool()),
    [](const testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
      return fmt::format("{}_{}", TestUtility::ipVersionToString(std::get<0>(params.param)),
                         std::get<1>(params.param) ? "WithStatusHeader" : "NoStatusHeader");
    });

/**
 * Exercises gzip compression with default configuration.
 */
TEST_P(CompressorIntegrationTest, AcceptanceDefaultConfigTest) {
  initializeDefaultFilter();
  doRequestAndCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                         {":path", "/test/long/url"},
                                                         {":scheme", "http"},
                                                         {":authority", "host"},
                                                         {"accept-encoding", "deflate, gzip"}},
                          Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                          {"content-length", "4400"},
                                                          {"content-type", "text/xml"}});
}

/**
 * Exercises gzip compression with full configuration.
 */
TEST_P(CompressorIntegrationTest, AcceptanceFullConfigTest) {
  initializeDefaultFilter();
  doRequestAndCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                         {":path", "/test/long/url"},
                                                         {":scheme", "http"},
                                                         {":authority", "host"},
                                                         {"accept-encoding", "deflate, gzip"}},
                          Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                          {"content-length", "4400"},
                                                          {"content-type", "application/json"}});
}

/**
 * Exercises filter when client request contains 'identity' type.
 */
TEST_P(CompressorIntegrationTest, IdentityAcceptEncoding) {
  initializeDefaultFilter();
  doRequestAndNoCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"accept-encoding", "identity"}},
                            Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                            {"content-length", "128"},
                                                            {"content-type", "text/plain"}});
}

/**
 * Exercises filter when client request contains unsupported 'accept-encoding' type.
 */
TEST_P(CompressorIntegrationTest, NotSupportedAcceptEncoding) {
  initializeDefaultFilter();
  doRequestAndNoCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"accept-encoding", "deflate, br"}},
                            Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                            {"content-length", "128"},
                                                            {"content-type", "text/plain"}});
}

/**
 * Exercises filter when upstream response is already encoded.
 */
TEST_P(CompressorIntegrationTest, UpstreamResponseAlreadyEncoded) {
  initializeDefaultFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-encoding", "br"},
                                                   {"content-length", "128"},
                                                   {"content-type", "application/json"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 128);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_EQ("br", response->headers()
                      .get(Http::CustomHeaders::get().ContentEncoding)[0]
                      ->value()
                      .getStringView());
  EXPECT_EQ(128U, response->body().size());
}

/**
 * Exercises filter when upstream responds with content length below the default threshold.
 */
TEST_P(CompressorIntegrationTest, NotEnoughContentLength) {
  initializeDefaultFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "10"}, {"content-type", "application/json"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 10);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
  EXPECT_EQ(10U, response->body().size());
}

/**
 * Exercises filter when response from upstream service is empty.
 */
TEST_P(CompressorIntegrationTest, EmptyResponse) {
  initializeDefaultFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "204"}, {"content-length", "0"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("204", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
  EXPECT_EQ(0U, response->body().size());
}

/**
 * Exercises filter when upstream responds with restricted content-type value.
 */
TEST_P(CompressorIntegrationTest, SkipOnContentType) {
  initializeFullFilter();
  doRequestAndNoCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"accept-encoding", "deflate, gzip"}},
                            Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                            {"content-length", "128"},
                                                            {"content-type", "application/xml"}});
}

/**
 * Exercises filter when upstream responds with restricted response code value.
 */
TEST_P(CompressorIntegrationTest, SkipOnUncompressibleResponseCode) {
  initializeFullFilter();
  doRequestAndNoCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"accept-encoding", "deflate, gzip"},
                                                           {"range", "bytes=100-227"}},
                            Http::TestResponseHeaderMapImpl{{":status", "206"},
                                                            {"content-length", "128"},
                                                            {"content-range", "bytes=100-227/567"},
                                                            {"content-type", "application/xml"}});
}

/**
 * Exercises filter when upstream responds with restricted cache-control value.
 */
TEST_P(CompressorIntegrationTest, SkipOnCacheControl) {
  initializeFullFilter();
  doRequestAndNoCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/test/long/url"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"accept-encoding", "deflate, gzip"}},
                            Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                            {"content-length", "128"},
                                                            {"cache-control", "no-transform"},
                                                            {"content-type", "application/json"}});
}

/**
 * Exercises gzip compression when upstream returns a chunked response.
 */
TEST_P(CompressorIntegrationTest, AcceptanceFullConfigChunkedResponse) {
  initializeFullFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};

  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-type", "application/json"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 1024);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_EQ("gzip", response->headers()
                        .get(Http::CustomHeaders::get().ContentEncoding)[0]
                        ->value()
                        .getStringView());
  ASSERT_EQ("chunked", response->headers().getTransferEncodingValue());
}

/**
 * Verify Vary header values are preserved.
 */
TEST_P(CompressorIntegrationTest, AcceptanceFullConfigVaryHeader) {
  initializeDefaultFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"content-type", "application/json"}, {"vary", "Cookie"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 1024);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_EQ("gzip", response->headers()
                        .get(Http::CustomHeaders::get().ContentEncoding)[0]
                        ->value()
                        .getStringView());
  ASSERT_EQ("Cookie, Accept-Encoding",
            response->headers().get(Http::CustomHeaders::get().Vary)[0]->value().getStringView());
}

/**
 * Exercises gzip request compression with full configuration.
 */
TEST_P(CompressorIntegrationTest, CompressedRequestAcceptanceFullConfigTest) {
  initializeFullFilter();
  doCompressedRequest(Http::TestRequestHeaderMapImpl{{":method", "PUT"},
                                                     {":path", "/test/long/url"},
                                                     {":scheme", "http"},
                                                     {":authority", "host"},
                                                     {"content-length", "256"}},
                      Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                      {"content-length", "10"},
                                                      {"content-type", "application/json"}});
}

// Enable filter, then disable per-route.
TEST_P(CompressorIntegrationTest, PerRouteDisable) {
  const bool is_add_status_header = std::get<1>(GetParam());
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cm) {
        auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
        auto* route = vh->mutable_routes()->Mutable(0);
        route->mutable_match()->set_path("/nocompress");
        envoy::extensions::filters::http::compressor::v3::CompressorPerRoute per_route;
        per_route.set_disabled(true);
        Any cfg_any;
        ASSERT_TRUE(cfg_any.PackFrom(per_route));
        route->mutable_typed_per_filter_config()->insert(
            MapPair<std::string, Any>("envoy.filters.http.compressor", cfg_any));
      });

  if (is_add_status_header) {
    initializeFilter(R"EOF(
        name: envoy.filters.http.compressor
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
          compressor_library:
            name: testlib
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
          response_direction_config:
            status_header_enabled: true
            common_config:
              enabled:
                default_value: true
                runtime_key: foo_key
              content_type:
                - text/html
                - application/json
      )EOF");
  } else {
    initializeFilter(R"EOF(
          name: envoy.filters.http.compressor
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
            compressor_library:
              name: testlib
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
            response_direction_config:
              common_config:
                enabled:
                  default_value: true
                  runtime_key: foo_key
                content_type:
                  - text/html
                  - application/json
        )EOF");
  }
  doRequestAndNoCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                           {":path", "/nocompress"},
                                                           {":scheme", "http"},
                                                           {":authority", "host"},
                                                           {"accept-encoding", "deflate, gzip"}},
                            Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                            {"content-length", "40"},
                                                            {"content-type", "text/xml"}});
}

// Disable filter, then enable per-route.
TEST_P(CompressorIntegrationTest, PerRouteEnable) {
  const bool is_add_status_header = std::get<1>(GetParam());
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cm) {
        auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
        auto* route = vh->mutable_routes()->Mutable(0);
        route->mutable_match()->set_path("/compress");
        envoy::extensions::filters::http::compressor::v3::CompressorPerRoute per_route;
        per_route.mutable_overrides()->mutable_response_direction_config();
        Any cfg_any;
        ASSERT_TRUE(cfg_any.PackFrom(per_route));
        route->mutable_typed_per_filter_config()->insert(
            MapPair<std::string, Any>("envoy.filters.http.compressor", cfg_any));
      });

  if (is_add_status_header) {
    initializeFilter(R"EOF(
        name: envoy.filters.http.compressor
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
          compressor_library:
            name: testlib
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
          response_direction_config:
            status_header_enabled: true
            common_config:
              enabled:
                default_value: false
                runtime_key: foo_key
              content_type:
                - text/xml
      )EOF");
  } else {
    initializeFilter(R"EOF(
      name: envoy.filters.http.compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        compressor_library:
          name: testlib
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
        response_direction_config:
          common_config:
            enabled:
              default_value: false
              runtime_key: foo_key
            content_type:
              - text/xml
    )EOF");
  }

  doRequestAndCompression(Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                                         {":path", "/compress"},
                                                         {":scheme", "http"},
                                                         {":authority", "host"},
                                                         {"accept-encoding", "deflate, gzip"}},
                          Http::TestResponseHeaderMapImpl{{":status", "200"},
                                                          {"content-length", "40"},
                                                          {"content-type", "text/xml"}});
}

// Test per-route compressor library override with brotli.
TEST_P(CompressorIntegrationTest, PerRouteCompressorLibraryOverride) {
  const bool is_add_status_header = std::get<1>(GetParam());
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cm) {
        auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
        auto* route = vh->mutable_routes()->Mutable(0);
        route->mutable_match()->set_path("/brotli-per-route");

        envoy::extensions::filters::http::compressor::v3::CompressorPerRoute per_route;
        // Override the compressor library to use brotli instead of gzip.
        auto* compressor_lib = per_route.mutable_overrides()->mutable_compressor_library();
        compressor_lib->set_name("brotli");
        compressor_lib->mutable_typed_config()->set_type_url(
            "type.googleapis.com/envoy.extensions.compression.brotli.compressor.v3.Brotli");
        compressor_lib->mutable_typed_config()->set_value("{}");

        Any cfg_any;
        ASSERT_TRUE(cfg_any.PackFrom(per_route));
        route->mutable_typed_per_filter_config()->insert(
            MapPair<std::string, Any>("envoy.filters.http.compressor", cfg_any));
      });
  if (is_add_status_header) {
    initializeFilter(R"EOF(
        name: envoy.filters.http.compressor
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
          compressor_library:
            name: gzip-default
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
          response_direction_config:
            status_header_enabled: true
            common_config:
              enabled:
                default_value: true
              content_type:
                - text/html
                - application/json
    )EOF");
  } else {
    initializeFilter(R"EOF(
        name: envoy.filters.http.compressor
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
          compressor_library:
            name: gzip-default
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
          response_direction_config:
            common_config:
              enabled:
                default_value: true
              content_type:
                - text/html
                - application/json
    )EOF");
  }

  // Request to /brotli-per-route should use brotli compression (per-route override).
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/brotli-per-route"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept-encoding", "br, gzip"}},
      0,
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-length", "40"}, {"content-type", "text/html"}},
      40);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  // Should be compressed with brotli (not gzip), validating the per-route override works.
  Http::HeaderMap::GetResult content_encoding =
      response->headers().get(Http::CustomHeaders::get().ContentEncoding);
  ASSERT_FALSE(content_encoding.empty());
  EXPECT_EQ("br", content_encoding[0]->value().getStringView());
}

// Test that per-route compressor library config creation works with various libraries.
TEST_P(CompressorIntegrationTest, PerRouteCompressorLibraryConfigCreation) {
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             cm) {
        auto* vh = cm.mutable_route_config()->mutable_virtual_hosts()->Mutable(0);
        auto* route = vh->mutable_routes()->Mutable(0);
        route->mutable_match()->set_path("/custom");

        envoy::extensions::filters::http::compressor::v3::CompressorPerRoute per_route;
        // Test that per-route config can be created with different compressor library.
        auto* compressor_lib = per_route.mutable_overrides()->mutable_compressor_library();
        compressor_lib->set_name("custom");
        compressor_lib->mutable_typed_config()->set_type_url(
            "type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip");

        // Set some config for gzip compressor.
        envoy::extensions::compression::gzip::compressor::v3::Gzip gzip_config;
        gzip_config.mutable_window_bits()->set_value(12);
        std::string serialized_config;
        ASSERT_TRUE(gzip_config.SerializeToString(&serialized_config));
        compressor_lib->mutable_typed_config()->set_value(serialized_config);

        Any cfg_any;
        ASSERT_TRUE(cfg_any.PackFrom(per_route));
        route->mutable_typed_per_filter_config()->insert(
            MapPair<std::string, Any>("envoy.filters.http.compressor", cfg_any));
      });

  initializeDefaultFilter();

  // Make request and verify it works.
  // Both should compress with gzip but different settings.
  auto response = sendRequestAndWaitForResponse(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/custom"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept-encoding", "gzip"}},
      0,
      Http::TestResponseHeaderMapImpl{
          {":status", "200"}, {"content-length", "40"}, {"content-type", "text/html"}},
      40);

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  Http::HeaderMap::GetResult content_encoding =
      response->headers().get(Http::CustomHeaders::get().ContentEncoding);
  ASSERT_FALSE(content_encoding.empty());
  EXPECT_EQ(Http::CustomHeaders::get().ContentEncodingValues.Gzip,
            content_encoding[0]->value().getStringView());
}

/**
 * Test suite for cases where the status_header_enabled configuration flag is set to true.
 */
class CompressorIntegrationTestWithStatusHeader : public CompressorIntegrationTest {};

INSTANTIATE_TEST_SUITE_P(
    IpVersions, CompressorIntegrationTestWithStatusHeader,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::Values(true)),
    [](const testing::TestParamInfo<std::tuple<Network::Address::IpVersion, bool>>& params) {
      return fmt::format("{}_{}", TestUtility::ipVersionToString(std::get<0>(params.param)),
                         "WithStatusHeader");
    });

/**
 * Exercises filter when upstream responds with content length below the default threshold.
 */
TEST_P(CompressorIntegrationTestWithStatusHeader, EnvoyCompressionStatusContentLengthTooSmall) {
  initializeDefaultFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};

  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "10"}, {"content-type", "application/json"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 10);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
  EXPECT_EQ(10U, response->body().size());
  EXPECT_EQ("gzip;ContentLengthTooSmall", response->headers()
                                              .get(Http::Headers::get().EnvoyCompressionStatus)[0]
                                              ->value()
                                              .getStringView());
}

/**
 * Exercises filter when upstream responds with restricted content-type value.
 */
TEST_P(CompressorIntegrationTestWithStatusHeader, EnvoyCompressionStatusContentTypeNotAllowed) {
  initializeFullFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "128"}, {"content-type", "application/xml"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 128);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
  EXPECT_EQ(128U, response->body().size());
  EXPECT_EQ("gzip;ContentTypeNotAllowed", response->headers()
                                              .get(Http::Headers::get().EnvoyCompressionStatus)[0]
                                              ->value()
                                              .getStringView());
}

/**
 * Exercises filter when upstream responds with an ETag header and disable_on_etag_header is true.
 */
TEST_P(CompressorIntegrationTestWithStatusHeader, EnvoyCompressionStatusEtagNotAllowed) {
  initializeFullFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                   {"content-length", "128"},
                                                   {"etag", "12345"},
                                                   {"content-type", "application/json"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 128);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
  EXPECT_EQ(128U, response->body().size());
  EXPECT_EQ("gzip;EtagNotAllowed", response->headers()
                                       .get(Http::Headers::get().EnvoyCompressionStatus)[0]
                                       ->value()
                                       .getStringView());
}

/**
 * Exercises filter when upstream responds with restricted response code value.
 */
TEST_P(CompressorIntegrationTestWithStatusHeader, EnvoyCompressionStatusStatusCodeNotAllowed) {
  initializeFullFilter();
  Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"},     {":path", "/test/long/url"},          {":scheme", "http"},
      {":authority", "host"}, {"accept-encoding", "deflate, gzip"}, {"range", "bytes=100-227"}};
  Http::TestResponseHeaderMapImpl response_headers{{":status", "206"},
                                                   {"content-length", "128"},
                                                   {"content-range", "bytes=100-227/567"},
                                                   {"content-type", "application/json"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 128);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("206", response->headers().getStatusValue());
  ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
  EXPECT_EQ(128U, response->body().size());
  EXPECT_EQ("gzip;StatusCodeNotAllowed", response->headers()
                                             .get(Http::Headers::get().EnvoyCompressionStatus)[0]
                                             ->value()
                                             .getStringView());
}

/**
 * Exercises gzip compression with full configuration and checks for the EnvoyCompressionStatus
 * header.
 */
TEST_P(CompressorIntegrationTestWithStatusHeader, EnvoyCompressionStatusCompressed) {
  initializeFullFilter();
  Http::TestRequestHeaderMapImpl request_headers{{":method", "GET"},
                                                 {":path", "/test/long/url"},
                                                 {":scheme", "http"},
                                                 {":authority", "host"},
                                                 {"accept-encoding", "deflate, gzip"}};
  Http::TestResponseHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "4400"}, {"content-type", "application/json"}};

  auto response = sendRequestAndWaitForResponse(request_headers, 0, response_headers, 4400);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("gzip", response->headers()
                        .get(Http::CustomHeaders::get().ContentEncoding)[0]
                        ->value()
                        .getStringView());
  EXPECT_EQ("gzip;Compressed;OriginalLength=4400",
            response->headers()
                .get(Http::Headers::get().EnvoyCompressionStatus)[0]
                ->value()
                .getStringView());
}

} // namespace Envoy
