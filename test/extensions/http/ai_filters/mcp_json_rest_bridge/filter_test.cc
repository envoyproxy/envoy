#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/stream_info/stream_id_provider_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"
#include "source/extensions/http/ai_filters/mcp_json_rest_bridge/filter.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace McpJsonRestBridge {
namespace {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiFilterSharedPtr;
using HttpFilters::AiProtocolManager::BufferManager;
using HttpFilters::AiProtocolManager::FakeBridge;
using HttpFilters::AiProtocolManager::FilterManager;
using HttpFilters::AiProtocolManager::InMemoryExternalBufferFactory;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LocalReply;
using HttpFilters::AiProtocolManager::ResponseMode;
using HttpFilters::AiProtocolManager::UnparsedRequest;
using BridgeProto = envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge;
using BridgePerRouteProto =
    envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridgePerRoute;
using nlohmann::json;
using testing::IsEmpty;

constexpr char FilterName[] = "mcp_bridge";

constexpr char DefaultConfig[] = R"yaml(
request_storage_mode: DYNAMIC_METADATA
tool_config:
  tools:
  - name: create_api_key
    http_rule:
      post: "/v1/{parent=projects/*}/apiKeys"
      body: key
  - name: get_api_key
    http_rule:
      get: "/v1/apiKeys"
  - name: stream_logs
    text_content_streaming_enabled: true
    http_rule:
      get: "/v1/logs"
)yaml";

constexpr char CreateApiKey[] = R"json({
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "create_api_key",
    "arguments": {"parent": "projects/p", "key": {"displayName": "k"}}
  }
})json";

std::string toolsCall(absl::string_view tool) {
  return json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"}, {"params", {{"name", tool}}}}
      .dump();
}

class McpJsonRestBridgeFilterTest : public testing::Test {
public:
  McpJsonRestBridgeFilterTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_), buffer_manager_(BufferManager::Config{}, factory_, bridge_),
        stream_info_(api_->timeSource(), nullptr, StreamInfo::FilterState::LifeSpan::FilterChain) {}

  ~McpJsonRestBridgeFilterTest() override { buffer_manager_.onDestroy(); }

  // Creates the filter and the chain that holds it, as the manager does once a request's body
  // has arrived.
  void makeFilter(const std::string& config = DefaultConfig, uint64_t request_payload_bytes = 0) {
    BridgeProto proto;
    TestUtility::loadFromYaml(config, proto);
    auto bridge_config = Bridge::McpJsonRestBridgeFilterConfig::create(proto);
    ASSERT_TRUE(bridge_config.ok()) << bridge_config.status();
    filter_ = std::make_shared<McpJsonRestBridgeFilter>(
        *bridge_config, AiFilterContext{.stream_info = stream_info_,
                                        .request_headers = request_headers_,
                                        .request_protocol = LLMProtocol::Unspecified,
                                        .request_payload_bytes = request_payload_bytes,
                                        .filter_name = FilterName,
                                        .route_config = route_config_,
                                        .clear_route_cache = [this] { ++route_cache_clears_; }});
    manager_ = std::make_unique<FilterManager>(std::vector<AiFilterSharedPtr>{filter_});
  }

  void drain() {
    for (int i = 0; i < 20; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Runs the request chain over `doc`, and returns the body it forwards.
  std::string decode(JsonWithExtBuf doc) {
    manager_->startRequest(
        std::move(doc), &buffer_manager_, *dispatcher_, stream_info_,
        [this](absl::Status status) { request_status_ = std::move(status); }, &request_headers_,
        [this](LocalReply reply) { local_reply_ = std::move(reply); });
    drain();
    return bridge_.injected_.toString();
  }

  std::string decode(const std::string& payload) {
    JsonWithExtBuf doc;
    doc.setJson(json::parse(payload));
    return decode(std::move(doc));
  }

  // Runs the raw response chain over `chunks`, and returns the body it forwards.
  std::string encode(const std::vector<std::string>& chunks) {
    FakeBridge bridge(*dispatcher_);
    BufferManager out(BufferManager::Config{}, factory_, bridge);
    manager_->startRawResponse(
        factory_, bridge, out,
        [this](absl::Status status) { response_status_ = std::move(status); },
        [this](LocalReply reply) { local_reply_ = std::move(reply); });
    for (const std::string& chunk : chunks) {
      Buffer::OwnedImpl data(chunk);
      manager_->onResponseData(data, /*end_stream=*/false);
      drain();
    }
    Buffer::OwnedImpl empty;
    manager_->onResponseData(empty, /*end_stream=*/true);
    drain();
    const std::string body = bridge.injected_.toString();
    out.onDestroy();
    return body;
  }

  // The headers `local_reply_` leaves with.
  Http::TestResponseHeaderMapImpl localReplyHeaders() {
    Http::TestResponseHeaderMapImpl headers;
    if (local_reply_->modify_headers != nullptr) {
      local_reply_->modify_headers(headers);
    }
    return headers;
  }

  json dynamicMetadata() {
    return json::parse(MessageUtil::getJsonStringFromMessageOrError(
        stream_info_.dynamicMetadata().filter_metadata().at(FilterName)));
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  FakeBridge bridge_;
  BufferManager buffer_manager_;
  StreamInfo::StreamInfoImpl stream_info_;
  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "POST"}, {":path", "/mcp"}, {":authority", "host"}, {"content-length", "100"}};
  Http::TestResponseHeaderMapImpl response_headers_{{":status", "200"},
                                                    {"content-type", "application/json"}};
  Router::RouteSpecificFilterConfigConstSharedPtr route_config_;
  int route_cache_clears_{0};

  std::shared_ptr<McpJsonRestBridgeFilter> filter_;
  std::unique_ptr<FilterManager> manager_;
  absl::Status request_status_{absl::UnknownError("the request chain has not completed")};
  absl::Status response_status_{absl::UnknownError("the response chain has not completed")};
  std::optional<LocalReply> local_reply_;
};

TEST_F(McpJsonRestBridgeFilterTest, LeavesOtherRequestsAlone) {
  request_headers_.setPath("/v1/apiKeys");
  makeFilter();

  EXPECT_EQ(json::parse(decode(R"({"a":1})")), json::parse(R"({"a":1})"));
  EXPECT_TRUE(request_status_.ok()) << request_status_;
  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_EQ(route_cache_clears_, 0);

  // Nor does it take the response.
  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/false),
            ResponseMode::Parsed);
  EXPECT_EQ(encode({R"({"keys":[]})"}), R"({"keys":[]})");
  EXPECT_TRUE(response_status_.ok()) << response_status_;
}

TEST_F(McpJsonRestBridgeFilterTest, AnswersInitialize) {
  makeFilter();

  EXPECT_THAT(decode(R"({"jsonrpc":"2.0","id":0,"method":"initialize",
                         "params":{"protocolVersion":"2025-06-18"}})"),
              IsEmpty());

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::OK);
  EXPECT_EQ(local_reply_->details, "mcp_json_rest_bridge_filter_initialize");
  EXPECT_EQ(local_reply_->grpc_status, Grpc::Status::WellKnownGrpcStatus::Ok);
  EXPECT_EQ(localReplyHeaders().getContentTypeValue(), "application/json");
  const json body = json::parse(local_reply_->body);
  EXPECT_EQ(body["id"], 0);
  EXPECT_EQ(body["result"]["protocolVersion"], "2025-06-18");
  EXPECT_EQ(body["result"]["serverInfo"]["name"], "host");
  EXPECT_EQ(dynamicMetadata()["method"], "initialize");
}

TEST_F(McpJsonRestBridgeFilterTest, MapsToolsCallOntoTheBackendRequest) {
  makeFilter();

  EXPECT_EQ(json::parse(decode(CreateApiKey)), json::parse(R"({"displayName":"k"})"));

  EXPECT_TRUE(request_status_.ok()) << request_status_;
  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_EQ(request_headers_.getMethodValue(), "POST");
  EXPECT_EQ(request_headers_.getPathValue(), "/v1/projects/p/apiKeys");
  EXPECT_EQ(request_headers_.getContentTypeValue(), "application/json");
  EXPECT_EQ(request_headers_.getContentLengthValue(), "19");
  EXPECT_EQ(route_cache_clears_, 1);
  const json metadata = dynamicMetadata();
  EXPECT_EQ(metadata["status"], "mcp_json_rest_bridge_ok");
  EXPECT_EQ(metadata["params"]["name"], "create_api_key");
}

TEST_F(McpJsonRestBridgeFilterTest, DropsTheBodyOfABodilessToolsCall) {
  makeFilter();

  EXPECT_THAT(decode(toolsCall("get_api_key")), IsEmpty());

  EXPECT_TRUE(request_status_.ok()) << request_status_;
  EXPECT_EQ(request_headers_.getMethodValue(), "GET");
  EXPECT_EQ(request_headers_.getPathValue(), "/v1/apiKeys");
  EXPECT_EQ(request_headers_.getContentLengthValue(), "0");
}

TEST_F(McpJsonRestBridgeFilterTest, MapsToolsListOntoTheBackendRequest) {
  makeFilter(R"yaml(
tool_config:
  tool_list_http_rule:
    get: "/v1/tools"
)yaml");

  EXPECT_THAT(decode(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})"), IsEmpty());

  EXPECT_TRUE(request_status_.ok()) << request_status_;
  EXPECT_EQ(request_headers_.getMethodValue(), "GET");
  EXPECT_EQ(request_headers_.getPathValue(), "/v1/tools");
  EXPECT_EQ(request_headers_.ContentLength(), nullptr);
  EXPECT_EQ(route_cache_clears_, 1);
}

TEST_F(McpJsonRestBridgeFilterTest, ForwardsToolsListWithoutARule) {
  makeFilter();
  const std::string tools_list = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";

  EXPECT_EQ(json::parse(decode(tools_list)), json::parse(tools_list));

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_EQ(request_headers_.getPathValue(), "/mcp");
  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/false),
            ResponseMode::Parsed);
}

// The HTTP filter wraps an error as its own reply passes back through it. The manager sends an
// AI filter's reply as it is, so the filter sends the error already wrapped.
TEST_F(McpJsonRestBridgeFilterTest, RepliesWithAJsonRpcError) {
  makeFilter();

  EXPECT_THAT(decode(R"({"jsonrpc":"2.0","id":7,"method":"resources/list"})"), IsEmpty());

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::OK);
  EXPECT_EQ(local_reply_->details, "mcp_json_rest_bridge_request_mcp_method_not_supported");
  EXPECT_EQ(localReplyHeaders().getContentTypeValue(), "application/json");
  EXPECT_EQ(json::parse(local_reply_->body), json::parse(
                                                 R"({"jsonrpc":"2.0","id":7,
              "error":{"code":-32601,"message":"MCP method resources/list is not supported"}})"));
}

TEST_F(McpJsonRestBridgeFilterTest, RejectsARequestThatIsNotAPost) {
  request_headers_.setMethod("PUT");
  makeFilter();

  EXPECT_THAT(decode(CreateApiKey), IsEmpty());

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::MethodNotAllowed);
  EXPECT_EQ(local_reply_->body, "HTTP Method Not Allowed");
  EXPECT_EQ(localReplyHeaders().get_("allow"), "POST");
  EXPECT_THAT(localReplyHeaders().getContentTypeValue(), IsEmpty());
}

TEST_F(McpJsonRestBridgeFilterTest, RejectsARequestOverTheBodyLimit) {
  makeFilter(R"yaml(
max_request_body_size: 16
)yaml",
             /*request_payload_bytes=*/17);

  EXPECT_THAT(decode(CreateApiKey), IsEmpty());

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::PayloadTooLarge);
  EXPECT_EQ(json::parse(local_reply_->body)["error"]["message"], "Request body too large");
}

// The bridge reads argument values, and a reference is a value the manager did not keep.
TEST_F(McpJsonRestBridgeFilterTest, RejectsARequestWithAnOffloadedString) {
  makeFilter();
  JsonWithExtBuf doc;
  doc.setJson(json::parse(CreateApiKey));
  doc.json()["params"]["arguments"]["parent"] =
      JsonWithExtBuf::makeExternalRef({.offset = 0, .length = 4096});

  EXPECT_THAT(decode(std::move(doc)), IsEmpty());

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::PayloadTooLarge);
  EXPECT_EQ(local_reply_->details, "mcp_json_rest_bridge_request_too_large");
}

TEST_F(McpJsonRestBridgeFilterTest, UsesTheRouteConfiguration) {
  BridgePerRouteProto proto;
  auto* tool = proto.add_tool_config()->add_tools();
  tool->set_name("get_api_key");
  tool->mutable_http_rule()->set_get("/v2/apiKeys");
  route_config_ = Bridge::McpJsonRestBridgePerRouteConfig::create(proto).value();
  makeFilter();

  decode(toolsCall("get_api_key"));

  EXPECT_EQ(request_headers_.getPathValue(), "/v2/apiKeys");
}

TEST_F(McpJsonRestBridgeFilterTest, AnswersARequestWithoutABody) {
  makeFilter();

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::NoBody);

  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::BadRequest);
  EXPECT_EQ(json::parse(reply->body), json::parse(R"({"jsonrpc":"2.0","id":null,
                            "error":{"code":-32700,"message":"JSON parse error"}})"));
}

TEST_F(McpJsonRestBridgeFilterTest, AnswersAGetWithoutABody) {
  request_headers_.setMethod("GET");
  makeFilter();

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::NoBody);

  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::MethodNotAllowed);
}

TEST_F(McpJsonRestBridgeFilterTest, AnswersARequestThatIsNotJson) {
  makeFilter();

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::InvalidJson);

  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::BadRequest);
  EXPECT_EQ(reply->details, "mcp_json_rest_bridge_request_failed_to_parse_json_rpc");
}

TEST_F(McpJsonRestBridgeFilterTest, AnswersARequestThatIsNotJsonAndOverTheBodyLimit) {
  makeFilter(R"yaml(
max_request_body_size: 16
)yaml",
             /*request_payload_bytes=*/17);

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::InvalidJson);

  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::PayloadTooLarge);
}

TEST_F(McpJsonRestBridgeFilterTest, LeavesOtherUnparsedRequestsToTheManager) {
  request_headers_.setPath("/v1/apiKeys");
  makeFilter();

  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::NoBody).has_value());
  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::InvalidJson).has_value());
}

TEST_F(McpJsonRestBridgeFilterTest, TranslatesABufferedResponse) {
  makeFilter();
  decode(CreateApiKey);

  // The headers are held: content-length is only known once the body has been translated.
  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/false),
            ResponseMode::RawHoldHeaders);
  const std::string body = encode({R"({"name":)", R"("keys/1"})"});

  EXPECT_TRUE(response_status_.ok()) << response_status_;
  EXPECT_EQ(json::parse(body), json::parse(R"({"jsonrpc":"2.0","id":1,
                            "result":{"content":[{"type":"text","text":"{\"name\":\"keys/1\"}"}],
                                      "isError":false}})"));
  EXPECT_EQ(response_headers_.getContentLengthValue(), std::to_string(body.size()));
  EXPECT_EQ(dynamicMetadata()["backend_response_code"], 200);
}

TEST_F(McpJsonRestBridgeFilterTest, TranslatesABackendError) {
  makeFilter();
  decode(CreateApiKey);
  response_headers_.setStatus(500);

  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/false),
            ResponseMode::RawHoldHeaders);
  const json body = json::parse(encode({"backend failure"}));

  EXPECT_EQ(body["result"]["isError"], true);
  EXPECT_EQ(body["result"]["content"][0]["text"], "backend failure");
  EXPECT_EQ(response_headers_.getStatusValue(), "200");
  EXPECT_EQ(response_headers_.getContentTypeValue(), "application/json");
}

TEST_F(McpJsonRestBridgeFilterTest, RepliesLocallyToAResponseOverTheBodyLimit) {
  makeFilter(std::string(DefaultConfig) + "max_response_body_size: 8\n");
  decode(CreateApiKey);

  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/false),
            ResponseMode::RawHoldHeaders);
  EXPECT_THAT(encode({"12345678", "9"}), IsEmpty());

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::OK);
  EXPECT_EQ(local_reply_->details, "mcp_json_rest_bridge_response_too_large");
  // The reply is complete as the bridge sends it.
  EXPECT_EQ(json::parse(local_reply_->body), json::parse(R"({"jsonrpc":"2.0","id":1,
                            "error":{"code":-32000,"message":"Response body too large"}})"));
}

TEST_F(McpJsonRestBridgeFilterTest, GivesAHeadersOnlyResponseABody) {
  makeFilter();
  decode(CreateApiKey);
  response_headers_.setStatus(204);

  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/true), ResponseMode::Raw);
  const std::string body = encode({});

  EXPECT_EQ(json::parse(body)["result"]["content"][0]["text"], "");
  EXPECT_EQ(response_headers_.getStatusValue(), "200");
  EXPECT_EQ(response_headers_.getContentLengthValue(), std::to_string(body.size()));
}

TEST_F(McpJsonRestBridgeFilterTest, TranslatesAStreamedResponseChunkByChunk) {
  makeFilter();
  decode(toolsCall("stream_logs"));
  response_headers_.setContentLength(7);

  // The headers go ahead of the body, which has no known length.
  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/false),
            ResponseMode::Raw);
  EXPECT_EQ(response_headers_.ContentLength(), nullptr);
  const json body = json::parse(encode({"line \"1\"\n", "line 2"}));

  EXPECT_TRUE(response_status_.ok()) << response_status_;
  EXPECT_EQ(body["result"]["content"][0]["text"], "line \"1\"\nline 2");
  EXPECT_EQ(body["result"]["isError"], false);
}

TEST_F(McpJsonRestBridgeFilterTest, RepliesLocallyToAStreamedResponseItCannotTranslate) {
  makeFilter(std::string(DefaultConfig) + "max_response_body_size: 8\n");
  decode(toolsCall("stream_logs"));
  response_headers_.setContentType("text/event-stream");

  EXPECT_EQ(manager_->onResponseHeaders(response_headers_, /*end_stream=*/false),
            ResponseMode::Raw);
  encode({"data: an event longer than the limit"});

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::InternalServerError);
  EXPECT_EQ(local_reply_->details,
            "mcp_json_rest_bridge_filter_streaming_payload_preparation_error");
}

TEST_F(McpJsonRestBridgeFilterTest, LogsWithTheRequestId) {
  makeFilter();
  EXPECT_EQ(filter_->streamId(), 0);
  EXPECT_FALSE(filter_->connection().has_value());

  stream_info_.setStreamIdProvider(
      std::make_shared<StreamInfo::StreamIdProviderImpl>("0000002a-0000-0000-0000-000000000000"));
  EXPECT_NE(filter_->streamId(), 0);
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
