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
    const Buffer::OwnedImpl expected_response{std::string(4096, 'a')};
    sendRequestAndWaitForResponse(request_headers, 0, response_headers, expected_response.length());

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

    EXPECT_TRUE(TestUtility::buffersEqual(expected_response, decompressed_response));
  }

  void doRequestAndNoCompression(Http::TestHeaderMapImpl&& request_headers,
                                 Http::TestHeaderMapImpl&& response_headers) {
    sendRequestAndWaitForResponse(request_headers, 0, response_headers, 200);

    EXPECT_TRUE(upstream_request_->complete());
    EXPECT_EQ(0U, upstream_request_->bodyLength());

    EXPECT_TRUE(response_->complete());
    EXPECT_STREQ("200", response_->headers().Status()->value().c_str());
    ASSERT_TRUE(response_->headers().ContentEncoding() == nullptr);
    EXPECT_EQ(200U, response_->body().size());
  }

  Decompressor::ZlibDecompressorImpl decompressor_{};
};

INSTANTIATE_TEST_CASE_P(IpVersions, GzipIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()));

/**
 * Exercises client request and upstream response with gzip encoded data and minimum
 * filter configuration.
 */
TEST_P(GzipIntegrationTest, GzipEncodingAcceptanceTest) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
        deprecated_v1: true
    )EOF");

  doRequestAndCompression(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"accept-encoding", "deflate, gzip"}},
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}});
}

/**
 * Exercises client request and upstream response with gzip encoded data, but with
 * content-type constraint.
 */
TEST_P(GzipIntegrationTest, GzipEncodingByContentTypesAcceptanceTest) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
        deprecated_v1: true
        content-types:
          - text/html
    )EOF");

  doRequestAndCompression(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"accept-encoding", "deflate, gzip"}},
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}});
}

/**
 * Exercises client request and upstream response with gzip encoded data, but with
 * memory-level constraint.
 */
TEST_P(GzipIntegrationTest, GzipEncodingMemmoryLevelAcceptanceTest) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
        deprecated_v1: true
        content-types:
          - text/html
        memory_level: 3
    )EOF");

  doRequestAndCompression(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"accept-encoding", "deflate, gzip"}},
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}});
}

/**
 * Exercises client request and upstream response with gzip encoded data, but with
 * compression-level constraint.
 */
TEST_P(GzipIntegrationTest, GzipEncodingCompressionLevelAcceptanceTest) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
        deprecated_v1: true
        content-types:
          - text/html
        memory_level: 3
        compression_level: speed
    )EOF");

  doRequestAndCompression(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"accept-encoding", "deflate, gzip"}},
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}});
}

/**
 * Exercises client request and upstream response with gzip encoded data, but with
 * compression-strategy constraint.
 */
TEST_P(GzipIntegrationTest, GzipEncodingCompressionStrategyAcceptanceTest) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
        deprecated_v1: true
        content-types:
          - text/html
        memory_level: 1
        compression_strategy: rle
    )EOF");

  doRequestAndCompression(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"accept-encoding", "deflate, gzip"}},
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}});
}

/**
 * Exercises client request and upstream response with no gzip encoded data, due to
 * not supported accept-encoding.
 */
TEST_P(GzipIntegrationTest, NotSupportedAcceptEncoding) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
        deprecated_v1: true
    )EOF");

  doRequestAndNoCompression(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"accept-encoding", "deflate, br"}},
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}});
}

/**
 * Exercises client request and upstream response with no gzip encoded data, due to
 * not supported content-type.
 */
TEST_P(GzipIntegrationTest, NotSupportedContentType) {
  initializeFilter(R"EOF(
      name: envoy.gzip
      config:
        deprecated_v1: true
        content-types:
          - application/json
    )EOF");

  doRequestAndNoCompression(
      Http::TestHeaderMapImpl{{":method", "GET"},
                              {":path", "/test/long/url"},
                              {":scheme", "http"},
                              {":authority", "host"},
                              {"accept-encoding", "deflate, br"}},
      Http::TestHeaderMapImpl{{":status", "200"}, {"content-type", "text/html"}});
}

} // namespace Envoy
