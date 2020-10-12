#include "envoy/event/timer.h"

#include "extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

#include "test/integration/http_integration.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class CompressorIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public Event::SimulatedTimeSystem,
                                  public HttpIntegrationTest {
public:
  CompressorIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override { decompressor_.init(window_bits); }
  void TearDown() override { cleanupUpstreamAndDownstream(); }

  void initializeFilter(const std::string& config) {
    config_helper_.addFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
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
    EXPECT_EQ(Http::CustomHeaders::get().ContentEncodingValues.Gzip,
              response->headers()
                  .get(Http::CustomHeaders::get().ContentEncoding)[0]
                  ->value()
                  .getStringView());
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
    EXPECT_EQ("200", response->headers().getStatusValue());
    ASSERT_TRUE(response->headers().get(Http::CustomHeaders::get().ContentEncoding).empty());
    ASSERT_EQ(content_length, response->body().size());
    EXPECT_EQ(response->body(), std::string(content_length, 'a'));
  }

  const std::string full_config{R"EOF(
      name: compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        disable_on_etag_header: true
        content_length: 100
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

  const uint64_t window_bits{15 | 16};

  Stats::IsolatedStoreImpl stats_store_;
  Extensions::Compression::Gzip::Decompressor::ZlibDecompressorImpl decompressor_{stats_store_,
                                                                                  "test"};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, CompressorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

/**
 * Exercises gzip compression with default configuration.
 */
TEST_P(CompressorIntegrationTest, AcceptanceDefaultConfigTest) {
  initializeFilter(default_config);
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
  initializeFilter(full_config);
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
  initializeFilter(default_config);
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
  initializeFilter(default_config);
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
  initializeFilter(default_config);
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
  initializeFilter(default_config);
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
  initializeFilter(default_config);
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
  initializeFilter(full_config);
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
 * Exercises filter when upstream responds with restricted cache-control value.
 */
TEST_P(CompressorIntegrationTest, SkipOnCacheControl) {
  initializeFilter(full_config);
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
  initializeFilter(full_config);
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
  initializeFilter(default_config);
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
} // namespace Envoy
