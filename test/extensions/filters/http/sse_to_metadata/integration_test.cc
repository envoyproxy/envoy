#include "envoy/extensions/filters/http/sse_to_metadata/v3/sse_to_metadata.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class SseToMetadataIntegrationTest : public HttpProtocolIntegrationTest {
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
name: envoy.filters.http.sse_to_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sse_to_metadata.v3.SseToMetadata
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "model"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "model_name"
                type: STRING
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
    Protocols, SseToMetadataIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(SseToMetadataIntegrationTest, BasicSseTokenExtraction) {
  initializeFilter();
  runTest(request_headers_, "", response_headers_, sse_response_body_);

  // Verify stats
  // - Events 1,2: only model_name matches (2 successes)
  // - Event 3: both tokens and model_name match (2 successes)
  // - Event 4: [DONE] is invalid JSON (1 parse_error)
  // Total: 4 successes, 1 parse_error
  EXPECT_EQ(4UL, test_server_->counter("sse_to_metadata.resp.json.metadata_added")->value());
  EXPECT_EQ(0UL,
            test_server_->counter("sse_to_metadata.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("sse_to_metadata.resp.json.no_data_field")->value());
  EXPECT_EQ(1UL, test_server_->counter("sse_to_metadata.resp.json.parse_error")->value());
}

TEST_P(SseToMetadataIntegrationTest, SseWithSmallChunks) {
  initializeFilter();
  // Test with very small chunks to ensure buffering works correctly
  runTest(request_headers_, "", response_headers_, sse_response_body_, 5);

  EXPECT_EQ(4UL, test_server_->counter("sse_to_metadata.resp.json.metadata_added")->value());
  EXPECT_EQ(0UL,
            test_server_->counter("sse_to_metadata.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("sse_to_metadata.resp.json.no_data_field")->value());
  EXPECT_EQ(1UL, test_server_->counter("sse_to_metadata.resp.json.parse_error")->value());
}

TEST_P(SseToMetadataIntegrationTest, SseWithLargeChunks) {
  initializeFilter();
  // Test with large chunks
  runTest(request_headers_, "", response_headers_, sse_response_body_, 100);

  EXPECT_EQ(4UL, test_server_->counter("sse_to_metadata.resp.json.metadata_added")->value());
  EXPECT_EQ(0UL,
            test_server_->counter("sse_to_metadata.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("sse_to_metadata.resp.json.no_data_field")->value());
  EXPECT_EQ(1UL, test_server_->counter("sse_to_metadata.resp.json.parse_error")->value());
}

TEST_P(SseToMetadataIntegrationTest, MismatchedContentType) {
  Http::TestResponseHeaderMapImpl json_headers{{":status", "200"},
                                               {"content-type", "application/json"}};
  initializeFilter();
  const std::string json_body = R"({"result": "not an SSE stream"})";
  runTest(request_headers_, "", json_headers, json_body);

  // Content-type mismatch should not process the response
  EXPECT_EQ(0UL, test_server_->counter("sse_to_metadata.resp.json.metadata_added")->value());
  EXPECT_EQ(1UL,
            test_server_->counter("sse_to_metadata.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("sse_to_metadata.resp.json.no_data_field")->value());
  EXPECT_EQ(0UL, test_server_->counter("sse_to_metadata.resp.json.parse_error")->value());
}

TEST_P(SseToMetadataIntegrationTest, VerifyMetadataValues) {
  // Configure access log to capture and verify actual metadata values
  config_helper_.prependFilter(filter_config_);
  useAccessLog("%DYNAMIC_METADATA(envoy.lb)%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Send SSE response
  upstream_request_->encodeHeaders(response_headers_, false);
  Buffer::OwnedImpl buffer(sse_response_body_);
  upstream_request_->encodeData(buffer, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // Cleanup
  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Verify metadata was extracted correctly
  // The last matching event (event 3) has total_tokens: 30 and model: "gpt-4"
  // These are the final values written to metadata (last wins)
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr(R"("tokens":30)"));
  EXPECT_THAT(log, testing::HasSubstr(R"("model_name":"gpt-4")"));

  // Also verify stats
  EXPECT_EQ(4UL, test_server_->counter("sse_to_metadata.resp.json.metadata_added")->value());
  EXPECT_EQ(1UL, test_server_->counter("sse_to_metadata.resp.json.parse_error")->value());
}

// Test extraction of prompt_tokens and completion_tokens for input/output token-based rate limiting
TEST_P(SseToMetadataIntegrationTest, VerifyTokenBreakdown) {
  const std::string token_breakdown_config = R"EOF(
name: envoy.filters.http.sse_to_metadata
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.sse_to_metadata.v3.SseToMetadata
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "prompt_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "input_tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "usage"
                - key: "completion_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "output_tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "usage"
                - key: "total_tokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "total_tokens"
                type: NUMBER
)EOF";

  config_helper_.prependFilter(token_breakdown_config);
  useAccessLog("%DYNAMIC_METADATA(envoy.lb)%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Send SSE response
  upstream_request_->encodeHeaders(response_headers_, false);
  Buffer::OwnedImpl buffer(sse_response_body_);
  upstream_request_->encodeData(buffer, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // Cleanup
  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Verify all token metadata was extracted correctly
  // From event 3: prompt_tokens: 10, completion_tokens: 20, total_tokens: 30
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr(R"("input_tokens":10)"));
  EXPECT_THAT(log, testing::HasSubstr(R"("output_tokens":20)"));
  EXPECT_THAT(log, testing::HasSubstr(R"("total_tokens":30)"));

  // Verify stats: 3 token fields extracted from event 3
  EXPECT_EQ(3UL, test_server_->counter("sse_to_metadata.resp.json.metadata_added")->value());
}

} // namespace
} // namespace Envoy
