#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"

#include "source/extensions/filters/common/mcp/filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"
#include "source/extensions/http/ai_filters/mcp/filter.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Mcp {
namespace {

using HttpFilters::AiProtocolManager::AiFilterContext;
using HttpFilters::AiProtocolManager::AiRequest;
using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::LocalReply;
using HttpFilters::AiProtocolManager::PartialJson;
using HttpFilters::AiProtocolManager::UnparsedRequest;
using McpProto = envoy::extensions::filters::http::mcp::v3::Mcp;
using McpOverrideProto = envoy::extensions::filters::http::mcp::v3::McpOverride;
using McpFilterStateObject = Filters::Common::Mcp::FilterStateObject;
using testing::HasSubstr;
using testing::NiceMock;

constexpr char MetadataNamespace[] = "envoy.filters.http.mcp";

constexpr char ToolsCall[] =
    R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather"}})";

class McpAiFilterTest : public testing::Test {
public:
  // Creates the filter as the manager does once it has the request headers.
  void makeFilter(const std::string& config = "{}", uint64_t request_payload_bytes = 0) {
    McpProto proto;
    TestUtility::loadFromYaml(config, proto);
    config_ = std::make_shared<HttpFilters::Mcp::McpFilterConfig>(proto, "ai_protocol_manager.",
                                                                  *stats_store_.rootScope());
    filter_ = std::make_unique<McpFilter>(
        config_, AiFilterContext{.stream_info = stream_info_,
                                 .request_headers = request_headers_,
                                 .request_protocol = LLMProtocol::Unspecified,
                                 .request_payload_bytes = request_payload_bytes,
                                 .route_config = route_config_,
                                 .clear_route_cache = [this] { ++route_cache_clears_; },
                                 .request_ended_on_headers = request_ended_on_headers_,
                                 .request_has_duplicate_keys = request_has_duplicate_keys_,
                                 .partial_request =
                                     partial_request_.has_value() ? &*partial_request_ : nullptr});
  }

  // What the manager's parser had read of the body when it gave up.
  void setPartialRequest(const std::string& read, std::vector<std::string> open_keys,
                         bool ended_early) {
    partial_request_.emplace();
    partial_request_->json = nlohmann::json::parse(read);
    partial_request_->open_keys = std::move(open_keys);
    partial_request_->ended_early = ended_early;
  }

  // Hands the filter the parsed body, as the manager does at the end of a JSON body.
  void decodeDocument(nlohmann::json body) {
    JsonWithExtBuf document;
    document.setJson(std::move(body));
    AiRequest request(std::move(document));
    EXPECT_TRUE(filter_
                    ->decodeSync(request, LocalReplier([this](LocalReply reply) {
                                   local_reply_ = std::move(reply);
                                 }))
                    .ok());
  }

  void decode(const std::string& body) { decodeDocument(nlohmann::json::parse(body)); }

  const Protobuf::Struct* metadata() const {
    const auto& filter_metadata = stream_info_.metadata_.filter_metadata();
    const auto it = filter_metadata.find(MetadataNamespace);
    return it != filter_metadata.end() ? &it->second : nullptr;
  }

  std::string metadataJson() const {
    const Protobuf::Struct* fields = metadata();
    return fields != nullptr ? MessageUtil::getJsonStringFromMessageOrError(*fields) : "";
  }

  // The headers the local reply sets.
  Http::TestResponseHeaderMapImpl replyHeaders() const {
    Http::TestResponseHeaderMapImpl headers;
    if (local_reply_.has_value() && local_reply_->modify_headers != nullptr) {
      local_reply_->modify_headers(headers);
    }
    return headers;
  }

  uint64_t counter(const std::string& name) {
    return TestUtility::findCounter(stats_store_, "ai_protocol_manager.mcp." + name)->value();
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
  Http::TestRequestHeaderMapImpl request_headers_{
      {":method", "POST"},
      {":path", "/mcp"},
      {"content-type", "application/json"},
      {"accept", "application/json, text/event-stream"}};
  Router::RouteSpecificFilterConfigConstSharedPtr route_config_;
  HttpFilters::Mcp::McpFilterConfigSharedPtr config_;
  std::unique_ptr<McpFilter> filter_;
  std::optional<LocalReply> local_reply_;
  int route_cache_clears_{0};
  bool request_ended_on_headers_{false};
  bool request_has_duplicate_keys_{false};
  std::optional<PartialJson> partial_request_;
};

TEST_F(McpAiFilterTest, PublishesTheAttributesOfAParsedRequest) {
  makeFilter();
  decode(ToolsCall);

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(),
      R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather"},)"
      R"("status":"mcp_ok","is_mcp_request":true})"))
      << metadataJson();
  EXPECT_EQ(route_cache_clears_, 0);
}

TEST_F(McpAiFilterTest, StoresTheAttributesInFilterState) {
  makeFilter("request_storage_mode: FILTER_STATE");
  decode(ToolsCall);

  EXPECT_EQ(metadata(), nullptr);
  const auto* object = stream_info_.filterState()->getDataReadOnly<McpFilterStateObject>(
      McpFilterStateObject::FilterStateKey);
  ASSERT_NE(object, nullptr);
  EXPECT_EQ(object->method(), "tools/call");
  EXPECT_TRUE(object->isMcpRequest());
}

TEST_F(McpAiFilterTest, ClearsTheRouteCacheWhenConfigured) {
  makeFilter("clear_route_cache: true");
  decode(ToolsCall);
  EXPECT_EQ(route_cache_clears_, 1);
}

TEST_F(McpAiFilterTest, PassThroughLeavesABodyThatIsNotJsonRpc) {
  makeFilter();
  decode(R"({"model":"gpt-4"})");

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_EQ(metadata(), nullptr);
}

TEST_F(McpAiFilterTest, RejectModeRejectsABodyThatIsNotJsonRpc) {
  makeFilter("traffic_mode: REJECT_NO_MCP");
  decode(R"({"model":"gpt-4"})");

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::BadRequest);
  EXPECT_THAT(local_reply_->body, HasSubstr("valid JSON-RPC 2.0 message"));
  EXPECT_EQ(local_reply_->details, "mcp_not_jsonrpc");
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(), R"({"status":"mcp_not_jsonrpc","is_mcp_request":false})"))
      << metadataJson();
}

// A body is a JSON-RPC message only as an object, whatever a batch or a scalar holds.
TEST_F(McpAiFilterTest, RejectModeRejectsABodyThatIsNotAnObject) {
  makeFilter("traffic_mode: REJECT_NO_MCP");
  decodeDocument(nlohmann::json::array({nlohmann::json::parse(ToolsCall)}));

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->details, "mcp_not_jsonrpc");
}

TEST_F(McpAiFilterTest, RejectModeRejectsAPostThatIsNotMcp) {
  request_headers_.remove("accept");
  makeFilter("traffic_mode: REJECT_NO_MCP");
  decode(ToolsCall);

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::BadRequest);
  EXPECT_EQ(local_reply_->body, "Only MCP traffic is allowed");
  EXPECT_EQ(local_reply_->details, "mcp_reject_no_mcp");
  EXPECT_EQ(counter("requests_rejected"), 1);
}

TEST_F(McpAiFilterTest, NoopInspectsNothing) {
  request_headers_.remove("accept");
  makeFilter("traffic_mode: NOOP");
  decode(ToolsCall);

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_EQ(metadata(), nullptr);
}

// A JSON-RPC error carries the request's id, as JSON.
TEST_F(McpAiFilterTest, AnswersAHeaderMismatchWithAJsonRpcError) {
  request_headers_.setCopy(Http::LowerCaseString("mcp-protocol-version"), "2026-07-28");
  request_headers_.setCopy(Http::LowerCaseString("mcp-method"), "tools/call");
  request_headers_.setCopy(Http::LowerCaseString("mcp-name"), "another_tool");
  makeFilter("traffic_mode: REJECT_NO_MCP");
  decode(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather",)"
         R"("_meta":{"io.modelcontextprotocol/protocolVersion":"2026-07-28",)"
         R"("io.modelcontextprotocol/clientCapabilities":{}}}})");

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::BadRequest);
  EXPECT_EQ(replyHeaders().getContentTypeValue(), "application/json");
  const nlohmann::json error = nlohmann::json::parse(local_reply_->body);
  EXPECT_EQ(error["id"], 7);
  EXPECT_EQ(error["error"]["code"], -32020);
  EXPECT_EQ(error["error"]["message"], "MCP header attributes do not match request body");
  EXPECT_EQ(counter("header_mismatch"), 1);
}

// With HEADERS as the attribute source the body is not read when the headers say enough.
TEST_F(McpAiFilterTest, PublishesAttributesFromTheHeaders) {
  request_headers_.setCopy(Http::LowerCaseString("mcp-protocol-version"), "2026-07-28");
  request_headers_.setCopy(Http::LowerCaseString("mcp-method"), "tools/call");
  request_headers_.setCopy(Http::LowerCaseString("mcp-name"), "get_weather");
  makeFilter("attribute_source: HEADERS");
  decode(R"({"jsonrpc":"2.0","id":7,"method":"tools/list"})");

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(), R"({"method":"tools/call","params":{"name":"get_weather"},)"
                      R"("status":"mcp_ok","is_mcp_request":true})"))
      << metadataJson();
}

TEST_F(McpAiFilterTest, PropagatesTheTraceContextIntoTheRequestHeaders) {
  makeFilter("propagate_trace_context: {}\npropagate_baggage: {}");
  decode(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather",)"
         R"("_meta":{"traceparent":"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",)"
         R"("baggage":"tenant=acme"}}})");

  EXPECT_EQ(request_headers_.get_("traceparent"),
            "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
  EXPECT_EQ(request_headers_.get_("baggage"), "tenant=acme");
}

// The route's override comes from the manager's per-route configuration for the filter.
TEST_F(McpAiFilterTest, AppliesTheRouteOverride) {
  McpOverrideProto override_proto;
  override_proto.set_traffic_mode(McpProto::REJECT_NO_MCP);
  override_proto.mutable_clear_route_cache_override()->set_value(true);
  route_config_ = std::make_shared<const HttpFilters::Mcp::McpOverrideConfig>(override_proto);

  makeFilter();
  decode(ToolsCall);
  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_EQ(route_cache_clears_, 1);

  makeFilter();
  decode(R"({"model":"gpt-4"})");
  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->details, "mcp_not_jsonrpc");
}

// Another AI filter's per-route configuration is not an override.
TEST_F(McpAiFilterTest, IgnoresARouteConfigThatIsNotAnOverride) {
  class OtherRouteConfig : public Router::RouteSpecificFilterConfig {};
  route_config_ = std::make_shared<const OtherRouteConfig>();
  makeFilter();
  decode(R"({"model":"gpt-4"})");
  EXPECT_FALSE(local_reply_.has_value());
}

TEST_F(McpAiFilterTest, PassThroughMarksABodyOverTheSizeLimit) {
  makeFilter("max_request_body_size: 64", /*request_payload_bytes=*/65);
  decode(ToolsCall);

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(),
      R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather"},)"
      R"("status":"mcp_ok","is_mcp_request":true,"is_exceeding_limit":true,)"
      R"("passthrough_reason":"mcp_body_too_large"})"))
      << metadataJson();
}

TEST_F(McpAiFilterTest, RejectModeRejectsABodyOverTheSizeLimit) {
  makeFilter("traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 64",
             /*request_payload_bytes=*/65);
  decode(ToolsCall);

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::BadRequest);
  EXPECT_EQ(local_reply_->details, "mcp_body_too_large");
  EXPECT_EQ(counter("body_too_large"), 1);
}

TEST_F(McpAiFilterTest, BodySizeLimitOfZeroTakesAnyBody) {
  makeFilter("traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 0",
             /*request_payload_bytes=*/1 << 20);
  decode(ToolsCall);
  EXPECT_FALSE(local_reply_.has_value());
}

// An attribute the manager offloaded is not in the document, so the attributes are incomplete.
TEST_F(McpAiFilterTest, HandlesAnOffloadedAttributeAsABodyParsedInPart) {
  nlohmann::json body = nlohmann::json::parse(ToolsCall);
  body["params"]["name"] = JsonWithExtBuf::makeExternalRef({.offset = 60, .length = 4096});

  makeFilter();
  decodeDocument(body);
  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(), R"({"jsonrpc":"2.0","id":7,"method":"tools/call","status":"mcp_ok",)"
                      R"("is_mcp_request":true,"is_exceeding_limit":true,)"
                      R"("passthrough_reason":"mcp_body_too_large"})"))
      << metadataJson();

  makeFilter("traffic_mode: REJECT_NO_MCP");
  decodeDocument(body);
  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->details, "mcp_body_too_large");
}

// The filter reads no argument, so one the manager offloaded is not missed.
TEST_F(McpAiFilterTest, TakesARequestWhoseArgumentsAreOffloaded) {
  nlohmann::json body = nlohmann::json::parse(ToolsCall);
  body["params"]["arguments"]["blob"] =
      JsonWithExtBuf::makeExternalRef({.offset = 80, .length = 4096});

  makeFilter("traffic_mode: REJECT_NO_MCP");
  decodeDocument(body);
  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(),
      R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather"},)"
      R"("status":"mcp_ok","is_mcp_request":true})"))
      << metadataJson();
}

// Where the HTTP filter may stop reading at the routing attributes, a longer body is not one
// parsed in part.
TEST_F(McpAiFilterTest, TakesABodyOverTheSizeLimitThatMayStopEarly) {
  makeFilter("max_request_body_size: 64\nearly_terminate_when_routable: true",
             /*request_payload_bytes=*/65);
  decode(ToolsCall);

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(),
      R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather"},)"
      R"("status":"mcp_ok","is_mcp_request":true})"))
      << metadataJson();

  // Without its routing attributes the body is still over the limit.
  makeFilter("max_request_body_size: 64\nearly_terminate_when_routable: true",
             /*request_payload_bytes=*/65);
  decode(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{}})");
  EXPECT_THAT(metadataJson(), HasSubstr("is_exceeding_limit"));
}

TEST_F(McpAiFilterTest, RejectModeAnswersARequestWithoutABodyThatIsNotMcp) {
  request_ended_on_headers_ = true;
  request_headers_ = {{":method", "GET"}, {":path", "/mcp"}, {"accept", "text/html"}};
  makeFilter("traffic_mode: REJECT_NO_MCP");

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::NoBody);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::BadRequest);
  EXPECT_EQ(reply->body, "Only MCP traffic is allowed");
  EXPECT_EQ(counter("requests_rejected"), 1);
}

// A POST that ends on its headers carries no JSON-RPC message.
TEST_F(McpAiFilterTest, RejectModeAnswersAPostWithoutABody) {
  request_ended_on_headers_ = true;
  makeFilter("traffic_mode: REJECT_NO_MCP");
  EXPECT_TRUE(filter_->onUnparsedRequest(UnparsedRequest::NoBody).has_value());
}

TEST_F(McpAiFilterTest, LeavesMcpRequestsWithoutABody) {
  request_ended_on_headers_ = true;
  request_headers_ = {{":method", "GET"}, {":path", "/mcp"}, {"accept", "text/event-stream"}};
  makeFilter("traffic_mode: REJECT_NO_MCP");
  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::NoBody).has_value());

  request_headers_ = {{":method", "DELETE"}, {":path", "/mcp"}, {"mcp-session-id", "session"}};
  makeFilter("traffic_mode: REJECT_NO_MCP");
  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::NoBody).has_value());
}

TEST_F(McpAiFilterTest, PassThroughLeavesARequestWithoutABody) {
  request_ended_on_headers_ = true;
  request_headers_ = {{":method", "GET"}, {":path", "/mcp"}, {"accept", "text/html"}};
  makeFilter();
  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::NoBody).has_value());
}

// The 2026-07-28 revision has no session to end.
TEST_F(McpAiFilterTest, AnswersASessionDeleteOfTheNewRevisionWithMethodNotAllowed) {
  request_ended_on_headers_ = true;
  request_headers_ = {{":method", "DELETE"},
                      {":path", "/mcp"},
                      {"mcp-session-id", "session"},
                      {"mcp-protocol-version", "2026-07-28"}};
  makeFilter("traffic_mode: REJECT_NO_MCP");

  local_reply_ = filter_->onUnparsedRequest(UnparsedRequest::NoBody);
  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::MethodNotAllowed);
  EXPECT_EQ(replyHeaders().get_("allow"), "POST");
  // No details of its own, so the manager names the reply.
  EXPECT_TRUE(local_reply_->details.empty());
}

TEST_F(McpAiFilterTest, RejectModeAnswersABodyThatIsNotJson) {
  makeFilter("traffic_mode: REJECT_NO_MCP");

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::InvalidJson);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::BadRequest);
  EXPECT_EQ(reply->body, "not a valid JSON");
  EXPECT_EQ(reply->details, "mcp_not_jsonrpc");
  EXPECT_EQ(counter("invalid_json"), 1);
}

// The filter records why it let the request go, and has the manager forward it.
TEST_F(McpAiFilterTest, PassThroughForwardsABodyThatIsNotJson) {
  makeFilter("clear_route_cache: true");
  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::InvalidJson).has_value());
  EXPECT_TRUE(filter_->forwardsUnparsedRequest());
  EXPECT_EQ(counter("invalid_json"), 1);
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(),
      R"({"status":"mcp_ok","is_mcp_request":false,"passthrough_reason":"mcp_not_jsonrpc"})"))
      << metadataJson();
  EXPECT_EQ(route_cache_clears_, 1);
}

// A body frame with no bytes is a body that ended before it began.
TEST_F(McpAiFilterTest, PassThroughRecordsAnEmptyBody) {
  makeFilter();
  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::NoBody).has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(),
      R"({"status":"mcp_ok","is_mcp_request":false,"passthrough_reason":"mcp_parse_error"})"))
      << metadataJson();
}

TEST_F(McpAiFilterTest, RejectModeAnswersAnEmptyBody) {
  makeFilter("traffic_mode: REJECT_NO_MCP");

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::NoBody);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::BadRequest);
  EXPECT_EQ(reply->details, "mcp_parse_error");
  EXPECT_EQ(counter("requests_rejected"), 0);
}

// A body that ends too soon still publishes the attributes that were read.
TEST_F(McpAiFilterTest, PassThroughPublishesWhatWasReadOfABodyThatEndsEarly) {
  setPartialRequest(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{}})", {"", "params"},
                    /*ended_early=*/true);
  makeFilter();

  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::InvalidJson).has_value());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadataJson(), R"({"jsonrpc":"2.0","id":7,"method":"tools/call","status":"mcp_ok",)"
                      R"("is_mcp_request":true,"passthrough_reason":"mcp_parse_error"})"))
      << metadataJson();
  EXPECT_EQ(counter("invalid_json"), 0);
}

TEST_F(McpAiFilterTest, RejectModeAnswersABodyThatEndsEarly) {
  setPartialRequest(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{}})", {"", "params"},
                    /*ended_early=*/true);
  makeFilter("traffic_mode: REJECT_NO_MCP");

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::InvalidJson);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->code, Http::Code::BadRequest);
  EXPECT_EQ(reply->details, "mcp_parse_error");
  EXPECT_EQ(counter("invalid_json"), 0);
}

// A request followed by bytes that are not JSON is still classified by the request.
TEST_F(McpAiFilterTest, PassThroughClassifiesARequestFollowedByOtherBytes) {
  setPartialRequest(ToolsCall, {}, /*ended_early=*/false);
  makeFilter();

  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::InvalidJson).has_value());
  EXPECT_THAT(metadataJson(), HasSubstr(R"("is_mcp_request":true)"));
  EXPECT_THAT(metadataJson(), HasSubstr(R"("passthrough_reason":"mcp_not_jsonrpc")"));
  EXPECT_THAT(metadataJson(), HasSubstr(R"("name":"get_weather")"));
  EXPECT_EQ(counter("invalid_json"), 1);
}

// The manager's parser reports a repeated key; the filter holds it to its configuration.
TEST_F(McpAiFilterTest, RejectsARepeatedKeyWhenConfigured) {
  request_has_duplicate_keys_ = true;
  makeFilter("traffic_mode: REJECT_NO_MCP\nreject_duplicate_keys: true");

  decode(ToolsCall);

  ASSERT_TRUE(local_reply_.has_value());
  EXPECT_EQ(local_reply_->code, Http::Code::BadRequest);
  EXPECT_EQ(local_reply_->details, "mcp_duplicate_keys");
  EXPECT_EQ(counter("duplicate_keys_rejected"), 1);
}

TEST_F(McpAiFilterTest, PassThroughRecordsARepeatedKeyWhenConfigured) {
  request_has_duplicate_keys_ = true;
  makeFilter("reject_duplicate_keys: true");

  decode(ToolsCall);

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_THAT(metadataJson(), HasSubstr(R"("passthrough_reason":"mcp_duplicate_keys")"));
}

TEST_F(McpAiFilterTest, TakesARepeatedKeyByDefault) {
  request_has_duplicate_keys_ = true;
  makeFilter("traffic_mode: REJECT_NO_MCP");

  decode(ToolsCall);

  EXPECT_FALSE(local_reply_.has_value());
  EXPECT_THAT(metadataJson(), testing::Not(HasSubstr("passthrough_reason")));
}

// A repeat seen before the parser gave up counts too.
TEST_F(McpAiFilterTest, RejectsARepeatedKeyInABodyThatEndsEarly) {
  request_has_duplicate_keys_ = true;
  setPartialRequest(R"({"jsonrpc":"2.0","id":7,"method":"tools/call"})", {""},
                    /*ended_early=*/true);
  makeFilter("reject_duplicate_keys: true");

  EXPECT_FALSE(filter_->onUnparsedRequest(UnparsedRequest::InvalidJson).has_value());
  EXPECT_THAT(metadataJson(), HasSubstr(R"("passthrough_reason":"mcp_parse_error")"));
}

// The header checks come first, as they do for a request the manager parsed.
TEST_F(McpAiFilterTest, RejectModeAnswersANonMcpPostWhoseBodyIsNotJson) {
  request_headers_.remove("accept");
  makeFilter("traffic_mode: REJECT_NO_MCP");

  const std::optional<LocalReply> reply = filter_->onUnparsedRequest(UnparsedRequest::InvalidJson);
  ASSERT_TRUE(reply.has_value());
  EXPECT_EQ(reply->body, "Only MCP traffic is allowed");
  EXPECT_EQ(counter("invalid_json"), 0);
}

} // namespace
} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
