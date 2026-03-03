#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp" // IWYU pragma: keep

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using testing::_;
using testing::Invoke;

class McpJsonRestBridgeFilterTest : public testing::Test {
public:
  void SetUp() override {
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge proto_config;
    // Set up a basic config
    config_ = std::make_shared<McpJsonRestBridgeFilterConfig>(proto_config);
    filter_ = std::make_unique<McpJsonRestBridgeFilter>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
  }

  McpJsonRestBridgeFilterConfigSharedPtr config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
};

TEST_F(McpJsonRestBridgeFilterTest, InitializeRequestReturnsServerInfoLocalResponse) {
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/mcp"}, {":authority", "test-host"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  constexpr absl::string_view kExpectedResponse =
      R"json({"id":0,"jsonrpc":"2.0","result":{"capabilities":{"tools":{"listChanged":false}},"protocolVersion":"2025-11-25","serverInfo":{"name":"test-host","version":"1.0.0"}}})json";

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::OK, testing::Eq(kExpectedResponse), _, _, _))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

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

  EXPECT_THAT(headers.getPathValue(), testing::StrEq("/mcp/foo"));
  EXPECT_EQ(nlohmann::json::parse(body.toString()),
            nlohmann::json::parse(R"json({"jsonrpc":"2.0","id":12,"method":"tools/list"})json"));
}

TEST_F(McpJsonRestBridgeFilterTest, NotificationsInitializedMethodReturnsAcceptedHttpCode) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};

  // It should return StopIteration because body is needed for POST requests.
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::Accepted, testing::Eq(""), _, _,
                             testing::Eq("mcp_json_rest_bridge_filter_initialize_ack")))
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

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Http::Code::BadRequest,
                     testing::Eq(R"json({"code":-32601,"message":"Missing method field"})json"), _,
                     _, testing::Eq("mcp_json_rest_bridge_filter_method_not_found")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

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
          testing::Eq(
              R"json({"code":-32601,"message":"Method unsupported_method is not supported"})json"),
          _, _, testing::Eq("mcp_json_rest_bridge_filter_method_not_supported")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

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
      sendLocalReply(
          Http::Code::BadRequest,
          testing::Eq(R"json({"code":-32601,"message":"Method field is not a string"})json"), _, _,
          testing::Eq("mcp_json_rest_bridge_filter_method_not_string")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

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
                             testing::Eq(R"json({"code":-32600,"message":"Missing ID field"})json"),
                             _, _, testing::Eq("mcp_json_rest_bridge_filter_id_not_found")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

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
                             testing::Eq(R"json({"code":-32600,"message":"Missing ID field"})json"),
                             _, _, testing::Eq("mcp_json_rest_bridge_filter_id_not_found")))
      .WillOnce(Invoke([](Http::Code, absl::string_view,
                          std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                          absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
        if (modify_headers) {
          Http::TestResponseHeaderMapImpl response_headers;
          modify_headers(response_headers);
        }
      }));

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
                             testing::Eq(R"json({"code":-32600,"message":"Missing ID field"})json"),
                             _, _, testing::Eq("mcp_json_rest_bridge_filter_id_not_found")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

  Buffer::OwnedImpl body(R"json({"jsonrpc":"2.0","id":123.45,"method":"tools/list"})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
            Http::FilterDataStatus::StopIterationNoBuffer);
}

TEST_F(McpJsonRestBridgeFilterTest, InvalidInputJsonReturnsError) {
  Http::TestRequestHeaderMapImpl headers{{":method", "POST"}, {":path", "/mcp"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);

  EXPECT_CALL(
      decoder_callbacks_,
      sendLocalReply(Http::Code::BadRequest,
                     testing::Eq(R"json({"code":-32700,"message":"JSON parse error"})json"), _, _,
                     testing::Eq("mcp_json_rest_bridge_filter_failed_to_parse_json_rpc_request")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

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
          testing::Eq(
              R"json({"code":-32602,"message":"Missing valid protocolVersion in initialize request"})json"),
          _, _, testing::Eq("mcp_json_rest_bridge_filter_initialize_request_not_valid")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

  Buffer::OwnedImpl body(
      R"json({"jsonrpc":"2.0", "id":0,"method":"initialize", "params":{"protocolVersion": 123}})json");
  EXPECT_EQ(filter_->decodeData(body, /*end_stream=*/true),
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
  }

  McpJsonRestBridgeFilterConfigSharedPtr config_;
  std::unique_ptr<McpJsonRestBridgeFilter> filter_;
  Http::MockStreamDecoderFilterCallbacks decoder_callbacks_;
  Http::MockStreamEncoderFilterCallbacks encoder_callbacks_;
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
              sendLocalReply(Http::Code::MethodNotAllowed, testing::Eq("Method Not Allowed"), _,
                             testing::Eq(Grpc::Status::WellKnownGrpcStatus::InvalidArgument),
                             testing::Eq("mcp_json_rest_bridge_filter_not_post")))
      .WillOnce(
          Invoke([](Http::Code, absl::string_view, std::function<void(Http::ResponseHeaderMap&)>,
                    absl::optional<Grpc::Status::GrpcStatus>, absl::string_view) {}));

  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
