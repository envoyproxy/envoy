#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

using testing::_;
using testing::NiceMock;
using testing::Return;

class McpFilterTest : public testing::Test {
public:
  McpFilterTest() {
    // Default config with PASS_THROUGH mode
    envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
    proto_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::PASS_THROUGH);
    config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
    filter_ = std::make_unique<McpFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void setupRejectMode() {
    envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
    proto_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::REJECT_NO_MCP);
    config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
    filter_ = std::make_unique<McpFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void setupWithClearRouteCache(bool clear_route_cache) {
    envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
    proto_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::PASS_THROUGH);
    proto_config.set_clear_route_cache(clear_route_cache);
    config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
    filter_ = std::make_unique<McpFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

protected:
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  McpFilterConfigSharedPtr config_;
  std::unique_ptr<McpFilter> filter_;

  void setupBufferMocks(const std::string& body) {
    Buffer::OwnedImpl buffer(body);
    ON_CALL(decoder_callbacks_, decodingBuffer()).WillByDefault(Return(&buffer));
    ON_CALL(decoder_callbacks_, addDecodedData(_, _))
        .WillByDefault([&buffer](Buffer::Instance& data, bool) { buffer.move(data); });
  }
};

// Test SSE request detection (GET with Accept: text/event-stream)
TEST_F(McpFilterTest, ValidSseRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"accept", "text/event-stream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Test SSE request with multiple accept values
TEST_F(McpFilterTest, SseRequestWithMultipleAcceptValues) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"},
                                         {"accept", "application/json, text/event-stream, */*"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Test non-SSE GET request passes through in PASS_THROUGH mode
TEST_F(McpFilterTest, NonSseGetRequestPassThrough) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"accept", "text/html"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Test valid MCP POST request headers (should stop iteration to check body)
TEST_F(McpFilterTest, ValidMcpPostHeaders) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  // Should stop to check body for JSON-RPC validation
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test POST request with both accept headers in single value
TEST_F(McpFilterTest, PostWithCombinedAcceptHeader) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json, text/event-stream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test POST request without proper accept headers passes through
TEST_F(McpFilterTest, PostWithoutProperAcceptHeaders) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"},
      {"content-type", "application/json"},
      {"accept", "application/json"}}; // Missing text/event-stream

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Test REJECT_NO_MCP mode - reject non-MCP traffic
TEST_F(McpFilterTest, RejectNoMcpMode) {
  setupRejectMode();

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"accept", "text/html"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest, "Only MCP traffic is allowed", _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test REJECT_NO_MCP mode - allow valid SSE
TEST_F(McpFilterTest, RejectModeAllowsValidSse) {
  setupRejectMode();

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"accept", "text/event-stream"}};

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Test REJECT_NO_MCP mode - reject non-JSON-RPC body
TEST_F(McpFilterTest, RejectModeRejectsNonJsonRpc) {
  setupRejectMode();

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string body = R"({"method": "test"})";
  Buffer::OwnedImpl buffer(body);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "request must be a valid JSON-RPC 2.0 message for MCP", _, _, _));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

// Test per-route override configuration
TEST_F(McpFilterTest, PerRouteOverride) {
  // Setup route-specific config to REJECT_NO_MCP
  envoy::extensions::filters::http::mcp::v3::McpOverride override_config;
  override_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::REJECT_NO_MCP);
  auto route_config = std::make_shared<McpOverrideConfig>(override_config);

  EXPECT_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
      .WillOnce(Return(route_config.get()));

  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {"accept", "text/html"}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest, "Only MCP traffic is allowed", _, _, _));

  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test dynamic metadata is set for valid JSON-RPC
TEST_F(McpFilterTest, DynamicMetadataSet) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  // Expect dynamic metadata to be set
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test buffering behavior for streaming data
TEST_F(McpFilterTest, StreamingDataBuffering) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  Buffer::OwnedImpl buffer("partial data");

  // Not end_stream, should buffer
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));
}

// Test encoder passthrough
TEST_F(McpFilterTest, EncoderPassthrough) {
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}};
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(headers, false));

  Buffer::OwnedImpl buffer("response data");
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(buffer, false));
}

// Test wrong JSON-RPC version
TEST_F(McpFilterTest, WrongJsonRpcVersion) {
  setupRejectMode();

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string wrong_version = R"({"jsonrpc": "1.0", "method": "test", "id": 1})";
  Buffer::OwnedImpl buffer(wrong_version);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::BadRequest, _, _, _, _));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

// Test empty POST body with MCP headers
TEST_F(McpFilterTest, EmptyPostBodyWithMcpHeaders) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  // If end_stream is true in headers, it means empty body
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, true));
}

// Test configuration getters
TEST_F(McpFilterTest, ConfigurationGetters) {
  EXPECT_EQ(envoy::extensions::filters::http::mcp::v3::Mcp::PASS_THROUGH, config_->trafficMode());
  EXPECT_FALSE(config_->shouldRejectNonMcp());

  setupRejectMode();
  EXPECT_EQ(envoy::extensions::filters::http::mcp::v3::Mcp::REJECT_NO_MCP, config_->trafficMode());
  EXPECT_TRUE(config_->shouldRejectNonMcp());
}

// Test POST with wrong content-type
TEST_F(McpFilterTest, PostWithWrongContentType) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "text/plain"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  // Wrong content-type, should pass through
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
}

// Test default max body size configuration
TEST_F(McpFilterTest, DefaultMaxBodySizeIsEightKB) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  // Don't set max_request_body_size, should default to 8KB
  auto config = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  EXPECT_EQ(8192u, config->maxRequestBodySize());
}

// Test custom max body size configuration
TEST_F(McpFilterTest, CustomMaxBodySizeConfiguration) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(16384);
  auto config = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  EXPECT_EQ(16384u, config->maxRequestBodySize());
}

// Test disabled max body size (0 = no limit)
TEST_F(McpFilterTest, DisabledMaxBodySizeConfiguration) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(0);
  auto config = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  EXPECT_EQ(0u, config->maxRequestBodySize());
}

// Test request body under the limit succeeds
TEST_F(McpFilterTest, RequestBodyUnderLimitSucceeds) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(1024); // 1KB limit
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(1024));
  filter_->decodeHeaders(headers, false);

  // Create a JSON-RPC body that's under 1KB
  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test request body exceeding the limit gets 413
TEST_F(McpFilterTest, RequestBodyExceedingLimitRejected) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(100); // Very small limit
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(100));
  filter_->decodeHeaders(headers, false);

  // Create a JSON body that exceeds 100 bytes
  std::string json =
      R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value", "longkey": "this is a very long string to exceed the limit"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::PayloadTooLarge,
                             testing::HasSubstr("Request body size exceeds maximum allowed size"),
                             _, _, "mcp_filter_body_too_large"));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

// Test request body with limit disabled (0 = no limit) allows large bodies
TEST_F(McpFilterTest, RequestBodyWithDisabledLimitAllowsLargeBodies) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(0); // Disable limit
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  // Should NOT call setDecoderBufferLimit when limit is 0
  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(_)).Times(0);
  filter_->decodeHeaders(headers, false);

  // Create a large JSON-RPC body
  std::string large_data(50000, 'x'); // 50KB of data
  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"data": ")" + large_data +
                     R"("}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  // Should succeed even with large body
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test request body exactly at the limit succeeds
TEST_F(McpFilterTest, RequestBodyExactlyAtLimitSucceeds) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(100); // 100 byte limit
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(100));
  filter_->decodeHeaders(headers, false);

  // Create a JSON body that's exactly 100 bytes
  std::string json =
      R"({"jsonrpc": "2.0", "method": "testMethod", "params": {"key": "val"}, "id": 1})"; // 81
                                                                                          // bytes
  // Pad to exactly 100 bytes
  while (json.size() < 100) {
    json.insert(json.size() - 1, " ");
  }
  json = json.substr(0, 100);

  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  // Should NOT be rejected
  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, _)).Times(0);

  // Note: This might fail JSON parsing due to padding, but should not trigger size limit
  filter_->decodeData(buffer, true);
}

// Test that buffer limit is set for valid MCP POST requests
TEST_F(McpFilterTest, BufferLimitSetForValidMcpPostRequest) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(8192);
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(8192));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test that buffer limit is NOT set when limit is disabled
TEST_F(McpFilterTest, BufferLimitNotSetWhenDisabled) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(0); // Disabled
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test body size check in PASS_THROUGH mode
TEST_F(McpFilterTest, BodySizeLimitInPassThroughMode) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::PASS_THROUGH);
  proto_config.mutable_max_request_body_size()->set_value(50); // Small limit
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(50));
  filter_->decodeHeaders(headers, false);

  // Large body should be rejected even in PASS_THROUGH mode
  std::string json =
      R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value with lots of data"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::PayloadTooLarge, _, _, _, "mcp_filter_body_too_large"));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

// Test route cache is NOT cleared by default when metadata is set
TEST_F(McpFilterTest, RouteCacheNotClearedByDefault) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  // Expect dynamic metadata to be set
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  // Expect route cache NOT to be cleared (default behavior)
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test route cache is NOT cleared when clear_route_cache is false
TEST_F(McpFilterTest, RouteCacheNotClearedWhenDisabled) {
  setupWithClearRouteCache(false);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  // Expect dynamic metadata to be set
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  // Expect route cache NOT to be cleared
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache()).Times(0);

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test route cache is cleared when explicitly enabled
TEST_F(McpFilterTest, RouteCacheClearedWhenExplicitlyEnabled) {
  setupWithClearRouteCache(true);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  // Expect dynamic metadata to be set
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  // Expect route cache to be cleared
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test route cache clearing configuration getter
TEST_F(McpFilterTest, ClearRouteCacheConfigGetter) {
  // Default should be false
  EXPECT_FALSE(config_->clearRouteCache());

  // Explicitly set to false
  setupWithClearRouteCache(false);
  EXPECT_FALSE(config_->clearRouteCache());

  // Explicitly set to true
  setupWithClearRouteCache(true);
  EXPECT_TRUE(config_->clearRouteCache());
}

// Test per-route max body size override with smaller limit
TEST_F(McpFilterTest, PerRouteMaxBodySizeSmallerLimit) {
  // Global config with 1024 bytes
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(1024);
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  // Per-route config with smaller limit (100 bytes)
  envoy::extensions::filters::http::mcp::v3::McpOverride override_config;
  override_config.mutable_max_request_body_size()->set_value(100);
  auto route_config = std::make_shared<McpOverrideConfig>(override_config);

  EXPECT_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
      .WillRepeatedly(Return(route_config.get()));

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  // Should use per-route limit of 100 bytes, not global 1024
  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(100));
  filter_->decodeHeaders(headers, false);

  // Create a body that exceeds per-route limit (100) but under global (1024)
  std::string json =
      R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value", "extra": "data to exceed 100 bytes"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));

  // Should be rejected based on per-route limit
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::PayloadTooLarge,
                             testing::HasSubstr("Request body size exceeds maximum allowed size"),
                             _, _, "mcp_filter_body_too_large"));

  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
}

// Test per-route max body size override with larger limit
TEST_F(McpFilterTest, PerRouteMaxBodySizeLargerLimit) {
  // Global config with 100 bytes
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(100);
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  // Per-route config with larger limit (2048 bytes)
  envoy::extensions::filters::http::mcp::v3::McpOverride override_config;
  override_config.mutable_max_request_body_size()->set_value(2048);
  auto route_config = std::make_shared<McpOverrideConfig>(override_config);

  EXPECT_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
      .WillRepeatedly(Return(route_config.get()));

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  // Should use per-route limit of 2048 bytes, not global 100
  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(2048));
  filter_->decodeHeaders(headers, false);

  // Create a body that exceeds global limit (100) but under per-route (2048)
  std::string json =
      R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value", "extra": "data to exceed 100 bytes limit"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  // Should succeed based on per-route limit
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test fallback to global max body size when no per-route override
TEST_F(McpFilterTest, PerRouteMaxBodySizeFallbackToGlobal) {
  // Global config with 512 bytes
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(512);
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  // Per-route config WITHOUT max_request_body_size override (only traffic mode)
  envoy::extensions::filters::http::mcp::v3::McpOverride override_config;
  override_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::PASS_THROUGH);
  auto route_config = std::make_shared<McpOverrideConfig>(override_config);

  EXPECT_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
      .WillRepeatedly(Return(route_config.get()));

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  // Should fallback to global limit of 512 bytes
  EXPECT_CALL(decoder_callbacks_, setDecoderBufferLimit(512));
  filter_->decodeHeaders(headers, false);

  // Create a valid JSON-RPC body under 512 bytes
  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
  Buffer::OwnedImpl decoding_buffer;

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, false))
      .WillOnce([&decoding_buffer](Buffer::Instance& data, bool) { decoding_buffer.move(data); });
  EXPECT_CALL(decoder_callbacks_, decodingBuffer()).WillRepeatedly(Return(&decoding_buffer));
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  // Should succeed based on global limit
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
