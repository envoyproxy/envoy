#include "envoy/extensions/filters/http/stream_to_metadata/v3/stream_to_metadata.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class StreamToMetadataIntegrationTest : public Event::TestUsingSimulatedTime,
                                        public HttpProtocolIntegrationTest {
public:
  void initializeFilter() {
    config_helper_.prependFilter(filter_config_);
    initialize();
  }

  void runTest(const Http::RequestHeaderMap& request_headers, const std::string& request_body,
               const Http::ResponseHeaderMap& response_headers, const std::string& response_body,
               const size_t chunk_size = 20) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    IntegrationStreamDecoderPtr response;
    if (request_body.empty()) {
      response = codec_client_->makeHeaderOnlyRequest(request_headers);
    } else {
      auto encoder_decoder = codec_client_->startRequest(request_headers);
      request_encoder_ = &encoder_decoder.first;
      response = std::move(encoder_decoder.second);
      Buffer::OwnedImpl buffer(request_body);
      codec_client_->sendData(*request_encoder_, buffer, true);
    }

    ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
    ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
    ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

    // Send chunked SSE response
    upstream_request_->encodeHeaders(response_headers, false);
    size_t i = 0;
    for (; i < response_body.length() / chunk_size; i++) {
      Buffer::OwnedImpl buffer(response_body.substr(i * chunk_size, chunk_size));
      upstream_request_->encodeData(buffer, false);
    }
    // Send the last chunk flagged as end_stream
    Buffer::OwnedImpl buffer(
        response_body.substr(i * chunk_size, response_body.length() % chunk_size));
    upstream_request_->encodeData(buffer, true);

    ASSERT_TRUE(response->waitForEndStream());
    EXPECT_TRUE(response->complete());

    // cleanup
    codec_client_->close();
    ASSERT_TRUE(fake_upstream_connection_->close());
    ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  }

  const std::string filter_config_ = R"EOF(
name: envoy.filters.http.stream_to_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.stream_to_metadata.v3.StreamToMetadata
  response_rules:
    format: SERVER_SENT_EVENTS
    rules:
      - selector:
          json_path:
            path: ["usage", "total_tokens"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "tokens"
            type: NUMBER
      - selector:
          json_path:
            path: ["model"]
        on_present:
          - metadata_namespace: "envoy.lb"
            key: "model_name"
            type: STRING
        stop_processing_on_match: false
)EOF";

  Http::TestRequestHeaderMapImpl request_headers_{
      {":scheme", "http"}, {":path", "/chat"}, {":method", "POST"}, {":authority", "host"}};

  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"content-type", "text/event-stream"}};

  const std::string sse_response_body_ =
      "data: "
      "{\"id\":\"1\",\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4\",\"choices\":[{"
      "\"delta\":{\"content\":\"Hello\"}}]}\n\n"
      "data: "
      "{\"id\":\"2\",\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4\",\"choices\":[{"
      "\"delta\":{\"content\":\" world\"}}]}\n\n"
      "data: "
      "{\"id\":\"3\",\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4\",\"choices\":[],"
      "\"usage\":{\"prompt_tokens\":10,\"completion_tokens\":20,\"total_tokens\":30}}\n\n"
      "data: [DONE]\n\n";
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, StreamToMetadataIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(StreamToMetadataIntegrationTest, BasicSseTokenExtraction) {
  initializeFilter();
  runTest(request_headers_, "", response_headers_, sse_response_body_);
}

TEST_P(StreamToMetadataIntegrationTest, SseWithSmallChunks) {
  initializeFilter();
  // Test with very small chunks to ensure buffering works correctly
  runTest(request_headers_, "", response_headers_, sse_response_body_, 5);
}

TEST_P(StreamToMetadataIntegrationTest, SseWithLargeChunks) {
  initializeFilter();
  // Test with large chunks
  runTest(request_headers_, "", response_headers_, sse_response_body_, 100);
}

TEST_P(StreamToMetadataIntegrationTest, MismatchedContentType) {
  Http::TestResponseHeaderMapImpl json_headers{{":status", "200"},
                                               {"content-type", "application/json"}};
  initializeFilter();
  const std::string json_body = R"({"result": "not an SSE stream"})";
  runTest(request_headers_, "", json_headers, json_body);
}

} // namespace
} // namespace Envoy
