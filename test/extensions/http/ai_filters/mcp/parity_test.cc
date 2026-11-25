#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/common/mcp/filter_state.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_request.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf_parser.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"
#include "source/extensions/http/ai_filters/mcp/config.h"
#include "source/extensions/http/ai_filters/mcp/filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
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
using HttpFilters::AiProtocolManager::JsonWithExtBufParser;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::LocalReplier;
using HttpFilters::AiProtocolManager::LocalReply;
using HttpFilters::AiProtocolManager::UnparsedRequest;
using HttpFilters::Mcp::McpFilterConfig;
using HttpFilters::Mcp::McpFilterConfigSharedPtr;
using HttpFilters::Mcp::McpOverrideConfig;
using McpProto = envoy::extensions::filters::http::mcp::v3::Mcp;
using McpOverrideProto = envoy::extensions::filters::http::mcp::v3::McpOverride;
using McpFilterStateObject = Filters::Common::Mcp::FilterStateObject;
using testing::_;
using testing::HasSubstr;
using testing::NiceMock;
using testing::Not;
using testing::Return;

constexpr char MetadataNamespace[] = "envoy.filters.http.mcp";
constexpr char StatsPrefix[] = "test.";
// The manager's cap on the inline string threshold an AI filter asks for.
constexpr uint32_t ManagerMaxInlineStringBytes = 1024 * 1024;

constexpr char RejectMode[] = "traffic_mode: REJECT_NO_MCP";

constexpr char ToolsCall[] =
    R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"get_weather"}})";
constexpr char ToolsList[] = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
constexpr char Notification[] = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
constexpr char ResultResponse[] = R"({"jsonrpc":"2.0","id":1,"result":{"content":[]}})";
constexpr char ErrorResponse[] =
    R"({"jsonrpc":"2.0","id":1,"error":{"code":-32600,"message":"bad"}})";

// A 2026-07-28 tools/call whose params._meta is `meta`.
std::string newRevisionToolsCall(const std::string& meta) {
  return absl::StrCat(R"({"jsonrpc":"2.0","id":7,"method":"tools/call",)"
                      R"("params":{"name":"get_weather","_meta":)",
                      meta, "}}");
}

// A tools/call of `argument_bytes` of arguments after its attributes.
std::string toolsCallWithArguments(size_t argument_bytes) {
  return absl::StrCat(R"({"jsonrpc":"2.0","id":7,"method":"tools/call",)"
                      R"("params":{"name":"get_weather","arguments":{"blob":")",
                      std::string(argument_bytes, 'a'), R"("}}})");
}

constexpr char NewRevisionMeta[] = R"({"io.modelcontextprotocol/protocolVersion":"2026-07-28",)"
                                   R"("io.modelcontextprotocol/clientCapabilities":{}})";

Http::TestRequestHeaderMapImpl post() {
  return {{":method", "POST"},
          {":path", "/mcp"},
          {"content-type", "application/json"},
          {"accept", "application/json, text/event-stream"}};
}

Http::TestRequestHeaderMapImpl newRevisionPost(const std::string& method, const std::string& name) {
  Http::TestRequestHeaderMapImpl headers = post();
  headers.addCopy("mcp-protocol-version", "2026-07-28");
  if (!method.empty()) {
    headers.addCopy("mcp-method", method);
  }
  if (!name.empty()) {
    headers.addCopy("mcp-name", name);
  }
  return headers;
}

struct Request {
  std::string name;
  // Mcp, as YAML.
  std::string config{"{}"};
  // The route's McpOverride, as YAML; none when empty.
  std::string route_override{};
  Http::TestRequestHeaderMapImpl headers{post()};
  // Not set for a request that ends on its headers.
  std::optional<std::string> body{};
};

// What a host leaves of one request.
struct Outcome {
  void setReply(Http::Code code, absl::string_view body,
                const std::function<void(Http::ResponseHeaderMap&)>& modify_headers,
                absl::string_view details) {
    Http::TestResponseHeaderMapImpl headers;
    if (modify_headers != nullptr) {
      modify_headers(headers);
    }
    reply = absl::StrCat(static_cast<uint32_t>(code), " details=", details,
                         " body=", canonicalJson(body), " headers=", describeHeaders(headers));
  }

  // JSON with its members in one order, whatever order a protobuf map held them in. Other text
  // is returned as it is.
  static std::string canonicalJson(absl::string_view text) {
    const nlohmann::json json = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    return json.is_discarded() ? std::string(text) : json.dump();
  }

  static std::string describeHeaders(const Http::HeaderMap& headers) {
    std::string out;
    headers.iterate([&out](const Http::HeaderEntry& entry) {
      absl::StrAppend(&out, entry.key().getStringView(), "=", entry.value().getStringView(), ";");
      return Http::HeaderMap::Iterate::Continue;
    });
    return out;
  }

  // One line per observable, so a mismatch names what differs.
  std::string describe() const {
    std::string counter_values;
    for (const auto& [name, value] : counters) {
      absl::StrAppend(&counter_values, name, "=", value, " ");
    }
    return absl::StrCat(
        "reply: ", reply, "\nmetadata: ", metadata, "\nfilter state: ", filter_state,
        "\nrequest headers: ", describeHeaders(request_headers),
        "\nroute cache clears: ", route_cache_clears, "\ncounters: ", counter_values);
  }

  std::string reply{"none"};
  std::string metadata{"none"};
  std::string filter_state{"none"};
  Http::TestRequestHeaderMapImpl request_headers;
  int route_cache_clears{0};
  std::vector<std::pair<std::string, uint64_t>> counters;
};

class McpFilterParityTest : public testing::Test {
public:
  static McpProto protoConfig(const Request& request) {
    McpProto proto;
    TestUtility::loadFromYaml(request.config, proto);
    return proto;
  }

  static std::shared_ptr<const McpOverrideConfig> routeOverride(const Request& request) {
    if (request.route_override.empty()) {
      return nullptr;
    }
    McpOverrideProto proto;
    TestUtility::loadFromYaml(request.route_override, proto);
    return std::make_shared<const McpOverrideConfig>(proto);
  }

  static void collect(Outcome& outcome, StreamInfo::MockStreamInfo& stream_info,
                      Stats::Store& stats) {
    const auto& filter_metadata = stream_info.metadata_.filter_metadata();
    if (const auto it = filter_metadata.find(MetadataNamespace); it != filter_metadata.end()) {
      outcome.metadata =
          Outcome::canonicalJson(MessageUtil::getJsonStringFromMessageOrError(it->second));
    }
    if (const auto* object = stream_info.filterState()->getDataReadOnly<McpFilterStateObject>(
            McpFilterStateObject::FilterStateKey);
        object != nullptr) {
      outcome.filter_state =
          absl::StrCat("method=", object->method().value_or(""), " mcp=", object->isMcpRequest(),
                       " exceeding=", object->isExceedingLimit(),
                       " status=", Filters::Common::Mcp::statusToString(object->status()), " ",
                       Outcome::canonicalJson(object->json()->asJsonString()));
    }
    for (const char* name : {"requests_rejected", "invalid_json", "body_too_large",
                             "duplicate_keys_rejected", "header_mismatch"}) {
      outcome.counters.emplace_back(
          name, TestUtility::findCounter(stats, absl::StrCat(StatsPrefix, "mcp.", name))->value());
    }
  }

  // The MCP HTTP filter, given the body in one frame.
  static Outcome runHttpFilter(const Request& request) {
    Outcome outcome;
    outcome.request_headers = request.headers;
    NiceMock<Stats::MockIsolatedStatsStore> stats;
    NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks;
    const auto route_override = routeOverride(request);
    ON_CALL(callbacks, mostSpecificPerFilterConfig()).WillByDefault(Return(route_override.get()));
    ON_CALL(callbacks, requestHeaders())
        .WillByDefault(Return(Http::RequestHeaderMapOptRef(outcome.request_headers)));
    ON_CALL(callbacks.downstream_callbacks_, clearRouteCache()).WillByDefault([&outcome] {
      ++outcome.route_cache_clears;
    });
    ON_CALL(callbacks, sendLocalReply(_, _, _, _, _))
        .WillByDefault([&outcome](Http::Code code, absl::string_view body,
                                  std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                                  std::optional<Grpc::Status::GrpcStatus>,
                                  absl::string_view details) {
          outcome.setReply(code, body, modify_headers, details);
        });

    HttpFilters::Mcp::McpFilter filter(
        std::make_shared<McpFilterConfig>(protoConfig(request), StatsPrefix, *stats.rootScope()));
    filter.setDecoderFilterCallbacks(callbacks);
    const Http::FilterHeadersStatus status =
        filter.decodeHeaders(outcome.request_headers, !request.body.has_value());
    if (status == Http::FilterHeadersStatus::StopIteration && outcome.reply == "none" &&
        request.body.has_value()) {
      Buffer::OwnedImpl data(*request.body);
      filter.decodeData(data, /*end_stream=*/true);
    }
    collect(outcome, callbacks.stream_info_, stats);
    return outcome;
  }

  // The MCP AI filter, given the request as the manager gives it: the document its parser built,
  // or the reason there is none. A body that is not JSON gets the manager's reply unless the
  // filter answers it or has it forwarded.
  static Outcome runAiFilter(const Request& request) {
    Outcome outcome;
    outcome.request_headers = request.headers;
    NiceMock<Stats::MockIsolatedStatsStore> stats;
    NiceMock<StreamInfo::MockStreamInfo> stream_info;
    const McpProto proto = protoConfig(request);
    const uint64_t body_size = request.body.has_value() ? request.body->size() : 0;
    McpFilter filter(
        std::make_shared<McpFilterConfig>(proto, StatsPrefix, *stats.rootScope()),
        AiFilterContext{.stream_info = stream_info,
                        .request_headers = outcome.request_headers,
                        .request_protocol = LLMProtocol::Unspecified,
                        .request_payload_bytes = body_size,
                        .route_config = routeOverride(request),
                        .clear_route_cache = [&outcome] { ++outcome.route_cache_clears; }});

    std::optional<LocalReply> reply;
    if (body_size == 0) {
      reply = filter.onUnparsedRequest(UnparsedRequest::NoBody);
    } else {
      JsonWithExtBufParser::Config parser_config;
      parser_config.inline_string_threshold_bytes =
          std::max(JsonWithExtBufParser::kDefaultInlineStringThresholdBytes,
                   std::min(McpFilterConfigFactory().requestInlineStringBytes(proto).value_or(0),
                            ManagerMaxInlineStringBytes));
      JsonWithExtBufParser parser(parser_config);
      if (parser.feed(*request.body, /*end_stream=*/true).ok()) {
        AiRequest ai_request(parser.takeDocument());
        EXPECT_TRUE(filter
                        .decodeSync(ai_request, LocalReplier([&reply](LocalReply sent) {
                                      reply = std::move(sent);
                                    }))
                        .ok());
      } else {
        reply = filter.onUnparsedRequest(UnparsedRequest::InvalidJson);
        if (!reply.has_value() && !filter.forwardsUnparsedRequest()) {
          reply = LocalReply{.code = Http::Code::BadRequest,
                             .body = "invalid JSON",
                             .details = "ai_protocol_manager_invalid_json"};
        }
      }
    }
    if (reply.has_value()) {
      outcome.setReply(reply->code, reply->body, reply->modify_headers, reply->details);
    }
    collect(outcome, stream_info, stats);
    return outcome;
  }

  // Runs the request through both filters and returns what they both left.
  static std::string expectSameOutcome(const Request& request) {
    SCOPED_TRACE(absl::StrCat(request.name, " [", request.config, "]"));
    const std::string http = runHttpFilter(request).describe();
    EXPECT_EQ(runAiFilter(request).describe(), http);
    return http;
  }

  // Runs the request in both traffic modes that inspect it.
  static void expectSameOutcomeInEachMode(Request request) {
    const std::string config = request.config == "{}" ? "" : absl::StrCat(request.config, "\n");
    expectSameOutcome(request);
    request.config = absl::StrCat(config, RejectMode);
    expectSameOutcome(request);
  }
};

TEST_F(McpFilterParityTest, ClassifiesRequests) {
  const std::vector<Request> requests = {
      {.name = "tools/call", .body = ToolsCall},
      {.name = "tools/list", .body = ToolsList},
      {.name = "notification", .body = Notification},
      {.name = "not JSON-RPC", .body = R"({"model":"gpt-4"})"},
      {.name = "JSON-RPC 1.0", .body = R"({"jsonrpc":"1.0","id":1,"method":"tools/list"})"},
      {.name = "batch", .body = absl::StrCat("[", ToolsCall, "]")},
      {.name = "scalar", .body = R"("tools/call")"},
      {.name = "unknown method", .body = R"({"jsonrpc":"2.0","id":"a","method":"custom/op"})"},
      {.name = "members in another order",
       .body = R"({"params":{"name":"get_weather"},"method":"tools/call","id":7,"jsonrpc":"2.0"})"},
      {.name = "POST that ends on its headers"},
      {.name = "POST that is not JSON",
       .headers = {{":method", "POST"},
                   {"content-type", "text/plain"},
                   {"accept", "application/json, text/event-stream"}},
       .body = "hello"},
      {.name = "POST that does not accept SSE",
       .headers = {{":method", "POST"},
                   {"content-type", "application/json"},
                   {"accept", "application/json"}},
       .body = ToolsCall},
      {.name = "SSE GET", .headers = {{":method", "GET"}, {"accept", "text/event-stream"}}},
      {.name = "GET", .headers = {{":method", "GET"}, {"accept", "text/html"}}},
      {.name = "session DELETE", .headers = {{":method", "DELETE"}, {"mcp-session-id", "session"}}},
      {.name = "DELETE", .headers = {{":method", "DELETE"}}},
  };
  for (const Request& request : requests) {
    expectSameOutcomeInEachMode(request);
  }

  EXPECT_THAT(expectSameOutcome({.name = "tools/call", .config = RejectMode, .body = ToolsCall}),
              HasSubstr(R"("params":{"name":"get_weather"})"));
  EXPECT_THAT(expectSameOutcome(
                  {.name = "not JSON-RPC", .config = RejectMode, .body = R"({"model":"gpt-4"})"}),
              HasSubstr("reply: 400 details=mcp_not_jsonrpc"));
}

TEST_F(McpFilterParityTest, TellsAResponseFromARequest) {
  const std::vector<Request> requests = {
      {.name = "result", .body = ResultResponse},
      {.name = "error", .body = ErrorResponse},
      {.name = "string result", .body = R"({"jsonrpc":"2.0","id":1,"result":"done"})"},
      {.name = "number result", .body = R"({"jsonrpc":"2.0","id":1,"result":5})"},
      {.name = "list result", .body = R"({"jsonrpc":"2.0","id":1,"result":[1]})"},
      {.name = "string error", .body = R"({"jsonrpc":"2.0","id":1,"error":"bad"})"},
      {.name = "result without jsonrpc", .body = R"({"id":1,"result":{}})"},
      {.name = "result and method",
       .body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","result":{}})"},
      // A response carries no params._meta, and none is asked of it.
      {.name = "2026-07-28 result", .headers = newRevisionPost("", ""), .body = ResultResponse},
      {.name = "2026-07-28 error", .headers = newRevisionPost("", ""), .body = ErrorResponse},
  };
  for (const Request& request : requests) {
    expectSameOutcomeInEachMode(request);
  }

  EXPECT_THAT(expectSameOutcome({.name = "result", .config = RejectMode, .body = ResultResponse}),
              HasSubstr(R"("method":"__jsonrpc_response")"));

  // The response is one whatever the manager keeps of its result.
  const std::string large_result =
      absl::StrCat(R"({"jsonrpc":"2.0","id":1,"result":")", std::string(2 << 20, 'r'), R"("})");
  EXPECT_THAT(expectSameOutcome({.name = "large string result",
                                 .config = "traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 0",
                                 .body = large_result}),
              HasSubstr(R"("method":"__jsonrpc_response")"));
}

TEST_F(McpFilterParityTest, ChecksTheNewRevisionHeaders) {
  const std::string body = newRevisionToolsCall(NewRevisionMeta);
  const std::vector<Request> requests = {
      {.name = "matching headers",
       .headers = newRevisionPost("tools/call", "get_weather"),
       .body = body},
      {.name = "Base64 name",
       .headers = newRevisionPost("tools/call", "=?base64?Z2V0X3dlYXRoZXI=?="),
       .body = body},
      {.name = "invalid Base64 name",
       .headers = newRevisionPost("tools/call", "=?base64?!!!?="),
       .body = body},
      {.name = "no method header", .headers = newRevisionPost("", "get_weather"), .body = body},
      {.name = "no name header", .headers = newRevisionPost("tools/call", ""), .body = body},
      {.name = "another method",
       .headers = newRevisionPost("tools/list", "get_weather"),
       .body = body},
      {.name = "another name", .headers = newRevisionPost("tools/call", "get_time"), .body = body},
      {.name = "another version in the body",
       .headers = newRevisionPost("tools/call", "get_weather"),
       .body = newRevisionToolsCall(R"({"io.modelcontextprotocol/protocolVersion":"2025-06-18",)"
                                    R"("io.modelcontextprotocol/clientCapabilities":{}})")},
      {.name = "version of another type",
       .headers = newRevisionPost("tools/call", "get_weather"),
       .body = newRevisionToolsCall(R"({"io.modelcontextprotocol/protocolVersion":7,)"
                                    R"("io.modelcontextprotocol/clientCapabilities":{}})")},
      {.name = "no version in the body",
       .headers = newRevisionPost("tools/call", "get_weather"),
       .body = newRevisionToolsCall(R"({"io.modelcontextprotocol/clientCapabilities":{}})")},
      {.name = "no client capabilities",
       .headers = newRevisionPost("tools/call", "get_weather"),
       .body = newRevisionToolsCall(R"({"io.modelcontextprotocol/protocolVersion":"2026-07-28"})")},
      {.name = "no _meta",
       .headers = newRevisionPost("tools/call", "get_weather"),
       .body = ToolsCall},
      {.name = "notification without _meta",
       .headers = newRevisionPost("notifications/initialized", ""),
       .body = Notification},
      {.name = "session DELETE",
       .headers = {{":method", "DELETE"},
                   {"mcp-session-id", "session"},
                   {"mcp-protocol-version", "2026-07-28"}}},
      {.name = "SSE GET",
       .headers = {{":method", "GET"},
                   {"accept", "text/event-stream"},
                   {"mcp-protocol-version", "2026-07-28"}}},
  };
  for (Request request : requests) {
    expectSameOutcomeInEachMode(request);
    request.config = "attribute_source: HEADERS";
    expectSameOutcomeInEachMode(request);
    request.config = "max_supported_protocol_version: \"2025-06-18\"";
    expectSameOutcomeInEachMode(request);
  }

  EXPECT_THAT(expectSameOutcome({.name = "another name",
                                 .config = RejectMode,
                                 .headers = newRevisionPost("tools/call", "get_time"),
                                 .body = body}),
              HasSubstr("-32020"));
  EXPECT_THAT(expectSameOutcome({.name = "session DELETE",
                                 .config = RejectMode,
                                 .headers = {{":method", "DELETE"},
                                             {"mcp-session-id", "session"},
                                             {"mcp-protocol-version", "2026-07-28"}}}),
              HasSubstr("reply: 405"));
}

TEST_F(McpFilterParityTest, AppliesTheConfiguration) {
  // SPELLCHECKER(off)
  const std::string traced =
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_weather","_meta":{)"
      R"("traceparent":"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",)"
      R"("tracestate":"vendor=value","baggage":"user=alice"}}})";
  // SPELLCHECKER(on)
  Http::TestRequestHeaderMapImpl traced_headers = post();
  traced_headers.addCopy("traceparent", "00-00000000000000000000000000000001-0000000000000001-00");
  traced_headers.addCopy("tracestate", "old=state");

  const std::vector<Request> requests = {
      {.name = "filter state", .config = "request_storage_mode: FILTER_STATE", .body = ToolsCall},
      {.name = "filter state of a response",
       .config = "request_storage_mode: FILTER_STATE",
       .body = ResultResponse},
      {.name = "metadata and filter state",
       .config = "request_storage_mode: DYNAMIC_METADATA_AND_FILTER_STATE",
       .body = ToolsCall},
      {.name = "filter state of a request that is not MCP",
       .config = "request_storage_mode: DYNAMIC_METADATA_AND_FILTER_STATE",
       .body = R"({"model":"gpt-4"})"},
      {.name = "route cache", .config = "clear_route_cache: true", .body = ToolsCall},
      {.name = "route cache of a request that is not MCP",
       .config = "clear_route_cache: true",
       .body = R"({"model":"gpt-4"})"},
      {.name = "trace context",
       .config = "propagate_trace_context: {}",
       .headers = traced_headers,
       .body = traced},
      {.name = "baggage", .config = "propagate_baggage: {}", .body = traced},
      {.name = "trace context and baggage",
       .config = "propagate_trace_context: {}\npropagate_baggage: {}",
       .headers = traced_headers,
       .body = traced},
      {.name = "no trace context in the body",
       .config = "propagate_trace_context: {}",
       .headers = traced_headers,
       .body = ToolsCall},
      {.name = "method group and extraction rule",
       .config = R"EOF(
parser_config:
  group_metadata_key: group
  methods:
  - method: tools/call
    group: tools
    extraction_rules:
    - path: params.name
    - path: params.arguments.city
)EOF",
       .body = R"({"jsonrpc":"2.0","id":1,"method":"tools/call",)"
               R"("params":{"name":"get_weather","arguments":{"city":"Paris","days":3}}})"},
      {.name = "header attributes of a legacy request",
       .config = "attribute_source: HEADERS",
       .headers = {{":method", "POST"},
                   {"content-type", "application/json"},
                   {"accept", "application/json, text/event-stream"},
                   {"mcp-method", "tools/call"},
                   {"mcp-name", "get_time"}},
       .body = ToolsCall},
      {.name = "unsupported protocol version",
       .config = "max_supported_protocol_version: \"2025-06-18\"",
       .headers = {{":method", "POST"},
                   {"content-type", "application/json"},
                   {"accept", "application/json, text/event-stream"},
                   {"mcp-protocol-version", "2025-11-25"}},
       .body = ToolsCall},
      {.name = "NOOP", .config = "traffic_mode: NOOP", .body = R"({"model":"gpt-4"})"},
  };
  for (const Request& request : requests) {
    expectSameOutcomeInEachMode(request);
  }

  EXPECT_THAT(expectSameOutcome({.name = "trace context",
                                 .config = "propagate_trace_context: {}",
                                 .headers = traced_headers,
                                 .body = traced}),
              HasSubstr("traceparent=00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01;"
                        "tracestate=vendor=value;"));
  EXPECT_THAT(expectSameOutcome(
                  {.name = "route cache", .config = "clear_route_cache: true", .body = ToolsCall}),
              HasSubstr("route cache clears: 1"));
}

TEST_F(McpFilterParityTest, AppliesTheRouteOverride) {
  const std::vector<Request> requests = {
      {.name = "reject", .route_override = RejectMode, .body = R"({"model":"gpt-4"})"},
      {.name = "NOOP",
       .config = RejectMode,
       .route_override = "traffic_mode: NOOP",
       .body = R"({"model":"gpt-4"})"},
      {.name = "pass through",
       .config = RejectMode,
       .route_override = "traffic_mode: PASS_THROUGH",
       .body = R"({"model":"gpt-4"})"},
      {.name = "storage mode",
       .route_override = "request_storage_mode: FILTER_STATE",
       .body = ToolsCall},
      {.name = "route cache",
       .route_override = "clear_route_cache_override: true",
       .body = ToolsCall},
      {.name = "parser",
       .route_override = R"EOF(
parser_config:
  group_metadata_key: route_group
  methods:
  - method: tools/call
    group: route_tools
)EOF",
       .body = ToolsCall},
      {.name = "smaller body size",
       .config = RejectMode,
       .route_override = "max_request_body_size: 128",
       .body = toolsCallWithArguments(512)},
      {.name = "a request with no body", .route_override = RejectMode},
  };
  for (const Request& request : requests) {
    expectSameOutcome(request);
  }
}

TEST_F(McpFilterParityTest, HoldsTheBodyToTheSizeLimit) {
  const std::string exact = ToolsCall;
  const std::vector<Request> requests = {
      {.name = "at the limit",
       .config = absl::StrCat("max_request_body_size: ", exact.size()),
       .body = exact},
      {.name = "over the limit, attributes within it",
       .config = "max_request_body_size: 128",
       .body = toolsCallWithArguments(512)},
      {.name = "over the limit, filter state",
       .config = "max_request_body_size: 128\nrequest_storage_mode: FILTER_STATE",
       .body = toolsCallWithArguments(512)},
      {.name = "over the limit, not MCP",
       .config = "max_request_body_size: 32",
       .body = absl::StrCat(R"({"model":"gpt-4","input":")", std::string(256, 'a'), R"("})")},
      {.name = "over the default limit", .body = toolsCallWithArguments(16 * 1024)},
      {.name = "no limit",
       .config = "max_request_body_size: 0",
       .body = toolsCallWithArguments(64 * 1024)},
      // The arguments are longer than any string the manager keeps inline.
      {.name = "no limit, arguments the manager offloads",
       .config = "max_request_body_size: 0",
       .body = toolsCallWithArguments(2 << 20)},
      {.name = "limit past what the manager keeps inline",
       .config = "max_request_body_size: 4194304",
       .body = toolsCallWithArguments(2 << 20)},
      // The manager keeps inline what the listener's limit asks for, not the route's.
      {.name = "route raises the limit",
       .route_override = "max_request_body_size: 1048576",
       .body = toolsCallWithArguments(64 * 1024)},
      {.name = "route raises the limit, not far enough",
       .route_override = "max_request_body_size: 16384",
       .body = toolsCallWithArguments(64 * 1024)},
      {.name = "route lifts the limit",
       .route_override = "max_request_body_size: 0",
       .body = toolsCallWithArguments(64 * 1024)},
  };
  for (const Request& request : requests) {
    expectSameOutcomeInEachMode(request);
  }

  const std::string passed = expectSameOutcome({.name = "over the limit, attributes within it",
                                                .config = "max_request_body_size: 128",
                                                .body = toolsCallWithArguments(512)});
  EXPECT_THAT(passed, HasSubstr("reply: none"));
  EXPECT_THAT(passed, HasSubstr(R"("is_exceeding_limit":true)"));
  EXPECT_THAT(passed, HasSubstr(R"("passthrough_reason":"mcp_body_too_large")"));
  EXPECT_THAT(passed, HasSubstr(R"("params":{"name":"get_weather"})"));

  const std::string rejected =
      expectSameOutcome({.name = "over the limit, attributes within it",
                         .config = "traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 128",
                         .body = toolsCallWithArguments(512)});
  EXPECT_THAT(rejected, HasSubstr("reply: 400 details=mcp_body_too_large"));
  EXPECT_THAT(rejected, HasSubstr("body_too_large=1"));

  EXPECT_THAT(expectSameOutcome({.name = "no limit, arguments the manager offloads",
                                 .config = "traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 0",
                                 .body = toolsCallWithArguments(2 << 20)}),
              Not(HasSubstr("is_exceeding_limit")));
}

// Stopping at the routing attributes takes a body past the limit as one read whole.
TEST_F(McpFilterParityTest, StopsEarlyAtTheRoutingAttributes) {
  const std::string config = "max_request_body_size: 128\nearly_terminate_when_routable: true";
  const std::vector<Request> requests = {
      {.name = "over the limit", .config = config, .body = toolsCallWithArguments(512)},
      {.name = "within the limit", .config = config, .body = ToolsCall},
      {.name = "over the limit, not MCP",
       .config = config,
       .body = absl::StrCat(R"({"model":"gpt-4","input":")", std::string(256, 'a'), R"("})")},
      // Rejecting needs the whole body, so nothing stops early.
      {.name = "over the limit, reject mode",
       .config = absl::StrCat(config, "\n", RejectMode),
       .body = toolsCallWithArguments(512)},
      {.name = "over the limit, trace context wanted",
       .config = absl::StrCat(config, "\npropagate_trace_context: {}"),
       .body = toolsCallWithArguments(512)},
  };
  for (const Request& request : requests) {
    expectSameOutcome(request);
  }

  EXPECT_THAT(
      expectSameOutcome(
          {.name = "over the limit", .config = config, .body = toolsCallWithArguments(512)}),
      Not(HasSubstr("is_exceeding_limit")));
}

TEST_F(McpFilterParityTest, HandlesABodyThatIsNotJson) {
  const std::vector<Request> requests = {
      {.name = "text", .body = "not json"},
      {.name = "text, filter state",
       .config = "request_storage_mode: DYNAMIC_METADATA_AND_FILTER_STATE",
       .body = "not json"},
      {.name = "text, route cache", .config = "clear_route_cache: true", .body = "not json"},
      {.name = "text, 2026-07-28",
       .headers = newRevisionPost("tools/call", "get_weather"),
       .body = "not json"},
  };
  for (const Request& request : requests) {
    expectSameOutcomeInEachMode(request);
  }

  const std::string passed = expectSameOutcome({.name = "text", .body = "not json"});
  EXPECT_THAT(passed, HasSubstr("reply: none"));
  EXPECT_THAT(passed, HasSubstr(R"("passthrough_reason":"mcp_not_jsonrpc")"));
  EXPECT_THAT(passed, HasSubstr("invalid_json=1"));

  EXPECT_THAT(expectSameOutcome({.name = "text", .config = RejectMode, .body = "not json"}),
              HasSubstr("reply: 400 details=mcp_not_jsonrpc body=not a valid JSON"));
}

// The hosts differ where the manager's parser and the streaming parser read a body differently.
// Each case states what both do, so a change to either shows here.
class McpFilterDifferenceTest : public McpFilterParityTest {};

// The streaming parser reads the message a malformed body starts with; the manager has no
// document at all.
TEST_F(McpFilterDifferenceTest, MalformedBodyAfterTheAttributes) {
  const Request request{.name = "malformed",
                        .body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list",oops})"};
  EXPECT_THAT(runHttpFilter(request).describe(),
              HasSubstr(R"({"is_mcp_request":true,"passthrough_reason":"mcp_not_jsonrpc")"));
  EXPECT_THAT(runAiFilter(request).describe(),
              HasSubstr(R"({"is_mcp_request":false,"passthrough_reason":"mcp_not_jsonrpc")"));

  Request rejected = request;
  rejected.config = RejectMode;
  expectSameOutcome(rejected);
}

// The manager does not tell a body that ends early from one that is malformed.
TEST_F(McpFilterDifferenceTest, BodyThatEndsEarly) {
  Request request{.name = "ends early", .body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list")"};
  EXPECT_THAT(runHttpFilter(request).describe(),
              HasSubstr(R"("passthrough_reason":"mcp_parse_error")"));
  EXPECT_THAT(runAiFilter(request).describe(),
              HasSubstr(R"("passthrough_reason":"mcp_not_jsonrpc")"));

  request.config = RejectMode;
  EXPECT_THAT(runHttpFilter(request).describe(), HasSubstr("reply: 400 details=mcp_parse_error"));
  EXPECT_THAT(runAiFilter(request).describe(), HasSubstr("reply: 400 details=mcp_not_jsonrpc"));
}

// The manager takes no body that repeats a key, so the last key never wins.
TEST_F(McpFilterDifferenceTest, RepeatedKey) {
  Request request{.name = "repeated key",
                  .config = RejectMode,
                  .body = R"({"jsonrpc":"2.0","id":1,"method":"tools/list","method":"tools/call",)"
                          R"("params":{"name":"get_weather"}})"};
  EXPECT_THAT(runHttpFilter(request).describe(), HasSubstr("reply: none"));
  EXPECT_THAT(runAiFilter(request).describe(),
              HasSubstr("reply: 400 details=mcp_not_jsonrpc body=not a valid JSON"));

  request.config = "traffic_mode: REJECT_NO_MCP\nreject_duplicate_keys: true";
  EXPECT_THAT(runHttpFilter(request).describe(),
              HasSubstr("reply: 400 details=mcp_duplicate_keys"));
  EXPECT_THAT(runAiFilter(request).describe(), HasSubstr("reply: 400 details=mcp_not_jsonrpc"));

  // Forwarded alike where the request is not rejected, as a message only past the HTTP filter.
  request.config = "{}";
  const std::string http = runHttpFilter(request).describe();
  const std::string ai = runAiFilter(request).describe();
  EXPECT_THAT(http, HasSubstr("reply: none"));
  EXPECT_THAT(http, HasSubstr(R"("is_mcp_request":true)"));
  EXPECT_THAT(ai, HasSubstr("reply: none"));
  EXPECT_THAT(ai, HasSubstr(R"("passthrough_reason":"mcp_not_jsonrpc")"));
}

// The manager reports a body of no bytes as a request without one.
TEST_F(McpFilterDifferenceTest, EmptyBody) {
  const Request request{.name = "empty body", .config = RejectMode, .body = ""};
  EXPECT_THAT(runHttpFilter(request).describe(), HasSubstr("reply: 400 details=mcp_parse_error"));
  EXPECT_THAT(runAiFilter(request).describe(),
              HasSubstr("reply: 400 details=mcp_reject_no_mcp body=Only MCP traffic is allowed"));
}

// The streaming parser stops at the limit; the manager's document holds what comes after it.
TEST_F(McpFilterDifferenceTest, AttributesPastTheSizeLimit) {
  Request request{.name = "attributes past the limit",
                  .config = "max_request_body_size: 128",
                  .body = absl::StrCat(R"({"jsonrpc":"2.0","id":7,"params":{"arguments":{"blob":")",
                                       std::string(512, 'a'),
                                       R"("},"name":"get_weather"},"method":"tools/call"})")};
  const std::string http = runHttpFilter(request).describe();
  const std::string ai = runAiFilter(request).describe();
  for (const std::string& outcome : {http, ai}) {
    EXPECT_THAT(outcome, HasSubstr("reply: none"));
    EXPECT_THAT(outcome, HasSubstr(R"("is_exceeding_limit":true)"));
    EXPECT_THAT(outcome, HasSubstr(R"("passthrough_reason":"mcp_body_too_large")"));
  }
  EXPECT_THAT(http, HasSubstr(R"("is_mcp_request":false)"));
  EXPECT_THAT(http, Not(HasSubstr("tools/call")));
  EXPECT_THAT(ai, HasSubstr(R"("is_mcp_request":true)"));
  EXPECT_THAT(ai, HasSubstr(R"("method":"tools/call")"));

  // Rejected alike, each recording what it read.
  request.config = "traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 128";
  for (const std::string& outcome :
       {runHttpFilter(request).describe(), runAiFilter(request).describe()}) {
    EXPECT_THAT(outcome,
                HasSubstr("reply: 400 details=mcp_body_too_large body=reached end_stream"));
    EXPECT_THAT(outcome, HasSubstr("body_too_large=1"));
  }

  // Stopping early needs the attributes within the limit, which the document does not show.
  request.config = "max_request_body_size: 128\nearly_terminate_when_routable: true";
  EXPECT_THAT(runHttpFilter(request).describe(), HasSubstr(R"("is_exceeding_limit":true)"));
  EXPECT_THAT(runAiFilter(request).describe(), Not(HasSubstr("is_exceeding_limit")));
}

// An attribute longer than any string the manager keeps inline is not in its document.
TEST_F(McpFilterDifferenceTest, AttributeTheManagerOffloads) {
  Request request{.name = "offloaded attribute",
                  .config = "max_request_body_size: 0",
                  .body = absl::StrCat(R"({"jsonrpc":"2.0","id":7,"method":"tools/call",)"
                                       R"("params":{"name":")",
                                       std::string(2 << 20, 'n'), R"("}})")};
  EXPECT_THAT(runHttpFilter(request).describe(), Not(HasSubstr("is_exceeding_limit")));
  const std::string ai = runAiFilter(request).describe();
  EXPECT_THAT(ai, HasSubstr(R"("is_exceeding_limit":true)"));
  EXPECT_THAT(ai, HasSubstr(R"("method":"tools/call")"));

  request.config = "traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 0";
  EXPECT_THAT(runHttpFilter(request).describe(), HasSubstr("reply: none"));
  EXPECT_THAT(runAiFilter(request).describe(), HasSubstr("reply: 400 details=mcp_body_too_large"));
}

} // namespace
} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
