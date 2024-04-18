#include "test/integration/http_protocol_integration.h"

namespace Envoy {
namespace {

absl::string_view testTmpDir() {
  auto env_tmpdir = std::getenv("TEST_TMPDIR");
  if (env_tmpdir) {
    return env_tmpdir;
  }
  env_tmpdir = std::getenv("TMPDIR");
  return env_tmpdir ? env_tmpdir : "/tmp";
}

// A config which will trigger buffering and injecting content length into headers.
std::string contentLengthConfig() {
  return absl::StrCat(
      R"EOF(
        name: file_system_buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.file_system_buffer.v3.FileSystemBufferFilterConfig
          manager_config:
            thread_pool:
              thread_count: 1
          request:
            memory_buffer_bytes_limit: 4096
            storage_buffer_bytes_limit: 8000000
            behavior:
              fully_buffer_and_always_inject_content_length: {}
          response:
            memory_buffer_bytes_limit: 4096
            storage_buffer_bytes_limit: 8000000
            behavior:
              fully_buffer_and_always_inject_content_length: {}
          storage_buffer_path: )EOF",
      testTmpDir());
}

// A config with a small request buffer, to test the behavior of exceeding the limit.
std::string smallRequestBufferConfig() {
  return absl::StrCat(
      R"EOF(
        name: file_system_buffer
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.file_system_buffer.v3.FileSystemBufferFilterConfig
          manager_config:
            thread_pool:
              thread_count: 1
          request:
            memory_buffer_bytes_limit: 64
            storage_buffer_bytes_limit: 64
            behavior:
              fully_buffer: {}
          storage_buffer_path: )EOF",
      testTmpDir());
}

using FileSystemBufferIntegrationTest = HttpProtocolIntegrationTest;

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, FileSystemBufferIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParams(
        /*downstream_protocols = */ {Envoy::Http::CodecType::HTTP1, Envoy::Http::CodecType::HTTP2},
        /*upstream_protocols = */ {Envoy::Http::CodecType::HTTP1, Envoy::Http::CodecType::HTTP2})),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(FileSystemBufferIntegrationTest, NotFoundBodyBuffer) {
  config_helper_.prependFilter(contentLengthConfig());
  testRouterNotFoundWithBody();
}

TEST_P(FileSystemBufferIntegrationTest, RequestAndResponseWithGiantBodyBuffer) {
  config_helper_.prependFilter(contentLengthConfig());
  // Not quite as giant as the memory buffer's integration test uses - with
  // disk operations involved that size risks timing out the test.
  testRouterRequestAndResponseWithBody(1024 * 1024, 1024 * 1024, false, false, nullptr,
                                       std::chrono::milliseconds(25000) * TIMEOUT_FACTOR);
}

TEST_P(FileSystemBufferIntegrationTest, HeaderOnlyRequestAndResponseBuffer) {
  config_helper_.prependFilter(contentLengthConfig());
  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(FileSystemBufferIntegrationTest, RequestAndResponseWithBodyBuffer) {
  config_helper_.prependFilter(contentLengthConfig());
  testRouterRequestAndResponseWithBody(1024, 512, false);
}

TEST_P(FileSystemBufferIntegrationTest, RequestAndResponseWithZeroByteBodyBuffer) {
  config_helper_.prependFilter(contentLengthConfig());
  testRouterRequestAndResponseWithBody(0, 0, false);
}

TEST_P(FileSystemBufferIntegrationTest, RequestPopulateContentLength) {
  config_helper_.prependFilter(contentLengthConfig());
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":scheme", "http"}, {":path", "/shelf"}, {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, "123", false);
  codec_client_->sendData(*request_encoder_, "456", false);
  codec_client_->sendData(*request_encoder_, "789", true);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(default_response_headers_, false);
  upstream_request_->encodeData(123, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  ASSERT_NE(upstream_request_->headers().ContentLength(), nullptr);
  EXPECT_EQ(upstream_request_->headers().ContentLength()->value().getStringView(), "9");

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
  ASSERT_NE(response->headers().ContentLength(), nullptr);
  EXPECT_EQ(response->headers().ContentLength()->value().getStringView(), "123");
}

TEST_P(FileSystemBufferIntegrationTest, RequestPopulateContentLengthOnTrailers) {
  config_helper_.prependFilter(contentLengthConfig());
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto encoder_decoder = codec_client_->startRequest(Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":scheme", "http"}, {":path", "/shelf"}, {":authority", "host"}});
  request_encoder_ = &encoder_decoder.first;
  IntegrationStreamDecoderPtr response = std::move(encoder_decoder.second);
  codec_client_->sendData(*request_encoder_, "0123", false);
  codec_client_->sendData(*request_encoder_, "456", false);
  codec_client_->sendData(*request_encoder_, "789", false);
  Http::TestRequestTrailerMapImpl request_trailers{{"request", "trailer"}};
  codec_client_->sendTrailers(*request_encoder_, request_trailers);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  upstream_request_->encodeHeaders(default_response_headers_, true);
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  ASSERT_NE(upstream_request_->headers().ContentLength(), nullptr);
  EXPECT_EQ(upstream_request_->headers().ContentLength()->value().getStringView(), "10");

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("200", response->headers().getStatusValue());
}

TEST_P(FileSystemBufferIntegrationTest, RequestBufferLimitExceeded) {
  // Make sure the connection isn't closed during request upload.
  // Without a large drain-close it's possible that the local reply will be sent
  // during request upload, and continued upload will result in TCP reset before
  // the response is read.
  config_helper_.addConfigModifier(
      [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
             hcm) { hcm.mutable_delayed_close_timeout()->set_seconds(2000 * 1000); });
  config_helper_.prependFilter(smallRequestBufferConfig());
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                     {":path", "/dynamo/url"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"x-forwarded-for", "10.0.0.1"},
                                     {"x-envoy-retry-on", "5xx"}},
      1024 * 65);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("413", response->headers().getStatusValue());
}

} // namespace
} // namespace Envoy
