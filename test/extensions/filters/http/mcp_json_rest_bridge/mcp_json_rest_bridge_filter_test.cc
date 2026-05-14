#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp" // IWYU pragma: keep
#include "ocpdiag/core/testing/parse_text_proto.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::ocpdiag::testing::ParseTextProtoOrDie;
using testing::_;
using testing::Eq;
using testing::Return;
using testing::SizeIs;
using testing::StrEq;

class McpJsonRestBridgeFilterTest : public testing::Test {
public:
  void SetUp() override {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config =
        ParseTextProtoOrDie(R"pb(
      tool_config {
        tools {
          name: "create_api_key"
          http_rule: {
            post: "/v1/{parent=projects/*}/apiKeys"
            body: "key"
          }
        }
        tools {
          name: "list_api_keys"
          http_rule: {
            get: "/v1/{parent=projects/*}/apiKeys"
          }
        }
        tools {
          name: "get_api_key"
          http_rule: {
            get: "/v1/apiKeys"
          }
        }
        tool_list_http_rule {
          get: "/discovery/v1/service/foo.googleapis.com/mcptools"
        }
      }
    )pb");
    config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
    filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    EXPECT_CALL(decoder_callbacks_, requestHeaders())
        .WillRepeatedly(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    EXPECT_CALL(encoder_callbacks_, responseHeaders())
        .WillRepeatedly(Return(Http::ResponseHeaderMapOptRef(response_headers_)));
  }

  McpJsonRestBridgeFilterConfigSharedPtr config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
};

TEST_F(McpJsonRestBridgeFilterTest, InitializeRequestReturnsServerInfoLocalResponse) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {":authority", "test-host"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Eq(Http::Code::OK),
          StrEq(
              R"json({"id":0,"jsonrpc":"2.0","result":{"capabilities":{"tools":{"listChanged":false}},"protocolVersion":"2025-06-18","serverInfo":{"name":"test-host","version":"1.0.0"}}})json"),
          _, _, _));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})json");

  // Decoding data triggers parse and handles 'initialize' method, sending local reply.
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::Continue);
  Buffer::OwnedImpl response_body("initialize response");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
}

TEST_F(McpJsonRestBridgeFilterTest, NotMcpRequestResponsePassThrough) {
  // Request URL not started with /mcp (or query params) should pass through.
  request_headers_ = {{":path", "/mcp/foo"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::Continue);

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":12,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true), Http::FilterDataStatus::Continue);

  EXPECT_THAT(request_headers_.getPathValue(), StrEq("/mcp/foo"));
  EXPECT_EQ(nlohmann::json::parse(body.toString()),
            nlohmann::json::parse(R"json({"jsonrpc":"2.0","id":12,"method":"tools/list"})json"));

  response_headers_ = {{"content-type", "text/plain"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("text/plain"));

  Buffer::OwnedImpl response_body("test response");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_body.toString(), StrEq("test response"));
}

TEST_F(McpJsonRestBridgeFilterTest, NotificationsInitializedMethodReturnsAcceptedHttpCode) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::Accepted), StrEq(""), _, _,
                             StrEq("mcp_json_rest_bridge_filter_initialize_ack")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","method":"notifications/initialized"})json");

  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/true),
            Http::FilterHeadersStatus::Continue);
}

TEST_F(McpJsonRestBridgeFilterTest, MissingMethodFieldReturnsError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::BadRequest),
                             StrEq(R"json({"code":-32601,"message":"Missing method field"})json"),
                             _, _, StrEq("mcp_json_rest_bridge_filter_method_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":0})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32601,"message":"Missing method field"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32601,"message":"Missing method field"},"id":0,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, UnsupportedMethodReturnsError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Eq(Http::Code::BadRequest),
          StrEq(
              R"json({"code":-32601,"message":"Method unsupported_method is not supported"})json"),
          _, _, StrEq("mcp_json_rest_bridge_filter_method_not_supported")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":0,"method":"unsupported_method"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"code":-32601,"message":"Method unsupported_method is not supported"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32601,"message":"Method unsupported_method is not supported"},"id":0,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, NonStringMethodReturnsError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Eq(Http::Code::BadRequest),
                     StrEq(R"json({"code":-32601,"message":"Method field is not a string"})json"),
                     _, _, StrEq("mcp_json_rest_bridge_filter_method_not_string")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":0,"method":123})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"code":-32601,"message":"Method field is not a string"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32601,"message":"Method field is not a string"},"id":0,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, MissingIdFieldReturnsError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::BadRequest),
                             StrEq(R"json({"code":-32600,"message":"Missing ID field"})json"), _, _,
                             StrEq("mcp_json_rest_bridge_filter_id_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32600,"message":"Missing ID field"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32600,"message":"Missing ID field"},"id":null,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, IdFieldWithNonNumericStringIsAccepted) {
  // Now that string IDs are valid, a non-numeric string ID should pass validation.
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":"string_id","method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/discovery/v1/service/foo.googleapis.com/mcptools"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"tools":[{"name":"google.api.CreateApiKey"}]})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":"string_id","result":{"tools":[{"name":"google.api.CreateApiKey"}]}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, IdFieldWithFloatReturnsError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::BadRequest),
                             StrEq(R"json({"code":-32600,"message":"Missing ID field"})json"), _, _,
                             StrEq("mcp_json_rest_bridge_filter_id_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":123.45,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32600,"message":"Missing ID field"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32600,"message":"Missing ID field"},"id":null,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidInputJsonReturnsError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Eq(Http::Code::BadRequest),
                     StrEq(R"json({"code":-32700,"message":"JSON parse error"})json"), _, _,
                     StrEq("mcp_json_rest_bridge_filter_failed_to_parse_json_rpc_request")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":123)json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32700,"message":"JSON parse error"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32700,"message":"JSON parse error"},"id":null,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidProtocolVersionParamsReturnsError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Eq(Http::Code::BadRequest),
          StrEq(
              R"json({"code":-32602,"message":"Missing valid protocolVersion in initialize request"})json"),
          _, _, StrEq("mcp_json_rest_bridge_filter_initialize_request_not_valid")));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":123}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"code":-32602,"message":"Missing valid protocolVersion in initialize request"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Missing valid protocolVersion in initialize request"},"id":0,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolCallRedirectUrlAndBodyToBackendResponseRewriteToJsonRpc) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"parent":"projects/test-codelab","key":{"displayName":"display-key"}}}})json");
  request_headers_.setContentLength(request_body.toString().size());

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(), StrEq("/v1/projects/test-codelab/apiKeys"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("POST"));
  EXPECT_THAT(request_headers_.getContentLengthValue(),
              StrEq(std::to_string(request_body.toString().size())));
  EXPECT_THAT(request_headers_.getContentTypeValue(), StrEq("application/json"));
  ASSERT_THAT(request_headers_.get(Http::CustomHeaders::get().AcceptEncoding), SizeIs(1));
  EXPECT_THAT(
      request_headers_.get(Http::CustomHeaders::get().AcceptEncoding)[0]->value().getStringView(),
      StrEq("identity"));
  EXPECT_EQ(nlohmann::json::parse(request_body.toString()),
            nlohmann::json::parse(R"json({"displayName":"display-key"})json"));

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"displayName":"display-key","createTime":"1970-01-01T00:00:22Z"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":123,"result":{"content":[{"text":"{\"displayName\":\"display-key\",\"createTime\":\"1970-01-01T00:00:22Z\"}","type":"text"}],"isError":false}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolCallWithoutHttpRuleBody) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"list_api_keys","arguments":{"parent":"projects/test-codelab","pageSize":1}}})json");

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/v1/projects/test-codelab/apiKeys?pageSize=1"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));
  EXPECT_THAT(request_headers_.getContentLengthValue(), StrEq("0"));

  EXPECT_TRUE(request_body.toString().empty());
}

TEST_F(McpJsonRestBridgeFilterTest, ToolCallWithEscapedQueryParamKey) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"list_api_keys","arguments":{"parent":"projects/test-codelab","page size":1}}})json");

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/v1/projects/test-codelab/apiKeys?page%20size=1"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));
  EXPECT_THAT(request_headers_.getContentLengthValue(), StrEq("0"));

  EXPECT_TRUE(request_body.toString().empty());
}

TEST_F(McpJsonRestBridgeFilterTest, ToolNameNotFoundReturnsError) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::BadRequest),
                             StrEq(R"json({"code":-32602,"message":"Tool name not found"})json"), _,
                             _, StrEq("mcp_json_rest_bridge_filter_tool_name_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32602,"message":"Tool name not found"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Tool name not found"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidToolNameReturnsError) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::BadRequest),
                             StrEq(R"json({"code":-32602,"message":"Tool name not found"})json"), _,
                             _, StrEq("mcp_json_rest_bridge_filter_tool_name_not_found")));

  // The tool name is not a valid string type.
  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":{}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32602,"message":"Tool name not found"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Tool name not found"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, UnknownToolReturnsError) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::BadRequest),
                             StrEq(R"json({"code":-32602,"message":"Unknown tool"})json"), _, _,
                             StrEq("mcp_json_rest_bridge_filter_unknown_tool")));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"unknown_tool","arguments":{}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32602,"message":"Unknown tool"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Unknown tool"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidToolArgumentsReturnsError) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::BadRequest),
                             StrEq(R"json({"code":-32602,"message":"Invalid tool arguments"})json"),
                             _, _, StrEq("mcp_json_rest_bridge_filter_invalid_tool_arguments")));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"foo":"bar"}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"code":-32602,"message":"Invalid tool arguments"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Invalid tool arguments"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolArgumentsMustBeObjectReturnsError) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(
                  Eq(Http::Code::BadRequest),
                  StrEq(R"json({"code":-32602,"message":"Tool arguments must be an object"})json"),
                  _, _, StrEq("mcp_json_rest_bridge_filter_tool_arguments_invalid")));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":123}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"code":-32602,"message":"Tool arguments must be an object"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Tool arguments must be an object"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, OptionalToolArguments) {
  request_headers_ = {{":path", "/mcp"}, {":method", "POST"}};
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"get_api_key"}})json");

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  EXPECT_THAT(request_headers_.getPathValue(), StrEq("/v1/apiKeys"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));
  EXPECT_THAT(request_headers_.getContentLengthValue(), StrEq("0"));

  EXPECT_TRUE(request_body.toString().empty());

  response_headers_ = {{":status", "200"}, {"content-type", "application/json"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"name":"projects/test/apiKeys/123"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"id":123,"jsonrpc":"2.0","result":{"content":[{"text":"{\"name\":\"projects/test/apiKeys/123\"}","type":"text"}],"isError":false}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, BackendErrorReturnsToolCallError) {
  request_headers_ = {{":path", "/mcp"}};

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"parent":"projects/test-codelab","key":{"displayName":"display-key"}}}})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(), StrEq("/v1/projects/test-codelab/apiKeys"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("POST"));
  EXPECT_EQ(nlohmann::json::parse(request_body.toString()),
            nlohmann::json::parse(R"json({"displayName":"display-key"})json"));

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "500"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body("Server error");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"id":123,"jsonrpc":"2.0","result":{"content":[{"text":"Server error","type":"text"}],"isError":true}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, RejectInvalidUtf8BackendResponse) {
  request_headers_ = {{":path", "/mcp"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"parent":"projects/test-codelab","key":{"displayName":"display-key"}}}})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  // "\xF1" is an invalid UTF-8 start byte.
  Buffer::OwnedImpl response_body("this-is-invalid-\xF1");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"id":123,"jsonrpc":"2.0","result":{"content":[{"text":"Backend response returns an invalid UTF-8 payload.","type":"text"}],"isError":true}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolListRewritePathForRequestAndTranslateResponse) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  // Assumes the request body is split into different parts.
  Buffer::OwnedImpl request_first_body(R"json({"jsonrpc":"2.0")json");
  EXPECT_EQ(filter_->decodeData(request_first_body, /*end_stream=*/false),
            Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl request_second_body(R"json(,"id":12,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(request_second_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/discovery/v1/service/foo.googleapis.com/mcptools"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"tools":[{"name":"google.api.CreateApiKey"}]})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":12,"result":{"tools":[{"name":"google.api.CreateApiKey"}]}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, EncodeDataReturnsStopIterationNoBufferWhenNotEndOfStream) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"parent":"projects/test-codelab","key":{"displayName":"display-key"}}}})json");
  request_headers_.setContentLength(request_body.toString().size());

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  response_headers_ = {
      {":status", "200"}, {"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl first_chunk("part1");
  EXPECT_EQ(filter_->encodeData(first_chunk, /*end_stream=*/false),
            Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_TRUE(first_chunk.toString().empty());
  Buffer::OwnedImpl second_chunk("part2");
  EXPECT_EQ(filter_->encodeData(second_chunk, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(second_chunk.length())));
  EXPECT_EQ(
      nlohmann::json::parse(second_chunk.toString()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":123,"result":{"content":[{"text":"part1part2","type":"text"}],"isError":false}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolListWithNumericStringId) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl request_body(R"json({"jsonrpc":"2.0","id":"12","method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/discovery/v1/service/foo.googleapis.com/mcptools"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"tools":[{"name":"google.api.CreateApiKey"}]})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":"12","result":{"tools":[{"name":"google.api.CreateApiKey"}]}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ErrorToolListResponseReturnsServerError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl request_body(R"json({"jsonrpc":"2.0","id":123,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "500"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body("Some internal error");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32000,"message":"Server error"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidToolListResponseReturnsServerError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl request_body(R"json({"jsonrpc":"2.0","id":123,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"foo")json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32000,"message":"Server error"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidResponseStatusCodeReturnsServerError) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl request_body(R"json({"jsonrpc":"2.0","id":123,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "invalid"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body("");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32000,"message":"Server error"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, QueryParamsFromMcpPathIsIgnored) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp?foo=bar"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl request_body(R"json({"jsonrpc":"2.0","id":12,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/discovery/v1/service/foo.googleapis.com/mcptools"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));

  response_headers_ = {
      {"content-type", "application/json"}, {"content-length", "123456"}, {":status", "200"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(R"json({"tools":[{"name":"google.api.CreateApiKey"}]})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"jsonrpc":"2.0","id":12,"result":{"tools":[{"name":"google.api.CreateApiKey"}]}})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolCallWithTransferEncodingChunkedRemovesContentLength) {
  // 1. Request Path
  request_headers_ = {{":method", "POST"},
                      {":path", "/mcp"},
                      {"content-type", "application/json"},
                      {"transfer-encoding", "chunked"}};
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"parent":"projects/test-codelab","key":{"displayName":"display-key"}}}})json");

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  EXPECT_THAT(request_headers_.getPathValue(), StrEq("/v1/projects/test-codelab/apiKeys"));
  EXPECT_FALSE(request_headers_.has(Http::Headers::get().ContentLength));

  // 2. Response Path
  response_headers_ = {{"content-type", "application/json"},
                       {"content-length", "123456"},
                       {":status", "200"},
                       {"transfer-encoding", "chunked"}};

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl response_body(R"json({"displayName":"display-key"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_FALSE(response_headers_.has(Http::Headers::get().ContentLength));
}

TEST_F(McpJsonRestBridgeFilterTest, ProtocolVersionFallsBackToLatestSupportedWhenNotSupported) {
  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {":authority", "test-host"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Eq(Http::Code::OK),
          StrEq(
              R"json({"id":0,"jsonrpc":"2.0","result":{"capabilities":{"tools":{"listChanged":false}},"protocolVersion":"2025-11-25","serverInfo":{"name":"test-host","version":"1.0.0"}}})json"),
          _, _, _));
  // Assumes the protocolVersion is not supported.
  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"unsupported-version"}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::Continue);
  Buffer::OwnedImpl response_body("initialize response");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
}

TEST_F(McpJsonRestBridgeFilterTest,
       NotificationsInitializedUnsupportedProtocolVersionReturnsBadRequest) {
  request_headers_ = {{":path", "/mcp"}, {"mcp-protocol-version", "unsupported-version"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Eq(Http::Code::BadRequest),
                     StrEq(R"json({"code":-32602,"message":"Unsupported protocol version"})json"),
                     _, _, StrEq("mcp_json_rest_bridge_filter_unsupported_protocol_version")));
  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","method":"notifications/initialized"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"code":-32602,"message":"Unsupported protocol version"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Unsupported protocol version"},"id":null,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest,
       NotificationsInitializedSupportedProtocolVersionProcessRequest) {
  request_headers_ = {{":path", "/mcp"}, {"mcp-protocol-version", "2025-11-25"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::Accepted), StrEq(""), _, _,
                             StrEq("mcp_json_rest_bridge_filter_initialize_ack")));
  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","method":"notifications/initialized"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/true),
            Http::FilterHeadersStatus::Continue);
}

TEST_F(McpJsonRestBridgeFilterTest, ToolsListUnsupportedProtocolVersionReturnsBadRequest) {
  request_headers_ = {{":path", "/mcp"}, {"mcp-protocol-version", "unsupported-version"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Eq(Http::Code::BadRequest),
                     StrEq(R"json({"code":-32602,"message":"Unsupported protocol version"})json"),
                     _, _, StrEq("mcp_json_rest_bridge_filter_unsupported_protocol_version")));
  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":123,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"code":-32602,"message":"Unsupported protocol version"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Unsupported protocol version"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolsListSupportedProtocolVersionProcessRequest) {
  request_headers_ = {{":path", "/mcp"}, {"mcp-protocol-version", "2025-11-25"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":123,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true), Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/discovery/v1/service/foo.googleapis.com/mcptools"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolsCallUnsupportedProtocolVersionReturnsBadRequest) {
  request_headers_ = {{":path", "/mcp"}, {"mcp-protocol-version", "unsupported-version"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Eq(Http::Code::BadRequest),
                     StrEq(R"json({"code":-32602,"message":"Unsupported protocol version"})json"),
                     _, _, StrEq("mcp_json_rest_bridge_filter_unsupported_protocol_version")));
  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"list_api_keys","arguments":{"parent":"projects/test-codelab","pageSize":1}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  response_headers_ = {{"content-type", "text/plain"}, {"content-length", "123456"}};
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  Buffer::OwnedImpl response_body(
      R"json({"code":-32602,"message":"Unsupported protocol version"})json");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(response_headers_.getContentTypeValue(), StrEq("application/json"));
  EXPECT_THAT(response_headers_.getContentLengthValue(),
              StrEq(std::to_string(response_body.length())));
  EXPECT_EQ(
      nlohmann::json::parse(response_body.toString()),
      nlohmann::json::parse(
          R"json({"error":{"code":-32602,"message":"Unsupported protocol version"},"id":123,"jsonrpc":"2.0"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolsCallSupportedProtocolVersionProcessRequest) {
  request_headers_ = {{":path", "/mcp"}, {"mcp-protocol-version", "2025-11-25"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"list_api_keys","arguments":{"parent":"projects/test-codelab","pageSize":1}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true), Http::FilterDataStatus::Continue);
  EXPECT_THAT(request_headers_.getPathValue(),
              StrEq("/v1/projects/test-codelab/apiKeys?pageSize=1"));
  EXPECT_THAT(request_headers_.getMethodValue(), StrEq("GET"));
}

TEST_F(McpJsonRestBridgeFilterTest, InitializeRequestIgnoreProtocolVersionHeader) {
  // The protocol version header is not checked for initialize request.
  request_headers_ = {{":path", "/mcp"},
                      {"mcp-protocol-version", "unsupported-version"},
                      {":authority", "test-host"}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Eq(Http::Code::OK),
          StrEq(
              R"json({"id":0,"jsonrpc":"2.0","result":{"capabilities":{"tools":{"listChanged":false}},"protocolVersion":"2025-06-18","serverInfo":{"name":"test-host","version":"1.0.0"}}})json"),
          _, _, _));
  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);

  // Simulates how the router filter handles the local response.
  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::Continue);
  Buffer::OwnedImpl response_body("initialize response");
  EXPECT_EQ(filter_->encodeData(response_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
}

TEST_F(McpJsonRestBridgeFilterTest, RequestBodyExceedsLimitReturnsError) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config =
      ParseTextProtoOrDie(R"pb(
    max_request_body_size { value: 10 }
  )pb");
  config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
  filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);

  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}};

  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::PayloadTooLarge), _, _, _,
                             StrEq("mcp_json_rest_bridge_filter_request_too_large")));
  Buffer::OwnedImpl body("12345678901"); // 11 bytes
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, ResponseBodyExceedsLimitReturnsError) {
  envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config =
      ParseTextProtoOrDie(R"pb(
    max_response_body_size { value: 10 }
    tool_config {
      tools {
        name: "create_api_key"
        http_rule: {
          post: "/v1/{parent=projects/*}/apiKeys"
          body: "key"
        }
      }
    }
  )pb");
  config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
  filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
  filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  filter_->setEncoderFilterCallbacks(encoder_callbacks_);

  request_headers_ = {{":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};
  Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"parent":"projects/test","key":{"displayName":"display-key"}}}})json");
  request_headers_.setContentLength(request_body.toString().size());

  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  EXPECT_EQ(filter_->encodeHeaders(response_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_CALL(
      encoder_callbacks_,
      sendLocalReply(
          Eq(Http::Code::InternalServerError),
          StrEq(
              R"json({"error":{"code":-32000,"message":"Response body too large"},"id":123,"jsonrpc":"2.0"})json"),
          _, _, StrEq("mcp_json_rest_bridge_filter_response_too_large")));
  Buffer::OwnedImpl body("12345678901"); // 11 bytes
  EXPECT_EQ(filter_->encodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

class McpHttpMethodFilterTest : public testing::TestWithParam<std::string> {
public:
  void SetUp() override {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
    filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    EXPECT_CALL(decoder_callbacks_, requestHeaders())
        .WillRepeatedly(Return(Http::RequestHeaderMapOptRef(request_headers_)));
    EXPECT_CALL(encoder_callbacks_, responseHeaders())
        .WillRepeatedly(Return(Http::ResponseHeaderMapOptRef(response_headers_)));
  }

  McpJsonRestBridgeFilterConfigSharedPtr config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestResponseHeaderMapImpl response_headers_;
};

INSTANTIATE_TEST_SUITE_P(NonPostMethods, McpHttpMethodFilterTest,
                         testing::Values("GET", "PUT", "PATCH", "DELETE", "OPTIONS", "CONNECT",
                                         "TRACE"),
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

TEST_P(McpHttpMethodFilterTest, NonPostMethodsReturnMethodNotAllowed) {
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Eq(Http::Code::MethodNotAllowed), StrEq("Method Not Allowed"), _,
                             Eq(Grpc::Status::WellKnownGrpcStatus::InvalidArgument),
                             StrEq("mcp_json_rest_bridge_filter_not_post")));
  Http::TestResponseHeaderMapImpl additional_response_headers;
  EXPECT_CALL(decoder_callbacks_, encodeHeaders_)
      .WillOnce(testing::SaveArg<0>(&additional_response_headers));
  request_headers_ = {{":path", "/mcp"}, {":method", GetParam()}};
  EXPECT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  ASSERT_THAT(additional_response_headers.get(Http::LowerCaseString("allow")), SizeIs(1));
  EXPECT_THAT(
      additional_response_headers.get(Http::LowerCaseString("allow"))[0]->value().getStringView(),
      StrEq("POST"));
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
