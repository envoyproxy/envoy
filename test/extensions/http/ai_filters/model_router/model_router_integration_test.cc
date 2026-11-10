#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/type/ai/v3/llm_protocol.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace {

constexpr absl::string_view Question = "Explain how Envoy routes an HTTP request.";

constexpr absl::string_view ClaudeUnaryResponse = R"({
  "id": "msg_01", "type": "message", "role": "assistant",
  "model": "claude-haiku-5-5",
  "content": [{"type": "text", "text": "Hello from Claude."}],
  "stop_reason": "end_turn", "stop_sequence": null,
  "usage": {"input_tokens": 12, "output_tokens": 6}
})";

constexpr absl::string_view ClaudeSonnetUnaryResponse = R"({
  "id": "msg_02", "type": "message", "role": "assistant",
  "model": "claude-sonnet-5-5",
  "content": [{"type": "text", "text": "Hello from Claude Sonnet."}],
  "stop_reason": "end_turn", "stop_sequence": null,
  "usage": {"input_tokens": 12, "output_tokens": 6}
})";

constexpr std::pair<absl::string_view, absl::string_view> ClaudeStreamEvents[] = {
    {"message_start",
     R"({"type":"message_start","message":{"id":"msg_01","type":"message",)"
     R"("role":"assistant","model":"claude-haiku-5-5","content":[],)"
     R"("stop_reason":null,"stop_sequence":null,"usage":{"input_tokens":12,"output_tokens":1}}})"},
    {"content_block_start",
     R"({"type":"content_block_start","index":0,"content_block":{"type":"text","text":""}})"},
    {"ping", R"({"type":"ping"})"},
    {"content_block_delta",
     R"({"type":"content_block_delta","index":0,"delta":{"type":"text_delta","text":"Hello"}})"},
    {"content_block_delta", R"({"type":"content_block_delta","index":0,)"
                            R"("delta":{"type":"text_delta","text":" from Claude."}})"},
    {"content_block_stop", R"({"type":"content_block_stop","index":0})"},
    {"message_delta", R"({"type":"message_delta","delta":{"stop_reason":"end_turn",)"
                      R"("stop_sequence":null},"usage":{"output_tokens":6}})"},
    {"message_stop", R"({"type":"message_stop"})"},
};

std::vector<std::pair<std::string, nlohmann::json>> sseEvents(absl::string_view body) {
  std::vector<std::pair<std::string, nlohmann::json>> events;
  const std::string text = absl::StrReplaceAll(body, {{"\r\n", "\n"}});
  for (absl::string_view block : absl::StrSplit(text, "\n\n", absl::SkipEmpty())) {
    std::pair<std::string, nlohmann::json> event;
    for (absl::string_view line : absl::StrSplit(block, '\n')) {
      if (absl::ConsumePrefix(&line, "event: ")) {
        event.first = std::string(line);
      } else if (absl::ConsumePrefix(&line, "data: ")) {
        event.second = nlohmann::json::parse(line);
      }
    }
    events.push_back(std::move(event));
  }
  return events;
}

constexpr absl::string_view DownstreamAiProtocolManagerConfig = R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  request_handling: {}
  filters:
  - name: envoy.http.ai_filters.schema_validation
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.ai_filters.schema_validation.v3.SchemaValidation
  - name: envoy.http.ai_filters.request_info
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.ai_filters.request_info.v3.RequestInfo
  - name: envoy.http.ai_filters.model_router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.ai_filters.model_router.v3.ModelRouter
      providers:
      - name: anthropic
        llm_protocol: ANTHROPIC_MESSAGES
        host_and_port: "api.anthropic.com"
        binding:
          uri_pattern: "/v1/messages"
      models:
      - name: antroclaude-haiku
        provider: anthropic
        model: "claude-haiku-5-5"
        api_key:
          inline_string: "Bearer ANTHROPIC_KEY"
      - name: antroclaude-sonnet
        provider: anthropic
        model: "claude-sonnet-5-5"
        api_key:
          inline_string: "Bearer ANTHROPIC_KEY"
      routes:
      - requested_model: "send_me_to_anthropic"
        targets:
        - model: antroclaude-haiku
        - model: antroclaude-sonnet
)EOF";

constexpr absl::string_view RouterWithUpstreamAiProtocolManagerConfig = R"EOF(
name: envoy.filters.http.router
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  upstream_http_filters:
  - name: envoy.filters.http.ai_protocol_manager
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
      request_handling: {}
      response_handling:
        token_usage: {}
      filters:
      - name: envoy.http.ai_filters.route_selector
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.route_selector.v3.RouteSelector
      - name: envoy.http.ai_filters.transcoder
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.transcoder.v3.Transcoder
          request_handling: TO_IR
          response_handling: FROM_IR
      - name: envoy.http.ai_filters.transcoder
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.http.ai_filters.transcoder.v3.Transcoder
          request_handling: FROM_IR
          response_handling: TO_IR
  - name: envoy.filters.http.upstream_codec
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.upstream_codec.v3.UpstreamCodec
)EOF";

class ModelRouterIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                   public HttpIntegrationTest {
public:
  ModelRouterIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    // Upstream 0 represents the Anthropic cluster.
    setUpstreamCount(1);

    config_helper_.addConfigModifier([ip_version = GetParam()](
                                         envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()->clear_clusters();

      auto* cluster = bootstrap.mutable_static_resources()->add_clusters();
      cluster->set_name("anthropic_cluster");
      cluster->mutable_connect_timeout()->set_seconds(5);
      cluster->set_type(envoy::config::cluster::v3::Cluster::STATIC);
      cluster->set_lb_policy(envoy::config::cluster::v3::Cluster::ROUND_ROBIN);

      auto* load_assignment = cluster->mutable_load_assignment();
      load_assignment->set_cluster_name(cluster->name());
      auto* endpoints = load_assignment->add_endpoints();
      auto* lb_endpoint = endpoints->add_lb_endpoints();
      auto* address = lb_endpoint->mutable_endpoint()->mutable_address()->mutable_socket_address();
      address->set_address(Network::Test::getLoopbackAddressString(ip_version));
      address->set_port_value(0);
    });

    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* virtual_host = hcm.mutable_route_config()->mutable_virtual_hosts(0);
          virtual_host->set_include_request_attempt_count(true);

          auto* route = virtual_host->mutable_routes(0);
          route->mutable_match()->set_path("/chat/completions");
          route->mutable_route()->set_cluster("anthropic_cluster");

          auto* retry_policy = route->mutable_route()->mutable_retry_policy();
          retry_policy->set_retry_on("5xx,connect-failure,reset,retriable-status-codes");
          retry_policy->add_retriable_status_codes(429);
          retry_policy->mutable_num_retries()->set_value(2);

          envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
              per_route;
          per_route.mutable_request()->set_llm_protocol(
              envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
          std::ignore =
              (*route->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
                  .PackFrom(per_route);

          auto* gemini_route = virtual_host->add_routes();
          gemini_route->mutable_match()->set_prefix("/v1beta/models/");
          gemini_route->mutable_route()->set_cluster("anthropic_cluster");
          *gemini_route->mutable_route()->mutable_retry_policy() = *retry_policy;

          envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
              gemini_per_route;
          gemini_per_route.mutable_request()->set_llm_protocol(
              envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
          std::ignore =
              (*gemini_route
                    ->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
                  .PackFrom(gemini_per_route);

          hcm.clear_http_filters();
          TestUtility::loadFromYaml(std::string(DownstreamAiProtocolManagerConfig),
                                    *hcm.add_http_filters());
          TestUtility::loadFromYaml(std::string(RouterWithUpstreamAiProtocolManagerConfig),
                                    *hcm.add_http_filters());
        });

    HttpIntegrationTest::initialize();
    test_server_->waitForGauge("cluster_manager.active_clusters", testing::Ge(1));
  }

  IntegrationStreamDecoderPtr sendChatCompletionsRequest() {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    const nlohmann::json request_body = {
        {"model", "send_me_to_anthropic"},
        {"max_tokens", 1024},
        {"messages", {{{"role", "user"}, {"content", Question}}}},
    };
    return codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", "/chat/completions"},
                                       {":scheme", "http"},
                                       {":authority", "ai-gateway.example.com"},
                                       {"authorization", "Bearer client-original-token"},
                                       {"content-type", "application/json"}},
        request_body.dump());
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ModelRouterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ModelRouterIntegrationTest, FirstTargetSucceeds) {
  initialize();

  IntegrationStreamDecoderPtr response = sendChatCompletionsRequest();

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  const Http::RequestHeaderMap& headers = upstream_request_->headers();
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().Method, "POST"));
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().Path, "/v1/messages"));
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().Host, "api.anthropic.com"));
  EXPECT_THAT(headers,
              ContainsHeader(Http::CustomHeaders::get().Authorization, "Bearer ANTHROPIC_KEY"));
  EXPECT_THAT(headers, ContainsHeader("anthropic-version", "2023-06-01"));
  EXPECT_EQ(nlohmann::json::parse(upstream_request_->body().toString()),
            (nlohmann::json{
                {"model", "claude-haiku-5-5"},
                {"max_tokens", 1024},
                {"messages", {{{"role", "user"}, {"content", Question}}}},
            }));

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  upstream_request_->encodeData(std::string(ClaudeUnaryResponse), true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_THAT(response->headers(), Http::HttpStatusIs("200")) << response->body();

  const nlohmann::json reply = nlohmann::json::parse(response->body());
  EXPECT_EQ(reply["object"], "chat.completion");
  ASSERT_EQ(reply["choices"].size(), 1);
  EXPECT_EQ(reply["choices"][0]["message"]["role"], "assistant");
  EXPECT_EQ(reply["choices"][0]["message"]["content"], "Hello from Claude.");
  EXPECT_EQ(reply["usage"]["prompt_tokens"], 12);
  EXPECT_EQ(reply["usage"]["completion_tokens"], 6);

  EXPECT_EQ(1, test_server_->counter("cluster.anthropic_cluster.upstream_rq_total")->value());
}

TEST_P(ModelRouterIntegrationTest, MultiModelFallbackAcrossRetries) {
  initialize();

  IntegrationStreamDecoderPtr response = sendChatCompletionsRequest();

  // Attempt 1 goes to the Anthropic cluster (`fake_upstreams_[0]`) with the primary model and fails
  // with 503.
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  const Http::RequestHeaderMap& attempt1_headers = upstream_request_->headers();
  EXPECT_THAT(attempt1_headers, ContainsHeader(Http::Headers::get().Method, "POST"));
  EXPECT_THAT(attempt1_headers, ContainsHeader(Http::Headers::get().Path, "/v1/messages"));
  EXPECT_THAT(attempt1_headers, ContainsHeader(Http::Headers::get().Host, "api.anthropic.com"));
  EXPECT_THAT(attempt1_headers,
              ContainsHeader(Http::CustomHeaders::get().Authorization, "Bearer ANTHROPIC_KEY"));
  EXPECT_THAT(attempt1_headers, ContainsHeader("anthropic-version", "2023-06-01"));
  EXPECT_EQ(nlohmann::json::parse(upstream_request_->body().toString()),
            (nlohmann::json{
                {"model", "claude-haiku-5-5"},
                {"max_tokens", 1024},
                {"messages", {{{"role", "user"}, {"content", Question}}}},
            }));

  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();

  // Attempt 2 fails over to the fallback model on the same provider (`fake_upstreams_[0]`).
  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  const Http::RequestHeaderMap& attempt2_headers = upstream_request_->headers();
  EXPECT_THAT(attempt2_headers, ContainsHeader(Http::Headers::get().Method, "POST"));
  EXPECT_THAT(attempt2_headers, ContainsHeader(Http::Headers::get().Path, "/v1/messages"));
  EXPECT_THAT(attempt2_headers, ContainsHeader(Http::Headers::get().Host, "api.anthropic.com"));
  EXPECT_THAT(attempt2_headers,
              ContainsHeader(Http::CustomHeaders::get().Authorization, "Bearer ANTHROPIC_KEY"));
  EXPECT_THAT(attempt2_headers, ContainsHeader("anthropic-version", "2023-06-01"));
  EXPECT_EQ(nlohmann::json::parse(upstream_request_->body().toString()),
            (nlohmann::json{
                {"model", "claude-sonnet-5-5"},
                {"max_tokens", 1024},
                {"messages", {{{"role", "user"}, {"content", Question}}}},
            }));

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  upstream_request_->encodeData(std::string(ClaudeSonnetUnaryResponse), true);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_THAT(response->headers(), Http::HttpStatusIs("200")) << response->body();

  const nlohmann::json reply = nlohmann::json::parse(response->body());
  EXPECT_EQ(reply["object"], "chat.completion");
  ASSERT_EQ(reply["choices"].size(), 1);
  EXPECT_EQ(reply["choices"][0]["message"]["role"], "assistant");
  EXPECT_EQ(reply["choices"][0]["message"]["content"], "Hello from Claude Sonnet.");
  EXPECT_EQ(reply["usage"]["prompt_tokens"], 12);
  EXPECT_EQ(reply["usage"]["completion_tokens"], 6);

  EXPECT_EQ(2, test_server_->counter("cluster.anthropic_cluster.upstream_rq_total")->value());
}

TEST_P(ModelRouterIntegrationTest, GeminiSseStreamGenerateContentToAnthropicMessages) {
  initialize();

  codec_client_ = makeHttpConnection(lookupPort("http"));
  const nlohmann::json request_body = {
      {"contents", {{{"role", "user"}, {"parts", {{{"text", Question}}}}}}},
      {"generationConfig", {{"maxOutputTokens", 1024}}},
  };
  IntegrationStreamDecoderPtr response = codec_client_->makeRequestWithBody(
      Http::TestRequestHeaderMapImpl{
          {":method", "POST"},
          {":path", "/v1beta/models/send_me_to_anthropic:streamGenerateContent?alt=sse"},
          {":scheme", "http"},
          {":authority", "ai-gateway.example.com"},
          {"x-goog-api-key", "client-google-key"},
          {"content-type", "application/json"}},
      request_body.dump());

  ASSERT_TRUE(fake_upstreams_[0]->waitForHttpConnection(*dispatcher_, fake_upstream_connection_));
  ASSERT_TRUE(fake_upstream_connection_->waitForNewStream(*dispatcher_, upstream_request_));
  ASSERT_TRUE(upstream_request_->waitForEndStream(*dispatcher_));

  const Http::RequestHeaderMap& headers = upstream_request_->headers();
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().Method, "POST"));
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().Path, "/v1/messages"));
  EXPECT_THAT(headers, ContainsHeader(Http::Headers::get().Host, "api.anthropic.com"));
  EXPECT_THAT(headers,
              ContainsHeader(Http::CustomHeaders::get().Authorization, "Bearer ANTHROPIC_KEY"));
  EXPECT_THAT(headers, ContainsHeader("anthropic-version", "2023-06-01"));
  EXPECT_TRUE(headers.get(Http::LowerCaseString("x-goog-api-key")).empty());
  EXPECT_EQ(nlohmann::json::parse(upstream_request_->body().toString()),
            (nlohmann::json{
                {"model", "claude-haiku-5-5"},
                {"max_tokens", 1024},
                {"stream", true},
                {"messages", {{{"role", "user"}, {"content", Question}}}},
            }));

  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
      false);
  for (size_t i = 0; i < std::size(ClaudeStreamEvents); ++i) {
    const auto& [name, data] = ClaudeStreamEvents[i];
    upstream_request_->encodeData(absl::StrCat("event: ", name, "\ndata: ", data, "\n\n"),
                                  i + 1 == std::size(ClaudeStreamEvents));
  }

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_THAT(response->headers(), Http::HttpStatusIs("200")) << response->body();
  EXPECT_EQ(response->headers().getContentTypeValue(), "text/event-stream");
  EXPECT_THAT(response->body(), testing::Not(testing::HasSubstr("[DONE]")));

  std::string text;
  nlohmann::json last;
  for (const auto& [name, data] : sseEvents(response->body())) {
    EXPECT_EQ(name, "");
    EXPECT_EQ(data["modelVersion"], "send_me_to_anthropic");
    for (const nlohmann::json& part : data["candidates"][0]["content"]["parts"]) {
      text += part["text"].get<std::string>();
    }
    last = data;
  }
  EXPECT_EQ(text, "Hello from Claude.");
  EXPECT_EQ(last["candidates"][0]["finishReason"], "STOP");
  EXPECT_EQ(last["usageMetadata"]["candidatesTokenCount"], 6);
  EXPECT_EQ(1, test_server_->counter("cluster.anthropic_cluster.upstream_rq_total")->value());
}

} // namespace
} // namespace Envoy
