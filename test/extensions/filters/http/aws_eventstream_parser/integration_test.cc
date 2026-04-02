#include <zlib.h>

#include "envoy/extensions/filters/http/aws_eventstream_parser/v3/aws_eventstream_parser.pb.h"

#include "test/integration/http_protocol_integration.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

class AwsEventstreamParserIntegrationTest : public HttpProtocolIntegrationTest {
public:
  void initializeFilter() {
    config_helper_.prependFilter(filter_config_);
    initialize();
  }

  void runTest(const Http::RequestHeaderMap& request_headers, const std::string& request_body,
               const Http::ResponseHeaderMap& response_headers, const std::string& response_body,
               const size_t chunk_size = 100) {
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

    // Send chunked EventStream response
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

  // Helper to build an EventStream message with the given payload.
  // This creates a properly formatted binary EventStream message.
  static std::string buildEventstreamMessage(const std::string& payload) {
    // EventStream message format:
    // - Prelude (12 bytes): total_length(4) + headers_length(4) + prelude_crc(4)
    // - Headers (variable)
    // - Payload (variable)
    // - Message CRC (4 bytes)

    // For simplicity, we'll create a message with no headers.
    const uint32_t headers_length = 0;
    const uint32_t payload_length = payload.size();
    const uint32_t total_length =
        12 + headers_length + payload_length + 4; // prelude + headers + payload + trailer

    std::string message;
    message.resize(total_length);

    auto* data = reinterpret_cast<uint8_t*>(message.data());

    // Write total_length (big-endian)
    data[0] = (total_length >> 24) & 0xFF;
    data[1] = (total_length >> 16) & 0xFF;
    data[2] = (total_length >> 8) & 0xFF;
    data[3] = total_length & 0xFF;

    // Write headers_length (big-endian)
    data[4] = 0;
    data[5] = 0;
    data[6] = 0;
    data[7] = 0;

    // Compute prelude CRC (CRC32 of first 8 bytes)
    uint32_t prelude_crc = crc32(0, data, 8);
    data[8] = (prelude_crc >> 24) & 0xFF;
    data[9] = (prelude_crc >> 16) & 0xFF;
    data[10] = (prelude_crc >> 8) & 0xFF;
    data[11] = prelude_crc & 0xFF;

    // Copy payload
    std::memcpy(data + 12, payload.data(), payload_length);

    // Compute message CRC (CRC32 of everything except the last 4 bytes)
    uint32_t message_crc = crc32(0, data, total_length - 4);
    data[total_length - 4] = (message_crc >> 24) & 0xFF;
    data[total_length - 3] = (message_crc >> 16) & 0xFF;
    data[total_length - 2] = (message_crc >> 8) & 0xFF;
    data[total_length - 1] = message_crc & 0xFF;

    return message;
  }

  const std::string filter_config_ = R"EOF(
name: envoy.filters.http.aws_eventstream_parser
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_eventstream_parser.v3.AwsEventstreamParser
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "totalTokens"
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
      {":scheme", "http"}, {":path", "/invoke"}, {":method", "POST"}, {":authority", "host"}};

  Http::TestResponseHeaderMapImpl response_headers_{
      {":status", "200"}, {"content-type", "application/vnd.amazon.eventstream"}};

  // Build sample EventStream response with multiple messages
  std::string eventstream_response_body_ =
      buildEventstreamMessage(
          R"({"contentBlockDelta":{"delta":{"text":"Hello"},"contentBlockIndex":0}})") +
      buildEventstreamMessage(
          R"({"contentBlockDelta":{"delta":{"text":" world"},"contentBlockIndex":0}})") +
      buildEventstreamMessage(
          R"({"messageStop":{"stopReason":"end_turn"},"model":"anthropic.claude-3","usage":{"inputTokens":10,"outputTokens":20,"totalTokens":30}})");
};

// TODO(#26236): Fix test suite for HTTP/3.
INSTANTIATE_TEST_SUITE_P(
    Protocols, AwsEventstreamParserIntegrationTest,
    testing::ValuesIn(HttpProtocolIntegrationTest::getProtocolTestParamsWithoutHTTP3()),
    HttpProtocolIntegrationTest::protocolTestParamsToString);

TEST_P(AwsEventstreamParserIntegrationTest, BasicEventstreamTokenExtraction) {
  initializeFilter();
  runTest(request_headers_, "", response_headers_, eventstream_response_body_);

  // Verify stats
  // - Messages 1,2: no matches (model and usage.totalTokens not present)
  // - Message 3: both tokens and model_name match (2 successes)
  // Total: 2 metadata_added
  EXPECT_EQ(2UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
  EXPECT_EQ(
      0UL,
      test_server_->counter("aws_eventstream_parser.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.empty_payload")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.parse_error")->value());
}

TEST_P(AwsEventstreamParserIntegrationTest, EventstreamWithSmallChunks) {
  initializeFilter();
  // Test with very small chunks to ensure buffering works correctly
  runTest(request_headers_, "", response_headers_, eventstream_response_body_, 10);

  EXPECT_EQ(2UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
  EXPECT_EQ(
      0UL,
      test_server_->counter("aws_eventstream_parser.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.empty_payload")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.parse_error")->value());
}

TEST_P(AwsEventstreamParserIntegrationTest, EventstreamWithLargeChunks) {
  initializeFilter();
  // Test with large chunks (entire response in one chunk)
  runTest(request_headers_, "", response_headers_, eventstream_response_body_, 1000);

  EXPECT_EQ(2UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
  EXPECT_EQ(
      0UL,
      test_server_->counter("aws_eventstream_parser.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.empty_payload")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.parse_error")->value());
}

TEST_P(AwsEventstreamParserIntegrationTest, MismatchedContentType) {
  Http::TestResponseHeaderMapImpl json_headers{{":status", "200"},
                                               {"content-type", "application/json"}};
  initializeFilter();
  const std::string json_body = R"({"result": "not an EventStream"})";
  runTest(request_headers_, "", json_headers, json_body);

  // Content-type mismatch should not process the response
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
  EXPECT_EQ(
      1UL,
      test_server_->counter("aws_eventstream_parser.resp.json.mismatched_content_type")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.empty_payload")->value());
  EXPECT_EQ(0UL, test_server_->counter("aws_eventstream_parser.resp.json.parse_error")->value());
}

TEST_P(AwsEventstreamParserIntegrationTest, VerifyMetadataValues) {
  // Configure access log to capture and verify actual metadata values
  config_helper_.prependFilter(filter_config_);
  useAccessLog("%DYNAMIC_METADATA(envoy.lb)%");
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  auto response = codec_client_->makeHeaderOnlyRequest(request_headers_);

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  // Send EventStream response
  upstream_request_->encodeHeaders(response_headers_, false);
  Buffer::OwnedImpl buffer(eventstream_response_body_);
  upstream_request_->encodeData(buffer, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // Cleanup
  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Verify metadata was extracted correctly
  // The last matching message has totalTokens: 30 and model: "anthropic.claude-3"
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr(R"("tokens":30)"));
  EXPECT_THAT(log, testing::HasSubstr(R"("model_name":"anthropic.claude-3")"));

  // Also verify stats
  EXPECT_EQ(2UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
}

// Test extraction of inputTokens and outputTokens for input/output token-based rate limiting
TEST_P(AwsEventstreamParserIntegrationTest, VerifyTokenBreakdown) {
  const std::string token_breakdown_config = R"EOF(
name: envoy.filters.http.aws_eventstream_parser
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.aws_eventstream_parser.v3.AwsEventstreamParser
  response_rules:
    content_parser:
      name: envoy.content_parsers.json
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.content_parsers.json.v3.JsonContentParser
        rules:
          - rule:
              selectors:
                - key: "usage"
                - key: "inputTokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "input_tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "usage"
                - key: "outputTokens"
              on_present:
                metadata_namespace: "envoy.lb"
                key: "output_tokens"
                type: NUMBER
          - rule:
              selectors:
                - key: "usage"
                - key: "totalTokens"
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

  // Send EventStream response
  upstream_request_->encodeHeaders(response_headers_, false);
  Buffer::OwnedImpl buffer(eventstream_response_body_);
  upstream_request_->encodeData(buffer, true);

  ASSERT_TRUE(response->waitForEndStream());
  EXPECT_TRUE(response->complete());

  // Cleanup
  codec_client_->close();
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());

  // Verify all token metadata was extracted correctly
  // From message 3: inputTokens: 10, outputTokens: 20, totalTokens: 30
  std::string log = waitForAccessLog(access_log_name_);
  EXPECT_THAT(log, testing::HasSubstr(R"("input_tokens":10)"));
  EXPECT_THAT(log, testing::HasSubstr(R"("output_tokens":20)"));
  EXPECT_THAT(log, testing::HasSubstr(R"("total_tokens":30)"));

  // Verify stats: 3 token fields extracted from message 3
  EXPECT_EQ(3UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
}

TEST_P(AwsEventstreamParserIntegrationTest, EmptyPayloadMessage) {
  initializeFilter();

  // Create response with an empty payload message
  std::string response_with_empty =
      buildEventstreamMessage("") + buildEventstreamMessage(R"({"model":"test-model"})");

  runTest(request_headers_, "", response_headers_, response_with_empty);

  // First message has empty payload, second message matches model rule
  EXPECT_EQ(1UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
  EXPECT_EQ(1UL, test_server_->counter("aws_eventstream_parser.resp.json.empty_payload")->value());
}

TEST_P(AwsEventstreamParserIntegrationTest, InvalidJsonPayload) {
  initializeFilter();

  // Create response with invalid JSON payload followed by valid message
  std::string response_with_invalid = buildEventstreamMessage("not valid json") +
                                      buildEventstreamMessage(R"({"model":"test-model"})");

  runTest(request_headers_, "", response_headers_, response_with_invalid);

  // First message has parse error, second message matches model rule
  EXPECT_EQ(1UL, test_server_->counter("aws_eventstream_parser.resp.json.metadata_added")->value());
  EXPECT_EQ(1UL, test_server_->counter("aws_eventstream_parser.resp.json.parse_error")->value());
}

} // namespace
} // namespace Envoy
