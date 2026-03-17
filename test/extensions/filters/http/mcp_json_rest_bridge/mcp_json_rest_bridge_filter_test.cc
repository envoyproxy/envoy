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
using testing::Invoke;
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
      }
    )pb");
    config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
    filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  McpJsonRestBridgeFilterConfigSharedPtr config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

TEST_F(McpJsonRestBridgeFilterTest, InitializeRequestReturnsServerInfoLocalResponse) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {":authority", "test-host"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  constexpr absl::string_view kExpectedResponse =
      R"json({"id":0,"jsonrpc":"2.0","result":{"capabilities":{"tools":{"listChanged":false}},"protocolVersion":"2025-11-25","serverInfo":{"name":"test-host","version":"1.0.0"}}})json";

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::OK, Eq(kExpectedResponse), _, _, _));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0", "id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})json");

  // Decoding data triggers parse and handles 'initialize' method, sending local reply.
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, NonMcpPathReturnsContinue) {
  // Request URL not started with /mcp (or query params) should pass through.
  Http::TestRequestHeaderMapImpl headers{{":path", "/mcp/foo"}};

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::Continue);

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":12,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true), Http::FilterDataStatus::Continue);

  EXPECT_THAT(headers.getPathValue(), Eq("/mcp/foo"));
  EXPECT_EQ(nlohmann::json::parse(body.toString()),
            nlohmann::json::parse(R"json({"jsonrpc":"2.0","id":12,"method":"tools/list"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, NotificationsInitializedMethodReturnsAcceptedHttpCode) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_, sendLocalReply(Http::Code::Accepted, Eq(""), _, _,
                                                 Eq("mcp_json_rest_bridge_filter_initialize_ack")))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                          absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        Http::TestResponseHeaderMapImpl response_headers;
        modify_headers(response_headers);
        EXPECT_EQ(response_headers.get(Http::LowerCaseString("content-length"))[0]
                      ->value()
                      .getStringView(),
                  "0");
      }));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","method":"notifications/initialized"})json");

  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, MissingMethodFieldReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             Eq(R"json({"code":-32601,"message":"Missing method field"})json"), _,
                             _, Eq("mcp_json_rest_bridge_filter_method_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0", "id":0})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, UnsupportedMethodReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Http::Code::BadRequest,
          Eq(R"json({"code":-32601,"message":"Method unsupported_method is not supported"})json"),
          _, _, Eq("mcp_json_rest_bridge_filter_method_not_supported")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":0,"method":"unsupported_method"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, NonStringMethodReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Http::Code::BadRequest,
                     Eq(R"json({"code":-32601,"message":"Method field is not a string"})json"), _,
                     _, Eq("mcp_json_rest_bridge_filter_method_not_string")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":0,"method":123})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, MissingIdFieldReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             Eq(R"json({"code":-32600,"message":"Missing ID field"})json"), _, _,
                             Eq("mcp_json_rest_bridge_filter_id_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, IdFieldWithNonNumericStringReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             Eq(R"json({"code":-32600,"message":"Missing ID field"})json"), _, _,
                             Eq("mcp_json_rest_bridge_filter_id_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":"invalid","method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, IdFieldWithFloatReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             Eq(R"json({"code":-32600,"message":"Missing ID field"})json"), _, _,
                             Eq("mcp_json_rest_bridge_filter_id_not_found")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":123.45,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidInputJsonReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::BadRequest,
                             Eq(R"json({"code":-32700,"message":"JSON parse error"})json"), _, _,
                             Eq("mcp_json_rest_bridge_filter_failed_to_parse_json_rpc_request")));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0", "id":123)json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidProtocolVersionParamsReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(
          Http::Code::BadRequest,
          Eq(R"json({"code":-32602,"message":"Missing valid protocolVersion in initialize request"})json"),
          _, _, Eq("mcp_json_rest_bridge_filter_initialize_request_not_valid")));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0", "id":0,"method":"initialize", "params":{"protocolVersion": 123}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, ToolCallRedirectUrlAndBodyToBackend) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {"content-type", "application/json"}};
  Envoy::Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"parent":"projects/test-codelab","key":{"displayName":"display-key"}}}})json");
  headers.setContentLength(request_body.toString().size());

  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(Return(Envoy::Http::RequestHeaderMapOptRef(headers)));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(headers.getPathValue(), StrEq("/v1/projects/test-codelab/apiKeys"));
  EXPECT_THAT(headers.getMethodValue(), StrEq("POST"));
  EXPECT_THAT(headers.getContentLengthValue(),
              StrEq(std::to_string(request_body.toString().size())));
  EXPECT_THAT(headers.getContentTypeValue(), StrEq("application/json"));
  ASSERT_THAT(headers.get(Envoy::Http::CustomHeaders::get().AcceptEncoding), SizeIs(1));
  EXPECT_THAT(
      headers.get(Envoy::Http::CustomHeaders::get().AcceptEncoding)[0]->value().getStringView(),
      StrEq("identity"));
  EXPECT_EQ(nlohmann::json::parse(request_body.toString()),
            nlohmann::json::parse(R"json({"displayName":"display-key"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, ToolCallWithoutHttpRuleBody) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};
  Envoy::Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"list_api_keys","arguments":{"parent":"projects/test-codelab","pageSize":1}}})json");

  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(Return(Envoy::Http::RequestHeaderMapOptRef(headers)));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(headers.getPathValue(), StrEq("/v1/projects/test-codelab/apiKeys?pageSize=1"));
  EXPECT_THAT(headers.getMethodValue(), StrEq("GET"));
  EXPECT_THAT(headers.getContentLengthValue(), StrEq("0"));

  EXPECT_TRUE(request_body.toString().empty());
}

TEST_F(McpJsonRestBridgeFilterTest, ToolCallWithEscapedQueryParamKey) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};
  Envoy::Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"list_api_keys","arguments":{"parent":"projects/test-codelab","page size":1}}})json");

  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(Return(Envoy::Http::RequestHeaderMapOptRef(headers)));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);
  EXPECT_THAT(headers.getPathValue(), StrEq("/v1/projects/test-codelab/apiKeys?page%20size=1"));
  EXPECT_THAT(headers.getMethodValue(), StrEq("GET"));
  EXPECT_THAT(headers.getContentLengthValue(), StrEq("0"));

  EXPECT_TRUE(request_body.toString().empty());
}

TEST_F(McpJsonRestBridgeFilterTest, ToolNameNotFoundReturnsError) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Envoy::Http::Code::BadRequest,
                             Eq(R"json({"code":-32602,"message":"Tool name not found"})json"), _, _,
                             Eq("mcp_json_rest_bridge_filter_tool_name_not_found")));

  Envoy::Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidToolNameReturnsError) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Envoy::Http::Code::BadRequest,
                             Eq(R"json({"code":-32602,"message":"Tool name not found"})json"), _, _,
                             Eq("mcp_json_rest_bridge_filter_tool_name_not_found")));

  // The tool name is not a valid string type.
  Envoy::Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":{}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, UnknownToolReturnsError) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Envoy::Http::Code::BadRequest,
                             Eq(R"json({"code":-32602,"message":"Unknown tool"})json"), _, _,
                             Eq("mcp_json_rest_bridge_filter_unknown_tool")));

  Envoy::Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"unknown_tool","arguments":{}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidToolArgumentsReturnsError) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Envoy::Http::Code::BadRequest,
                             Eq(R"json({"code":-32602,"message":"Invalid tool arguments"})json"), _,
                             _, Eq("mcp_json_rest_bridge_filter_invalid_tool_arguments")));

  Envoy::Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":{"foo":"bar"}}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, ToolArgumentsMustBeObjectReturnsError) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Envoy::Http::Code::BadRequest,
                     Eq(R"json({"code":-32602,"message":"Tool arguments must be an object"})json"),
                     _, _, Eq("mcp_json_rest_bridge_filter_tool_arguments_invalid")));

  Envoy::Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"create_api_key","arguments":123}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, OptionalToolArguments) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", "POST"}};
  Envoy::Buffer::OwnedImpl request_body(
      R"json({"jsonrpc":"2.0","id":123,"method":"tools/call","params":{"name":"get_api_key"}})json");

  EXPECT_CALL(decoder_callbacks_, requestHeaders())
      .WillRepeatedly(Return(Envoy::Http::RequestHeaderMapOptRef(headers)));
  EXPECT_CALL(decoder_callbacks_.downstream_callbacks_, clearRouteCache());

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_EQ(filter_->decodeData(request_body, /*end_stream=*/true),
            Http::FilterDataStatus::Continue);

  EXPECT_THAT(headers.getPathValue(), StrEq("/v1/apiKeys"));
  EXPECT_THAT(headers.getMethodValue(), StrEq("GET"));
  EXPECT_THAT(headers.getContentLengthValue(), StrEq("0"));

  EXPECT_TRUE(request_body.toString().empty());
}

class McpHttpMethodFilterTest : public testing::TestWithParam<std::string> {
public:
  void SetUp() override {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
    filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  McpJsonRestBridgeFilterConfigSharedPtr config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
};

INSTANTIATE_TEST_SUITE_P(NonPostMethods, McpHttpMethodFilterTest,
                         testing::Values("GET", "PUT", "PATCH", "DELETE", "OPTIONS", "CONNECT",
                                         "TRACE"),
                         [](const testing::TestParamInfo<std::string>& info) {
                           return info.param;
                         });

TEST_P(McpHttpMethodFilterTest, NonPostMethodsReturnMethodNotAllowed) {
  Http::TestRequestHeaderMapImpl headers{{":path", "/mcp"}, {":method", GetParam()}};

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::MethodNotAllowed, Eq("Method Not Allowed"), _,
                             Eq(Grpc::Status::WellKnownGrpcStatus::InvalidArgument),
                             Eq("mcp_json_rest_bridge_filter_not_post")));

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
