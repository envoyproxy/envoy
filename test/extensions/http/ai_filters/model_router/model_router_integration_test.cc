#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
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

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    if (enable_rls_) {
      addFakeUpstream(Http::CodecType::HTTP2);
    }
  }

  void initialize() override {
    // Upstream 0 represents the Anthropic cluster.
    setUpstreamCount(1);

    config_helper_.addConfigModifier([this, ip_version = GetParam()](
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

      if (enable_rls_) {
        auto* rls_cluster = bootstrap.mutable_static_resources()->add_clusters();
        rls_cluster->MergeFrom(*cluster);
        rls_cluster->set_name("rls_cluster");
        rls_cluster->mutable_load_assignment()->set_cluster_name("rls_cluster");
        ConfigHelper::setHttp2(*rls_cluster);
      }
    });

    config_helper_.addConfigModifier(
        [this](
            envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
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
          auto* downstream_filter = hcm.add_http_filters();
          TestUtility::loadFromYaml(std::string(DownstreamAiProtocolManagerConfig),
                                    *downstream_filter);
          if (enable_rls_) {
            envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager ai_mgr;
            ASSERT_TRUE(downstream_filter->typed_config().UnpackTo(&ai_mgr));
            for (auto& filter : *ai_mgr.mutable_filters()) {
              if (filter.name() == "envoy.http.ai_filters.model_router") {
                envoy::extensions::http::ai_filters::model_router::v3::ModelRouter router_proto;
                ASSERT_TRUE(filter.typed_config().UnpackTo(&router_proto));
                envoy::extensions::http::ai_filters::model_router::v3::RlsQuotaConfig rls_config;
                rls_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name(
                    "rls_cluster");
                rls_config.mutable_timeout()->set_seconds(5);
                router_proto.mutable_quota_config()->set_name("rls");
                ASSERT_TRUE(router_proto.mutable_quota_config()->mutable_typed_config()->PackFrom(
                    rls_config));
                ASSERT_TRUE(filter.mutable_typed_config()->PackFrom(router_proto));
              }
            }
            ASSERT_TRUE(downstream_filter->mutable_typed_config()->PackFrom(ai_mgr));
          }
          TestUtility::loadFromYaml(std::string(RouterWithUpstreamAiProtocolManagerConfig),
                                    *hcm.add_http_filters());
        });

    HttpIntegrationTest::initialize();
    test_server_->waitForGauge("cluster_manager.active_clusters", testing::Ge(enable_rls_ ? 2 : 1));
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

  void waitForAnthropicUpstreamRequest(absl::string_view expected_model, bool stream = false) {
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

    nlohmann::json expected_body = {
        {"model", expected_model},
        {"max_tokens", 1024},
        {"messages", {{{"role", "user"}, {"content", Question}}}},
    };
    if (stream) {
      expected_body["stream"] = true;
    }
    EXPECT_EQ(nlohmann::json::parse(upstream_request_->body().toString()), expected_body);
  }

  bool enable_rls_{false};
  FakeHttpConnectionPtr fake_rls_connection_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ModelRouterIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ModelRouterIntegrationTest, FirstTargetSucceeds) {
  initialize();

  IntegrationStreamDecoderPtr response = sendChatCompletionsRequest();
  waitForAnthropicUpstreamRequest("claude-haiku-5-5");

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
  waitForAnthropicUpstreamRequest("claude-haiku-5-5");
  upstream_request_->encodeHeaders(Http::TestResponseHeaderMapImpl{{":status", "503"}}, true);
  ASSERT_TRUE(fake_upstream_connection_->close());
  ASSERT_TRUE(fake_upstream_connection_->waitForDisconnect());
  fake_upstream_connection_.reset();

  // Attempt 2 fails over to the fallback model on the same provider (`fake_upstreams_[0]`).
  waitForAnthropicUpstreamRequest("claude-sonnet-5-5");
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

  waitForAnthropicUpstreamRequest("claude-haiku-5-5", /*stream=*/true);
  EXPECT_TRUE(upstream_request_->headers().get(Http::LowerCaseString("x-goog-api-key")).empty());

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

TEST_P(ModelRouterIntegrationTest, RlsQuotaFiltersOverLimitModelAndConsumesActualTokens) {
  enable_rls_ = true;
  initialize();

  IntegrationStreamDecoderPtr response = sendChatCompletionsRequest();

  ASSERT_TRUE(fake_upstreams_[1]->waitForHttpConnection(*dispatcher_, fake_rls_connection_));

  // 1. CheckRateLimit for primary candidate model (`claude-haiku-5-5`) returns OVER_LIMIT.
  FakeStreamPtr check_haiku_stream;
  ASSERT_TRUE(fake_rls_connection_->waitForNewStream(*dispatcher_, check_haiku_stream));
  envoy::service::ratelimit::v3::RateLimitRequest check_haiku_req;
  ASSERT_TRUE(check_haiku_stream->waitForGrpcMessage(*dispatcher_, check_haiku_req));
  ASSERT_TRUE(check_haiku_stream->waitForEndStream(*dispatcher_));
  EXPECT_EQ(check_haiku_stream->headers().getPathValue(),
            "/envoy.service.ratelimit.v3.RateLimitService/CheckRateLimit");
  EXPECT_EQ(check_haiku_req.domain(), "model_router");
  ASSERT_EQ(check_haiku_req.descriptors_size(), 2);
  EXPECT_EQ(check_haiku_req.descriptors(0).entries(1).value(), "claude-haiku-5-5");

  check_haiku_stream->startGrpcStream();
  envoy::service::ratelimit::v3::RateLimitResponse over_limit_resp;
  over_limit_resp.set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OVER_LIMIT);
  check_haiku_stream->sendGrpcMessage(over_limit_resp);
  check_haiku_stream->finishGrpcStream(Grpc::Status::Ok);

  // 2. CheckRateLimit for fallback candidate model (`claude-sonnet-5-5`) returns OK.
  FakeStreamPtr check_sonnet_stream;
  ASSERT_TRUE(fake_rls_connection_->waitForNewStream(*dispatcher_, check_sonnet_stream));
  envoy::service::ratelimit::v3::RateLimitRequest check_sonnet_req;
  ASSERT_TRUE(check_sonnet_stream->waitForGrpcMessage(*dispatcher_, check_sonnet_req));
  ASSERT_TRUE(check_sonnet_stream->waitForEndStream(*dispatcher_));
  EXPECT_EQ(check_sonnet_stream->headers().getPathValue(),
            "/envoy.service.ratelimit.v3.RateLimitService/CheckRateLimit");
  ASSERT_EQ(check_sonnet_req.descriptors_size(), 2);
  EXPECT_EQ(check_sonnet_req.descriptors(0).entries(1).value(), "claude-sonnet-5-5");

  check_sonnet_stream->startGrpcStream();
  envoy::service::ratelimit::v3::RateLimitResponse ok_resp;
  ok_resp.set_overall_code(envoy::service::ratelimit::v3::RateLimitResponse::OK);
  check_sonnet_stream->sendGrpcMessage(ok_resp);
  check_sonnet_stream->finishGrpcStream(Grpc::Status::Ok);

  // 3. Request routes directly to `claude-sonnet-5-5` without trying `claude-haiku-5-5`.
  waitForAnthropicUpstreamRequest("claude-sonnet-5-5");
  upstream_request_->encodeHeaders(
      Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "application/json"}},
      false);
  upstream_request_->encodeData(std::string(ClaudeSonnetUnaryResponse), true);

  // 4. ConsumeRateLimit is called with 1 request and 12 + 6 = 18 tokens for `claude-sonnet-5-5`.
  FakeStreamPtr consume_stream;
  ASSERT_TRUE(fake_rls_connection_->waitForNewStream(*dispatcher_, consume_stream));
  envoy::service::ratelimit::v3::RateLimitRequest consume_req;
  ASSERT_TRUE(consume_stream->waitForGrpcMessage(*dispatcher_, consume_req));
  ASSERT_TRUE(consume_stream->waitForEndStream(*dispatcher_));
  EXPECT_EQ(consume_stream->headers().getPathValue(),
            "/envoy.service.ratelimit.v3.RateLimitService/ConsumeRateLimit");
  ASSERT_EQ(consume_req.descriptors_size(), 2);
  EXPECT_EQ(consume_req.descriptors(0).entries(1).value(), "claude-sonnet-5-5");
  EXPECT_EQ(consume_req.descriptors(0).hits_addend().value(), 1);
  EXPECT_EQ(consume_req.descriptors(1).entries(2).value(), "tokens");
  EXPECT_EQ(consume_req.descriptors(1).hits_addend().value(), 18);

  consume_stream->startGrpcStream();
  consume_stream->sendGrpcMessage(ok_resp);
  consume_stream->finishGrpcStream(Grpc::Status::Ok);

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_THAT(response->headers(), Http::HttpStatusIs("200")) << response->body();

  ASSERT_TRUE(fake_rls_connection_->close());
  ASSERT_TRUE(fake_rls_connection_->waitForDisconnect());
  cleanupUpstreamAndDownstream();
}

} // namespace
} // namespace Envoy
