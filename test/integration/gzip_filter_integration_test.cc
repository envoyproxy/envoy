#include "common/decompressor/zlib_decompressor_impl.h"

#include "test/integration/http_integration.h"

#include "gtest/gtest.h"

namespace Envoy {

class GzipIntegrationTest : public HttpIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  GzipIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void SetUp() override { decompressor_.init(31); }
  void TearDown() override { cleanupUpstreamAndDownstream(); }

  void initializeFilter(std::string&& config) {
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

  Decompressor::ZlibDecompressorImpl decompressor_{};
};

INSTANTIATE_TEST_CASE_P(IpVersions, GzipIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * Exercises gzip compression with default configuration
 */
TEST_P(GzipIntegrationTest, GzipEncodingAcceptanceTest) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
    )EOF");

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
 * Exercises filter when client request contains unsupported 'accept-encoding' type
 */
TEST_P(GzipIntegrationTest, NotSupportedAcceptEncoding) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
    )EOF");

  doRequestAndNoCompression(Http::TestHeaderMapImpl{{":method", "GET"},
                                                    {":path", "/test/long/url"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"accept-encoding", "deflate, br"}},
                            Http::TestHeaderMapImpl{{":status", "200"},
                                                    {"content-length", "4400"},
                                                    {"content-type", "text/plain"}});
}

/**
 * Exercises filter when upstream response is already encoded
 */
TEST_P(GzipIntegrationTest, UpstreamResponseAlreadyEncoded) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
    )EOF");

  Http::TestHeaderMapImpl request_headers{{":method", "GET"},
                                          {":path", "/test/long/url"},
                                          {":scheme", "http"},
                                          {":authority", "host"},
                                          {"accept-encoding", "deflate, gzip"}};

  Http::TestHeaderMapImpl response_headers{{":status", "200"},
                                           {"content-encoding", "br"},
                                           {"content-length", "4400"},
                                           {"content-type", "application/json"}};

  sendRequestAndWaitForResponse(request_headers, 0, response_headers, 4400);

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ(0U, upstream_request_->bodyLength());
  EXPECT_TRUE(response_->complete());
  EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
  ASSERT_STREQ("br", response_->headers().ContentEncoding()->value().c_str());
  EXPECT_EQ(4400U, response_->body().size());
}

/**
 * Exercises filter when upstream responds with content length below the default threshold
 */
TEST_P(GzipIntegrationTest, NotEnoughContentLength) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
    )EOF");

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
 * Exercises filter when upstream responds with not supportted content type
 */
TEST_P(GzipIntegrationTest, NotSupportedContentType) {
  initializeFilter(R"EOF(
    name: envoy.gzip
    config:
      content_type:
        - application/json
    )EOF");

  doRequestAndNoCompression(Http::TestHeaderMapImpl{{":method", "GET"},
                                                    {":path", "/test/long/url"},
                                                    {":scheme", "http"},
                                                    {":authority", "host"},
                                                    {"accept-encoding", "deflate, gzip"}},
                            Http::TestHeaderMapImpl{{":status", "200"},
                                                    {"content-length", "1024"},
                                                    {"content-type", "application/xml"}});
}

} // namespace Envoy
