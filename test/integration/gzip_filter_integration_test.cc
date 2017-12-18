#include "common/decompressor/zlib_decompressor_impl.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class GzipIntegrationTest : public HttpIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  GzipIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override { decompressor_.init(window_bits); }
  void TearDown() override { cleanupUpstreamAndDownstream(); }

  void initializeFilter(const std::string& config) {
    config_helper_.addFilter(config);
    initialize();
    codec_client_ = makeHttpConnection(makeClientConnection((lookupPort("http"))));
  }

  void doRequestAndCompression(Http::TestHeaderMapImpl&& request_headers,
                               Http::TestHeaderMapImpl&& response_headers) {
    uint64_t content_length;
    ASSERT_TRUE(StringUtil::atoul(response_headers.get_("content-length").c_str(), content_length));
    const Buffer::OwnedImpl expected_response{std::string(content_length, 'a')};
    sendRequestAndWaitForResponse(request_headers, 0, response_headers, content_length);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    EXPECT_TRUE(response_->complete());
    EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
    ASSERT_TRUE(response_->headers().ContentEncoding() != nullptr);
    EXPECT_EQ(Http::Headers::get().ContentEncodingValues.Gzip,
              response_->headers().ContentEncoding()->value().c_str());
    ASSERT_TRUE(response_->headers().TransferEncoding() != nullptr);
    EXPECT_EQ(Http::Headers::get().TransferEncodingValues.Chunked,
              response_->headers().TransferEncoding()->value().c_str());

    Buffer::OwnedImpl decompressed_response{};
    const Buffer::OwnedImpl compressed_response{response_->body()};
    decompressor_.decompress(compressed_response, decompressed_response);
    ASSERT_EQ(content_length, decompressed_response.length());
    EXPECT_TRUE(TestUtility::buffersEqual(expected_response, decompressed_response));
  }

  void doRequestAndNoCompression(Http::TestHeaderMapImpl&& request_headers,
                                 Http::TestHeaderMapImpl&& response_headers) {
    uint64_t content_length;
    ASSERT_TRUE(StringUtil::atoul(response_headers.get_("content-length").c_str(), content_length));
    sendRequestAndWaitForResponse(request_headers, 0, response_headers, content_length);
    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());
    EXPECT_TRUE(response_->complete());
    EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
    ASSERT_TRUE(response_->headers().ContentEncoding() == nullptr);
    ASSERT_EQ(content_length, response_->body().size());
    EXPECT_EQ(response_->body(), std::string(content_length, 'a'));
  }

  const std::string full_config = R"EOF(
      name: envoy.gzip
      config:
        memory_level: 3
        compression_level: best
        compression_strategy: rle
        content_length: 100
        content_type:
          - text/html
          - application/json
        cache_control:
          - private
          - no-store
          - no-cache
        disable_on_etag: true
        disable_on_last_modified: true
    )EOF";

  const std::string default_config = R"EOF(
      name: envoy.gzip
    )EOF";

  const uint64_t window_bits{15 | 16};

  Decompressor::ZlibDecompressorImpl decompressor_{};
};

INSTANTIATE_TEST_CASE_P(IpVersions, GzipIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * Exercises gzip compression with default configuration.
 */
TEST_P(GzipIntegrationTest, AcceptanceDefaultConfigTest) {
  initializeFilter(default_config);
  doRequestAndCompression(Http::TestHeaderMapImpl{{":method", "GET"},
                                                  {":path", "/test/long/url"},
                                                  {":scheme", "http"},
                                                  {":authority", "host"},
                                                  {"accept-encoding", "deflate, gzip"}},
                          Http::TestHeaderMapImpl{{":status", "200"},
                                                  {"content-length", "4400"},
                                                  {"content-type", "text/xml"}});
}

/**
 * Exercises gzip compression with full configuration.
 */
TEST_P(GzipIntegrationTest, AcceptanceFullConfigTest) {
  initializeFilter(full_config);
  doRequestAndCompression(Http::TestHeaderMapImpl{{":method", "GET"},
                                                  {":path", "/test/long/url"},
                                                  {":scheme", "http"},
                                                  {":authority", "host"},
                                                  {"accept-encoding", "deflate, gzip"}},
                          Http::TestHeaderMapImpl{{":status", "200"},
                                                  {"content-length", "4400"},
                                                  {"content-type", "application/json"}});
}

/**
 * Exercises filter when client request contains unsupported 'accept-encoding' type.
 */
TEST_P(GzipIntegrationTest, NotSupportedAcceptEncoding) {
  initializeFilter(default_config);
  doRequestAndNoCompression(Http::TestHeaderMapImpl{{":method", "GET"},
                                                    {":path", "/test/long/url"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"accept-encoding", "deflate, br"}},
                            Http::TestHeaderMapImpl{{":status", "200"},
                                                    {"content-length", "128"},
                                                    {"content-type", "text/plain"}});
}

/**
 * Exercises filter when upstream response is already encoded.
 */
TEST_P(GzipIntegrationTest, UpstreamResponseAlreadyEncoded) {
  initializeFilter(default_config);
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"accept-encoding", "deflate, gzip"}};

  Http::TestHeaderMapImpl response_headers{{":status", "200"},
                                           {"content-encoding", "br"},
                                           {"content-length", "128"},
                                           {"content-type", "application/json"}};

  sendRequestAndWaitForResponse(request_headers, 0, response_headers, 128);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  ASSERT_STREQ("br", response_->headers().ContentEncoding()->value().c_str());
  EXPECT_EQ(128U, response_->body().size());
}

/**
 * Exercises filter when upstream responds with content length below the default threshold.
 */
TEST_P(GzipIntegrationTest, NotEnoughContentLength) {
  initializeFilter(default_config);
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"accept-encoding", "deflate, gzip"}};

  Http::TestHeaderMapImpl response_headers{
      {":status", "200"}, {"content-length", "10"}, {"content-type", "application/json"}};

  sendRequestAndWaitForResponse(request_headers, 0, response_headers, 10);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  ASSERT_TRUE(response_->headers().ContentEncoding() == nullptr);
  EXPECT_EQ(10U, response_->body().size());
}

/**
 * Exercises filter when response from upstream service is empty.
 */
TEST_P(GzipIntegrationTest, EmptyResponse) {
  initializeFilter(default_config);
  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"accept-encoding", "deflate, gzip"}};

  Http::TestHeaderMapImpl response_headers{{":status", "204"}, {"content-length", "0"}};

  sendRequestAndWaitForResponse(request_headers, 0, response_headers, 0);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("204", response_->headers().Status()->value().c_str());
  ASSERT_TRUE(response_->headers().ContentEncoding() == nullptr);
  EXPECT_EQ(0U, response_->body().size());
}

/**
 * Exercises filter when upstream responds with restricted content-type value.
 */
TEST_P(GzipIntegrationTest, SkipOnContentType) {
  initializeFilter(full_config);
  doRequestAndNoCompression(Http::TestHeaderMapImpl{{":method", "GET"},
                                                    {":path", "/test/long/url"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"accept-encoding", "deflate, gzip"}},
                            Http::TestHeaderMapImpl{{":status", "200"},
                                                    {"content-length", "128"},
                                                    {"content-type", "application/xml"}});
}

/**
 * Exercises filter when upstream responds with restricted cache-control value.
 */
TEST_P(GzipIntegrationTest, SkipOnCacheControl) {
  initializeFilter(full_config);
  doRequestAndNoCompression(Http::TestHeaderMapImpl{{":method", "GET"},
                                                    {":path", "/test/long/url"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"accept-encoding", "deflate, gzip"}},
                            Http::TestHeaderMapImpl{{":status", "200"},
                                                    {"content-length", "128"},
                                                    {"cache-control", "max-age=3600"},
                                                    {"content-type", "application/json"}});
}

} // namespace Envoy
