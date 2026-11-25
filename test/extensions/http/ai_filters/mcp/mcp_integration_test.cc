#include <string>

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/http/mcp/v3/mcp.pb.h"

#include "source/common/protobuf/utility.h"

#include "test/integration/fake_access_log.h"
#include "test/integration/http_integration.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/strip.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace Mcp {
namespace {

using McpProto = envoy::extensions::filters::http::mcp::v3::Mcp;
using McpOverrideProto = envoy::extensions::filters::http::mcp::v3::McpOverride;
using ManagerProto = envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager;
using ManagerPerRouteProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute;
using HttpConnectionManager =
    envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

constexpr char ManagerName[] = "envoy.filters.http.ai_protocol_manager";
constexpr char McpFilterName[] = "envoy.http.ai_filters.mcp";
constexpr char MetadataNamespace[] = "envoy.filters.http.mcp";

constexpr char ToolsCall[] =
    R"({"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "test_tool"}, "id": 1})";

// The AI Protocol Manager's per-route configuration: it declares an AI endpoint, and carries
// `override_yaml` as the MCP AI filter's override when that is set.
ManagerPerRouteProto aiEndpoint(const std::string& override_yaml = "") {
  ManagerPerRouteProto per_route;
  per_route.mutable_request();
  if (!override_yaml.empty()) {
    McpOverrideProto mcp_override;
    TestUtility::loadFromYaml(override_yaml, mcp_override);
    std::ignore =
        (*per_route.mutable_typed_per_filter_config())[McpFilterName].PackFrom(mcp_override);
  }
  return per_route;
}

class McpAiFilterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  McpAiFilterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP2, GetParam()) {}

  // Adds the manager with the MCP AI filter as its only AI filter. The MCP filter only reads
  // the request, so the manager forwards the body as it received it.
  void addManager(const std::string& mcp_yaml) {
    McpProto mcp;
    TestUtility::loadFromYaml(mcp_yaml, mcp);
    ManagerProto manager;
    manager.mutable_request_handling()->set_reserialize_body(
        envoy::extensions::filters::http::ai_protocol_manager::v3::RequestHandling::DISABLE);
    auto* ai_filter = manager.add_filters();
    ai_filter->set_name(McpFilterName);
    std::ignore = ai_filter->mutable_typed_config()->PackFrom(mcp);

    envoy::extensions::filters::network::http_connection_manager::v3::HttpFilter filter;
    filter.set_name(ManagerName);
    std::ignore = filter.mutable_typed_config()->PackFrom(manager);
    config_helper_.prependFilter(MessageUtil::getJsonStringFromMessageOrError(filter));
  }

  // Runs the MCP AI filter on a virtual host declared an AI endpoint.
  void initializeFilter(const std::string& mcp_yaml = "{}", const std::string& override_yaml = "") {
    addManager(mcp_yaml);
    config_helper_.addConfigModifier([override_yaml](HttpConnectionManager& hcm) {
      auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
      std::ignore = (*virtual_host->mutable_typed_per_filter_config())[ManagerName].PackFrom(
          aiEndpoint(override_yaml));
    });
    initialize();
    codec_client_ = makeHttpConnection(lookupPort("http"));
  }

  // Reports the MCP dynamic metadata of each request to `metadata_json_`.
  void captureMetadata() {
    access_log_factory_.setLogCallback(
        [this](const Formatter::Context&, const StreamInfo::StreamInfo& stream_info) {
          const auto& filter_metadata = stream_info.dynamicMetadata().filter_metadata();
          const auto it = filter_metadata.find(MetadataNamespace);
          metadata_json_ = it != filter_metadata.end()
                               ? MessageUtil::getJsonStringFromMessageOrError(it->second)
                               : "";
        });
    config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
      auto* access_log = hcm.add_access_log();
      access_log->set_name("envoy.access_loggers.test");
      std::ignore = access_log->mutable_typed_config()->PackFrom(
          test::integration::accesslog::FakeAccessLog());
    });
  }

  static Http::TestRequestHeaderMapImpl postHeaders() {
    return {{":method", "POST"},
            {":path", "/mcp"},
            {":scheme", "http"},
            {":authority", "host"},
            {"content-type", "application/json"},
            {"accept", "application/json, text/event-stream"}};
  }

  static Http::TestRequestHeaderMapImpl newRevisionHeaders() {
    Http::TestRequestHeaderMapImpl headers = postHeaders();
    headers.setCopy(Http::LowerCaseString("mcp-protocol-version"), "2026-07-28");
    return headers;
  }

  // Sends the request to completion, answering it from the upstream when it gets that far.
  IntegrationStreamDecoderPtr roundTrip(const Http::TestRequestHeaderMapImpl& headers,
                                        const std::string& body) {
    auto response = body.empty() ? codec_client_->makeHeaderOnlyRequest(headers)
                                 : codec_client_->makeRequestWithBody(headers, body);
    waitForNextUpstreamRequest();
    upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
    EXPECT_TRUE(response->waitForEndStream());
    return response;
  }

  // Sends a request the filter chain answers itself.
  IntegrationStreamDecoderPtr rejected(const Http::TestRequestHeaderMapImpl& headers,
                                       const std::string& body) {
    auto response = body.empty() ? codec_client_->makeHeaderOnlyRequest(headers)
                                 : codec_client_->makeRequestWithBody(headers, body);
    EXPECT_TRUE(response->waitForEndStream());
    EXPECT_EQ(upstream_request_, nullptr);
    return response;
  }

  FakeAccessLogFactory access_log_factory_;
  Registry::InjectFactory<AccessLog::AccessLogInstanceFactory> access_log_registration_{
      access_log_factory_};
  std::string metadata_json_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, McpAiFilterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(McpAiFilterIntegrationTest, PublishesAttributesAndForwardsTheBody) {
  captureMetadata();
  initializeFilter();

  auto response = roundTrip(postHeaders(), ToolsCall);

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(ToolsCall, upstream_request_->body().toString());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      metadata_json_, R"({"jsonrpc":"2.0","id":1,"method":"tools/call",)"
                      R"("params":{"name":"test_tool"},"status":"mcp_ok","is_mcp_request":true})"))
      << metadata_json_;
}

TEST_P(McpAiFilterIntegrationTest, ParsesABodyThatArrivesInChunks) {
  captureMetadata();
  initializeFilter();

  auto encoder_decoder = codec_client_->startRequest(postHeaders());
  auto response = std::move(encoder_decoder.second);
  const std::string body = ToolsCall;
  codec_client_->sendData(encoder_decoder.first, body.substr(0, 20), false);
  codec_client_->sendData(encoder_decoder.first, body.substr(20), true);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "200"}}, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ(body, upstream_request_->body().toString());
  EXPECT_THAT(metadata_json_, testing::HasSubstr(R"("method":"tools/call")"));
}

// The manager keeps inline the strings the MCP filter reads, up to its body size limit.
TEST_P(McpAiFilterIntegrationTest, ReadsAnAttributeLongerThanTheManagersInlineThreshold) {
  captureMetadata();
  initializeFilter();

  const std::string name(4000, 'n');
  roundTrip(
      postHeaders(),
      fmt::format(R"({{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{{"name":"{}"}}}})",
                  name));

  EXPECT_THAT(metadata_json_, testing::HasSubstr(name));
  EXPECT_THAT(metadata_json_, testing::Not(testing::HasSubstr("is_exceeding_limit")));
}

TEST_P(McpAiFilterIntegrationTest, PassThroughMarksABodyOverTheSizeLimit) {
  captureMetadata();
  initializeFilter("max_request_body_size: 64");

  auto response = roundTrip(postHeaders(), ToolsCall);

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_THAT(metadata_json_, testing::HasSubstr(R"("is_exceeding_limit":true)"));
  EXPECT_THAT(metadata_json_, testing::HasSubstr(R"("passthrough_reason":"mcp_body_too_large")"));
}

TEST_P(McpAiFilterIntegrationTest, RejectModeRejectsABodyOverTheSizeLimit) {
  initializeFilter("traffic_mode: REJECT_NO_MCP\nmax_request_body_size: 64");

  auto response = rejected(postHeaders(), ToolsCall);

  EXPECT_EQ("400", response->headers().getStatusValue());
  test_server_->waitForCounter("ai_protocol_manager.mcp.body_too_large", testing::Eq(1));
}

// The route's limit takes a body whose arguments the manager offloads, since the filter reads no
// argument.
TEST_P(McpAiFilterIntegrationTest, TakesArgumentsTheManagerOffloads) {
  captureMetadata();
  initializeFilter("traffic_mode: REJECT_NO_MCP", "max_request_body_size: 1048576");

  const std::string body =
      fmt::format(R"({{"jsonrpc":"2.0","id":1,"method":"tools/call",)"
                  R"("params":{{"name":"test_tool","arguments":{{"blob":"{}"}}}}}})",
                  std::string(64 * 1024, 'a'));
  auto response = roundTrip(postHeaders(), body);

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(body, upstream_request_->body().toString());
  EXPECT_THAT(metadata_json_, testing::HasSubstr(R"("params":{"name":"test_tool"})"));
  EXPECT_THAT(metadata_json_, testing::Not(testing::HasSubstr("is_exceeding_limit")));
}

TEST_P(McpAiFilterIntegrationTest, RejectModeRejectsARequestWithoutABodyThatIsNotMcp) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  auto response = rejected(
      Http::TestRequestHeaderMapImpl{
          {":method", "GET"}, {":path", "/mcp"}, {":scheme", "http"}, {":authority", "host"}},
      "");

  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_EQ("Only MCP traffic is allowed", response->body());
  test_server_->waitForCounter("ai_protocol_manager.mcp.requests_rejected", testing::Eq(1));
}

TEST_P(McpAiFilterIntegrationTest, RejectModeAllowsAnSseGet) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  auto response = codec_client_->makeHeaderOnlyRequest(
      Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                     {":path", "/mcp"},
                                     {":scheme", "http"},
                                     {":authority", "host"},
                                     {"accept", "text/event-stream"}});
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  upstream_request_->encodeData("data: {\"jsonrpc\":\"2.0\",\"method\":\"ping\"}\n\n", true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("data: {\"jsonrpc\":\"2.0\",\"method\":\"ping\"}\n\n", response->body());
}

TEST_P(McpAiFilterIntegrationTest, RejectModeRejectsABodyThatIsNotJsonRpc) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  auto response = rejected(postHeaders(), R"({"model": "gpt-4"})");

  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("JSON-RPC 2.0"));
}

// The MCP filter answers a body the manager could not parse, as the MCP HTTP filter does.
TEST_P(McpAiFilterIntegrationTest, RejectModeRejectsABodyThatIsNotJson) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  auto response = rejected(postHeaders(), "invalid_json_content");

  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_EQ("not a valid JSON", response->body());
  test_server_->waitForCounter("ai_protocol_manager.mcp.invalid_json", testing::Eq(1));
}

// A body the filter does not reject goes upstream as it arrived, as it does past the MCP HTTP
// filter.
TEST_P(McpAiFilterIntegrationTest, PassThroughForwardsABodyThatIsNotJson) {
  captureMetadata();
  initializeFilter();

  auto response = roundTrip(postHeaders(), "invalid_json_content");

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ("invalid_json_content", upstream_request_->body().toString());
  EXPECT_THAT(metadata_json_, testing::HasSubstr(R"("passthrough_reason":"mcp_not_jsonrpc")"));
  test_server_->waitForCounter("ai_protocol_manager.mcp.invalid_json", testing::Eq(1));
  test_server_->waitForCounter("ai_protocol_manager.request_parse_error", testing::Eq(1));
}

// The manager's parser takes no body that repeats a key, so the MCP filter has no document for
// one.
TEST_P(McpAiFilterIntegrationTest, PassThroughForwardsABodyThatRepeatsAKey) {
  initializeFilter();
  const std::string body = R"({"jsonrpc": "2.0", "method": "tools/list", "id": 1, "id": 2})";

  auto response = roundTrip(postHeaders(), body);

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_EQ(body, upstream_request_->body().toString());
  test_server_->waitForCounter("ai_protocol_manager.request_parse_error", testing::Eq(1));
}

TEST_P(McpAiFilterIntegrationTest, RejectModeRejectsABodyThatRepeatsAKey) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  auto response =
      rejected(postHeaders(), R"({"jsonrpc": "2.0", "method": "tools/list", "id": 1, "id": 2})");

  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_EQ("not a valid JSON", response->body());
}

TEST_P(McpAiFilterIntegrationTest, AnswersAHeaderMismatchWithAJsonRpcError) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  Http::TestRequestHeaderMapImpl headers = newRevisionHeaders();
  headers.setCopy(Http::LowerCaseString("mcp-method"), "tools/call");
  headers.setCopy(Http::LowerCaseString("mcp-name"), "another_tool");
  auto response = rejected(
      headers,
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"test_tool","_meta":)"
      R"({"io.modelcontextprotocol/protocolVersion":"2026-07-28",)"
      R"("io.modelcontextprotocol/clientCapabilities":{}}}})");

  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_EQ("application/json", response->headers().getContentTypeValue());
  EXPECT_TRUE(TestUtility::jsonStringEqual(
      response->body(), R"({"jsonrpc":"2.0","id":1,"error":{"code":-32020,)"
                        R"("message":"MCP header attributes do not match request body"}})"))
      << response->body();
}

TEST_P(McpAiFilterIntegrationTest, AnswersASessionDeleteOfTheNewRevisionWithMethodNotAllowed) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  auto response = rejected(Http::TestRequestHeaderMapImpl{{":method", "DELETE"},
                                                          {":path", "/mcp"},
                                                          {":scheme", "http"},
                                                          {":authority", "host"},
                                                          {"mcp-session-id", "session"},
                                                          {"mcp-protocol-version", "2026-07-28"}},
                           "");

  EXPECT_EQ("405", response->headers().getStatusValue());
  EXPECT_EQ("POST",
            response->headers().get(Http::LowerCaseString("allow"))[0]->value().getStringView());
}

TEST_P(McpAiFilterIntegrationTest, PropagatesTheTraceContextUpstream) {
  initializeFilter("propagate_trace_context: {}");

  roundTrip(
      postHeaders(),
      R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"test_tool",)"
      R"("_meta":{"traceparent":"00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"}}})");

  EXPECT_EQ("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01",
            upstream_request_->headers()
                .get(Http::LowerCaseString("traceparent"))[0]
                ->value()
                .getStringView());
}

TEST_P(McpAiFilterIntegrationTest, AppliesTheRouteOverride) {
  initializeFilter("traffic_mode: PASS_THROUGH", "traffic_mode: REJECT_NO_MCP");

  auto response = rejected(postHeaders(), R"({"model": "gpt-4"})");

  EXPECT_EQ("400", response->headers().getStatusValue());
  EXPECT_THAT(response->body(), testing::HasSubstr("JSON-RPC 2.0"));
}

// A route selected on the attributes the filter publishes.
TEST_P(McpAiFilterIntegrationTest, ClearsTheRouteCacheToRouteOnAttributes) {
  addManager("clear_route_cache: true");
  config_helper_.addConfigModifier([](HttpConnectionManager& hcm) {
    auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
    std::ignore =
        (*virtual_host->mutable_typed_per_filter_config())[ManagerName].PackFrom(aiEndpoint());
    virtual_host->clear_routes();
    TestUtility::loadFromYaml(R"EOF(
      match:
        prefix: "/"
        dynamic_metadata:
        - filter: envoy.filters.http.mcp
          path:
          - key: params
          - key: name
          value:
            string_match:
              exact: probe_tool
      direct_response:
        status: 200
        body:
          inline_string: per-tool
    )EOF",
                              *virtual_host->add_routes());
    TestUtility::loadFromYaml(R"EOF(
      match:
        prefix: "/"
      direct_response:
        status: 200
        body:
          inline_string: catch-all
    )EOF",
                              *virtual_host->add_routes());
  });
  initialize();
  codec_client_ = makeHttpConnection(lookupPort("http"));

  auto response1 = codec_client_->makeRequestWithBody(
      postHeaders(),
      R"({"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "probe_tool"}, "id": 1})");
  ASSERT_TRUE(response1->waitForEndStream());
  EXPECT_EQ("per-tool", response1->body());

  auto response2 = codec_client_->makeRequestWithBody(
      postHeaders(),
      R"({"jsonrpc": "2.0", "method": "tools/call", "params": {"name": "other_tool"}, "id": 2})");
  ASSERT_TRUE(response2->waitForEndStream());
  EXPECT_EQ("catch-all", response2->body());
}

// The response of an inspected request goes back through the manager, which re-serializes its
// JSON.
TEST_P(McpAiFilterIntegrationTest, ReturnsTheJsonRpcResponse) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  const std::string result = R"({"jsonrpc":"2.0","id":1,"result":{"content":[]}})";
  auto response = codec_client_->makeRequestWithBody(postHeaders(), ToolsCall);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  upstream_request_->encodeData(result, true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("200", response->headers().getStatusValue());
  EXPECT_TRUE(TestUtility::jsonStringEqual(response->body(), result)) << response->body();
}

TEST_P(McpAiFilterIntegrationTest, ReturnsTheSseResponse) {
  initializeFilter("traffic_mode: REJECT_NO_MCP");

  const std::string result = R"({"jsonrpc":"2.0","id":1,"result":{}})";
  auto response = codec_client_->makeRequestWithBody(postHeaders(), ToolsCall);
  waitForNextUpstreamRequest();
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  upstream_request_->encodeData(absl::StrCat("data: ", result, "\n\n"), true);
  ASSERT_TRUE(response->waitForEndStream());

  EXPECT_EQ("200", response->headers().getStatusValue());
  absl::string_view data = response->body();
  ASSERT_TRUE(absl::ConsumePrefix(&data, "data: ")) << response->body();
  ASSERT_TRUE(absl::ConsumeSuffix(&data, "\n\n")) << response->body();
  EXPECT_TRUE(TestUtility::jsonStringEqual(std::string(data), result)) << response->body();
}

} // namespace
} // namespace Mcp
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
