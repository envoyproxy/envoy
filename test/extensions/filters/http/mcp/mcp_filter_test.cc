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
    config_ = std::make_shared<McpFilterConfig>(proto_config);
    filter_ = std::make_unique<McpFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  void setupRejectMode() {
    envoy::extensions::filters::http::mcp::v3::Mcp proto_config;
    proto_config.set_traffic_mode(envoy::extensions::filters::http::mcp::v3::Mcp::REJECT_NO_MCP);
    config_ = std::make_shared<McpFilterConfig>(proto_config);
    filter_ = std::make_unique<McpFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

protected:
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

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
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

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
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

  EXPECT_CALL(decoder_callbacks_, addDecodedData(_, true))
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

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
