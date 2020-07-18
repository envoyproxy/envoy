#include "envoy/event/timer.h"

#include "extensions/compression/gzip/compressor/config.h"

#include "test/integration/http_integration.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

class DecompressorIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                    public HttpIntegrationTest {
public:
  DecompressorIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP2, GetParam()) {
    Extensions::Compression::Gzip::Compressor::GzipCompressorLibraryFactory
        compressor_library_factory;
    envoy::extensions::compression::gzip::compressor::v3::Gzip factory_config;
    testing::NiceMock<Server::Configuration::MockFactoryContext> context;

    auto compressor_factory =
        compressor_library_factory.createCompressorFactoryFromProto(factory_config, context);
    request_compressor_ = compressor_factory->createCompressor();
    response_compressor_ = compressor_factory->createCompressor();
  }

  void TearDown() override { cleanupUpstreamAndDownstream(); }

  void initializeFilter(const std::string& config) {
    config_helper_.addFilter(config);
    HttpIntegrationTest::initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  const std::string default_config{R"EOF(
      name: default_decompressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
        decompressor_library:
          name: testlib
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip
    )EOF"};

  Envoy::Compression::Compressor::CompressorPtr request_compressor_{};
  Envoy::Compression::Compressor::CompressorPtr response_compressor_{};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, DecompressorIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

/**
 * Exercises gzip decompression bidirectionally with default configuration.
 */
TEST_P(DecompressorIntegrationTest, BidirectionalDecompression) {
  // Use gzip for decompression.
  initializeFilter(default_config);

  // Enable request decompression by setting the Content-Encoding header to gzip.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "host"},
                                                                 {"content-encoding", "gzip"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Send first data chunk upstream.
  Buffer::OwnedImpl request_data1;
  TestUtility::feedBufferWithRandomCharacters(request_data1, 8192);
  auto uncompressed_request_length = request_data1.length();
  request_compressor_->compress(request_data1, Envoy::Compression::Compressor::State::Flush);
  auto compressed_request_length = request_data1.length();
  codec_client_->sendData(*request_encoder, request_data1, false);

  // Send second data chunk upstream and finish the request stream.
  Buffer::OwnedImpl request_data2;
  TestUtility::feedBufferWithRandomCharacters(request_data2, 16384);
  uncompressed_request_length += request_data2.length();
  request_compressor_->compress(request_data2, Envoy::Compression::Compressor::State::Finish);
  compressed_request_length += request_data2.length();
  codec_client_->sendData(*request_encoder, request_data2, true);

  // Wait for frames to arrive upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Assert that the total bytes received upstream equal the sum of the uncompressed byte buffers
  // sent.
  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("chunked", upstream_request_->headers().TransferEncoding()->value().getStringView());
  EXPECT_EQ("gzip", upstream_request_->headers()
                        .get(Http::LowerCaseString("accept-encoding"))
                        ->value()
                        .getStringView());
  EXPECT_EQ(nullptr, upstream_request_->headers().get(Http::LowerCaseString("content-encoding")));
  EXPECT_EQ(uncompressed_request_length, upstream_request_->bodyLength());

  // Verify stats
  test_server_->waitForCounterEq("http.config_test.decompressor.testlib.gzip.request.decompressed",
                                 1);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.request.not_decompressed", 0);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.request.total_compressed_bytes",
      compressed_request_length);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.request.total_uncompressed_bytes",
      uncompressed_request_length);

  // Enable response decompression by setting the Content-Encoding header to gzip.
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-encoding", "gzip"}}, false);

  // Send first data chunk downstream.
  Buffer::OwnedImpl response_data1;
  TestUtility::feedBufferWithRandomCharacters(response_data1, 4096);
  auto uncompressed_response_length = response_data1.length();
  response_compressor_->compress(response_data1, Envoy::Compression::Compressor::State::Flush);
  auto compressed_response_length = response_data1.length();
  upstream_request_->encodeData(response_data1, false);

  // Send second data chunk downstream and finish the response stream.
  Buffer::OwnedImpl response_data2;
  TestUtility::feedBufferWithRandomCharacters(response_data2, 8192);
  uncompressed_response_length += response_data2.length();
  response_compressor_->compress(response_data2, Envoy::Compression::Compressor::State::Flush);
  compressed_response_length += response_data2.length();
  upstream_request_->encodeData(response_data2, true);

  // Wait for frames to arrive downstream.
  response->waitForEndStream();

  // Assert that the total bytes received downstream equal the sum of the uncompressed byte buffers
  // sent.
  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());
  EXPECT_EQ(uncompressed_response_length, response->body().length());

  // Verify stats
  test_server_->waitForCounterEq("http.config_test.decompressor.testlib.gzip.response.decompressed",
                                 1);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.response.not_decompressed", 0);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.response.total_compressed_bytes",
      compressed_response_length);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.response.total_uncompressed_bytes",
      uncompressed_response_length);
}

/**
 * Exercises gzip decompression bidirectionally with configuration using incompatible window bits
 * resulting in an error.
 */
TEST_P(DecompressorIntegrationTest, BidirectionalDecompressionError) {
  const std::string bad_config{R"EOF(
      name: default_decompressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
        decompressor_library:
          name: testlib
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip
            window_bits: 10
    )EOF"};
  // Use gzip for decompression.
  initializeFilter(bad_config);

  // Enable request decompression by setting the Content-Encoding header to gzip.
  auto encoder_decoder =
      codec_client_->startRequest(Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                                 {":scheme", "http"},
                                                                 {":path", "/test/long/url"},
                                                                 {":authority", "host"},
                                                                 {"content-encoding", "gzip"}});
  auto request_encoder = &encoder_decoder.first;
  auto response = std::move(encoder_decoder.second);

  // Send first data chunk upstream.
  Buffer::OwnedImpl request_data1;
  TestUtility::feedBufferWithRandomCharacters(request_data1, 8192);
  request_compressor_->compress(request_data1, Envoy::Compression::Compressor::State::Flush);
  auto compressed_request_length = request_data1.length();
  codec_client_->sendData(*request_encoder, request_data1, false);

  // Send second data chunk upstream and finish the request stream.
  Buffer::OwnedImpl request_data2;
  TestUtility::feedBufferWithRandomCharacters(request_data2, 16384);
  request_compressor_->compress(request_data2, Envoy::Compression::Compressor::State::Finish);
  compressed_request_length += request_data2.length();
  codec_client_->sendData(*request_encoder, request_data2, true);

  // Wait for frames to arrive upstream.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  EXPECT_TRUE(upstream_request_->complete());
  EXPECT_EQ("chunked", upstream_request_->headers().TransferEncoding()->value().getStringView());
  EXPECT_EQ("gzip", upstream_request_->headers()
                        .get(Http::LowerCaseString("accept-encoding"))
                        ->value()
                        .getStringView());
  EXPECT_EQ(nullptr, upstream_request_->headers().get(Http::LowerCaseString("content-encoding")));

  // Verify stats. While the stream was decompressed, there should be a decompression failure.
  test_server_->waitForCounterEq("http.config_test.decompressor.testlib.gzip.request.decompressed",
                                 1);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.request.not_decompressed", 0);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.request.total_compressed_bytes",
      compressed_request_length);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.decompressor_library.zlib_data_error", 2);

  // Enable response decompression by setting the Content-Encoding header to gzip.
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-encoding", "gzip"}}, false);

  // Send first data chunk downstream.
  Buffer::OwnedImpl response_data1;
  TestUtility::feedBufferWithRandomCharacters(response_data1, 4096);
  response_compressor_->compress(response_data1, Envoy::Compression::Compressor::State::Flush);
  auto compressed_response_length = response_data1.length();
  upstream_request_->encodeData(response_data1, false);

  // Send second data chunk downstream and finish the response stream.
  Buffer::OwnedImpl response_data2;
  TestUtility::feedBufferWithRandomCharacters(response_data2, 8192);
  response_compressor_->compress(response_data2, Envoy::Compression::Compressor::State::Flush);
  compressed_response_length += response_data2.length();
  upstream_request_->encodeData(response_data2, true);

  // Wait for frames to arrive downstream.
  response->waitForEndStream();

  EXPECT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().Status()->value().getStringView());

  // Verify stats. While the stream was decompressed, there should be a decompression failure.
  test_server_->waitForCounterEq("http.config_test.decompressor.testlib.gzip.response.decompressed",
                                 1);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.response.not_decompressed", 0);
  test_server_->waitForCounterEq(
      "http.config_test.decompressor.testlib.gzip.response.total_compressed_bytes",
      compressed_response_length);
  test_server_->waitForCounterGe(
      "http.config_test.decompressor.testlib.gzip.decompressor_library.zlib_data_error", 3);
}

} // namespace Envoy
