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

  std::string json =
      R"({"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "test"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _))
      .WillOnce([&](const std::string&, const Protobuf::Struct& metadata) {
        const auto& fields = metadata.fields();

        auto jsonrpc_it = fields.find("jsonrpc");
        ASSERT_NE(jsonrpc_it, fields.end());
        EXPECT_EQ(jsonrpc_it->second.string_value(), "2.0");

        auto method_it = fields.find("method");
        ASSERT_NE(method_it, fields.end());
        EXPECT_EQ(method_it->second.string_value(), "tools/call");

        auto params_it = fields.find("params");
        ASSERT_NE(params_it, fields.end());
        const auto& params = params_it->second.struct_value().fields();

        auto name_it = params.find("name");
        ASSERT_NE(name_it, params.end());
        EXPECT_EQ(name_it->second.string_value(), "test");
      });

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test buffering behavior for streaming data
TEST_F(McpFilterTest, PartialNoJsonData) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  Buffer::OwnedImpl buffer("partial data");

  // Not end_stream, should buffer
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(buffer, true));
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

  EXPECT_CALL(decoder_callbacks_, setBufferLimit(1024));
  filter_->decodeHeaders(headers, false);

  // Create a JSON-RPC body that's under 1KB
  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test request body exceeding, but it will continue since we get the enough data.
TEST_F(McpFilterTest, RequestBodyExceedingLimitContinues) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(100); // Very small limit
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setBufferLimit(100));
  filter_->decodeHeaders(headers, false);

  // Create a JSON body that exceeds 100 bytes
  std::string json =
      R"({"jsonrpc": "2.0", "method": "test", "id": 1, "params": {"key": "value", "longkey": "this is a very long string to exceed the limit"}})";
  Buffer::OwnedImpl buffer(json);
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test request body exceeding limit when there is not enough data.
TEST_F(McpFilterTest, RequestBodyExceedingLimitRejectWhenNotEnoughData) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_max_request_body_size()->set_value(20); // Very small limit
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  EXPECT_CALL(decoder_callbacks_, setBufferLimit(20));
  filter_->decodeHeaders(headers, false);

  // Create a JSON body that exceeds 20 bytes but is incomplete
  std::string json = R"({"jsonrpc": "2.0", "me)";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "reached end_stream or configured body size, don't get enough data.",
                             _, _, _));

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

  // Should NOT call setBufferLimit when limit is 0
  EXPECT_CALL(decoder_callbacks_, setBufferLimit(_)).Times(0);
  filter_->decodeHeaders(headers, false);

  // Create a large JSON-RPC body
  std::string large_data(50000, 'x'); // 50KB of data
  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"data": ")" + large_data +
                     R"("}, "id": 1})";
  Buffer::OwnedImpl buffer(json);
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

  EXPECT_CALL(decoder_callbacks_, setBufferLimit(100));
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

  EXPECT_CALL(decoder_callbacks_, setBufferLimit(8192));
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

  EXPECT_CALL(decoder_callbacks_, setBufferLimit(_)).Times(0);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test body size check in PASS_THROUGH mode - reject when required fields are beyond the limit
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

  EXPECT_CALL(decoder_callbacks_, setBufferLimit(50));
  filter_->decodeHeaders(headers, false);

  // JSON body with required fields (jsonrpc, method, id) in the first 50 bytes.
  std::string json =
      R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value with lots of data"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             "reached end_stream or configured body size, don't get enough data.",
                             _, _, _));
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

TEST_F(McpFilterTest, FilterWithCustomParserConfig) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::PASS_THROUGH);

  // Add custom parser config
  auto* parser_config = proto_config.mutable_parser_config();
  auto* method_rule = parser_config->add_methods();
  method_rule->set_method("custom/method");
  method_rule->add_extraction_rules()->set_path("params.custom_field");

  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string json = R"({
    "jsonrpc": "2.0",
    "method": "custom/method",
    "params": {
      "custom_field": "extracted_value",
      "other_field": "ignored"
    },
    "id": 1
  })";
  Buffer::OwnedImpl buffer(json);

  // Expect dynamic metadata to be set with the custom field
  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _))
      .WillOnce([&](const std::string&, const Protobuf::Struct& metadata) {
        const auto& fields = metadata.fields();
        auto it = fields.find("params");
        ASSERT_NE(it, fields.end());
        const auto& params = it->second.struct_value().fields();

        // Custom field should be extracted
        auto custom_it = params.find("custom_field");
        ASSERT_NE(custom_it, params.end());
        EXPECT_EQ(custom_it->second.string_value(), "extracted_value");

        // Other field should not be extracted
        EXPECT_EQ(params.find("other_field"), params.end());
      });

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test that extra data is ignored after parsing is complete
TEST_F(McpFilterTest, ParsingCompleteIgnoresExtraData) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  // Send a complete JSON-RPC request
  std::string json = R"({"jsonrpc": "2.0", "method": "test", "params": {"key": "value"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);

  // Should complete parsing and return Continue
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, false));

  // Send more data
  Buffer::OwnedImpl extra_buffer("extra data");
  // Should return Continue immediately because parsing is complete
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(extra_buffer, true));
}

// Test that partial valid JSON returns StopIterationAndBuffer
TEST_F(McpFilterTest, PartialValidJsonBuffers) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  // Send partial JSON with a method that requires params (tools/call requires params.name)
  // This ensures early stop is not triggered immediately.
  std::string json = R"({"jsonrpc": "2.0", "method": "tools/call")";
  Buffer::OwnedImpl buffer(json);

  // Should buffer and wait for more data
  EXPECT_EQ(Http::FilterDataStatus::StopIterationAndBuffer, filter_->decodeData(buffer, false));
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
  EXPECT_CALL(decoder_callbacks_, setBufferLimit(100));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
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
  EXPECT_CALL(decoder_callbacks_, setBufferLimit(2048));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
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
  EXPECT_CALL(decoder_callbacks_, setBufferLimit(512));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration, filter_->decodeHeaders(headers, false));
}

// Test method group added to dynamic metadata when configured
TEST_F(McpFilterTest, MethodGroupAddedToMetadata) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  proto_config.mutable_parser_config()->set_group_metadata_key("method_group");
  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string json =
      R"({"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "test"}, "id": 1})";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _))
      .WillOnce([&](const std::string&, const Protobuf::Struct& metadata) {
        const auto& fields = metadata.fields();

        // Check method_group is set to "tool" (built-in group for tools/call)
        auto group_it = fields.find("method_group");
        ASSERT_NE(group_it, fields.end());
        EXPECT_EQ(group_it->second.string_value(), "tool");

        // Check method is also set
        auto method_it = fields.find("method");
        ASSERT_NE(method_it, fields.end());
        EXPECT_EQ(method_it->second.string_value(), "tools/call");
      });

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

// Test method group with custom override
TEST_F(McpFilterTest, MethodGroupWithCustomOverride) {
  envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
  auto* parser_config = proto_config.mutable_parser_config();
  parser_config->set_group_metadata_key("group");

  auto* method_config = parser_config->add_methods();
  method_config->set_method("tools/list");
  method_config->set_group("custom_tools");

  config_ = std::make_shared<McpFilterConfig>(proto_config, "test.", factory_context_.scope());
  filter_ = std::make_unique<McpFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);

  Http::TestRequestHeaderMapImpl headers{{":method", "POST"},
                                         {"content-type", "application/json"},
                                         {"accept", "application/json"},
                                         {"accept", "text/event-stream"}};

  filter_->decodeHeaders(headers, false);

  std::string json = R"({"jsonrpc": "2.0", "method": "tools/list", "id": 1})";
  Buffer::OwnedImpl buffer(json);

  EXPECT_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata("mcp_proxy", _))
      .WillOnce([&](const std::string&, const Protobuf::Struct& metadata) {
        const auto& fields = metadata.fields();
        auto group_it = fields.find("group");
        ASSERT_NE(group_it, fields.end());
        EXPECT_EQ(group_it->second.string_value(), "custom_tools");
      });

  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(buffer, true));
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
