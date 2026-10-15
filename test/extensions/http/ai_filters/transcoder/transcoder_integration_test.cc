#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/type/ai/v3/llm_protocol.pb.h"

#include "test/integration/http_integration.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "absl/strings/substitute.h"
#include "nlohmann/json.hpp"
#include "re2/re2.h"

namespace Envoy {
namespace {

using testing::ElementsAre;

constexpr absl::string_view kClaudeModel = "claude-sonnet-4-5@20250929";
constexpr absl::string_view kGeminiModel = "gemini-2.5-flash";
constexpr absl::string_view kVertexClaudePath =
    "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/claude-sonnet-4-5@20250929";
constexpr absl::string_view kVertexGeminiPath =
    "/v1/projects/acme/locations/us-east5/publishers/google/models/gemini-2.5-flash";
constexpr absl::string_view kSystem = "You are an expert in Envoy and distributed systems.";
constexpr absl::string_view kQuestion = "Explain how Envoy routes an HTTP request.";
constexpr absl::string_view kStreamQuestion = "Explain HTTP/2 flow control step by step.";

// The route's credential, as `gcp_authn` would set it: bindings leave credentials to the route.
constexpr absl::string_view kAccessToken = "Bearer test-access-token";

absl::Status checkAuthorization(const Http::RequestHeaderMap& headers) {
  const auto authorization = headers.get(Http::LowerCaseString("authorization"));
  if (authorization.empty() || authorization[0]->value().getStringView() != kAccessToken) {
    return absl::UnauthenticatedError("Request is missing required authentication credential.");
  }
  return absl::OkStatus();
}

// The request rules of Gemini on Vertex AI's `generateContent` and `streamGenerateContent`: the
// model and the method in the path, and a `GenerateContentRequest` body. Vertex AI rejects a field
// it does not know, so a client field left in the body fails the request.
absl::Status checkVertexGeminiRequest(const Http::RequestHeaderMap& headers, absl::string_view body,
                                      bool stream) {
  static const re2::RE2 path("/v1/projects/[a-z][a-z0-9-]*/locations/[a-z0-9-]+/publishers/google/"
                             "models/[a-z0-9.@-]+:(generateContent|streamGenerateContent)"
                             "(\\?alt=sse)?");
  std::string method;
  std::string alt;
  if (headers.getMethodValue() != "POST" ||
      !RE2::FullMatch(headers.getPathValue(), path, &method, &alt)) {
    return absl::NotFoundError(absl::StrCat("no such Vertex AI method: ", headers.getPathValue()));
  }
  if ((method == "streamGenerateContent") != stream || alt.empty() == stream) {
    return absl::InvalidArgumentError(
        absl::StrCat("expected the ", stream ? "SSE stream" : "unary", " method"));
  }
  if (absl::Status status = checkAuthorization(headers); !status.ok()) {
    return status;
  }

  static const absl::flat_hash_set<std::string> kRequestFields = {
      "contents",         "systemInstruction", "system_instruction",
      "cachedContent",    "cached_content",    "tools",
      "toolConfig",       "tool_config",       "labels",
      "safetySettings",   "safety_settings",   "generationConfig",
      "generation_config"};
  static const absl::flat_hash_set<std::string> kGenerationConfigFields = {"temperature",
                                                                           "topP",
                                                                           "topK",
                                                                           "candidateCount",
                                                                           "maxOutputTokens",
                                                                           "stopSequences",
                                                                           "presencePenalty",
                                                                           "frequencyPenalty",
                                                                           "seed",
                                                                           "responseMimeType",
                                                                           "responseSchema",
                                                                           "responseJsonSchema",
                                                                           "thinkingConfig"};
  const nlohmann::json request = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (!request.is_object()) {
    return absl::InvalidArgumentError("Invalid JSON payload received.");
  }
  for (const auto& [name, value] : request.items()) {
    if (!kRequestFields.contains(name)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "Invalid JSON payload received. Unknown name \"", name, "\": Cannot find field."));
    }
  }
  const auto contents = request.find("contents");
  if (contents == request.end() || !contents->is_array() || contents->empty()) {
    return absl::InvalidArgumentError("contents is not specified");
  }
  for (const nlohmann::json& content : *contents) {
    const auto role = content.find("role");
    const auto parts = content.find("parts");
    if (role == content.end() || (*role != "user" && *role != "model") || parts == content.end() ||
        !parts->is_array() || parts->empty()) {
      return absl::InvalidArgumentError(absl::StrCat("invalid content: ", content.dump()));
    }
  }
  if (const auto config = request.find("generationConfig"); config != request.end()) {
    for (const auto& [name, value] : config->items()) {
      if (!kGenerationConfigFields.contains(name)) {
        return absl::InvalidArgumentError(
            absl::StrCat("Invalid JSON payload received. Unknown name \"", name,
                         "\" at 'generation_config': Cannot find field."));
      }
    }
  }
  return absl::OkStatus();
}

// The request rules of Claude on Vertex AI's `rawPredict` and `streamRawPredict`: an Anthropic
// Messages body with the model in the path alone, the Vertex AI API version in
// `anthropic_version`, and `stream` matching the method.
absl::Status checkVertexClaudeRequest(const Http::RequestHeaderMap& headers, absl::string_view body,
                                      bool stream) {
  static const re2::RE2 path("/v1/projects/[a-z][a-z0-9-]*/locations/[a-z0-9-]+/publishers/"
                             "anthropic/models/[a-z0-9.@-]+:(rawPredict|streamRawPredict)");
  std::string method;
  if (headers.getMethodValue() != "POST" ||
      !RE2::FullMatch(headers.getPathValue(), path, &method)) {
    return absl::NotFoundError(absl::StrCat("no such Vertex AI method: ", headers.getPathValue()));
  }
  if ((method == "streamRawPredict") != stream) {
    return absl::InvalidArgumentError(
        absl::StrCat("expected the ", stream ? "streaming" : "unary", " method"));
  }
  if (absl::Status status = checkAuthorization(headers); !status.ok()) {
    return status;
  }

  static const absl::flat_hash_set<std::string> kRequestFields = {"anthropic_version",
                                                                  "max_tokens",
                                                                  "messages",
                                                                  "system",
                                                                  "stream",
                                                                  "temperature",
                                                                  "top_p",
                                                                  "top_k",
                                                                  "stop_sequences",
                                                                  "metadata",
                                                                  "tools",
                                                                  "tool_choice",
                                                                  "thinking",
                                                                  "service_tier"};
  const nlohmann::json request = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (!request.is_object()) {
    return absl::InvalidArgumentError("invalid JSON body");
  }
  for (const auto& [name, value] : request.items()) {
    if (!kRequestFields.contains(name)) {
      return absl::InvalidArgumentError(absl::StrCat(name, ": Extra inputs are not permitted"));
    }
  }
  if (request.value("anthropic_version", "") != "vertex-2023-10-16") {
    return absl::InvalidArgumentError("anthropic_version: Input should be 'vertex-2023-10-16'");
  }
  if (const auto max_tokens = request.find("max_tokens");
      max_tokens == request.end() || !max_tokens->is_number_integer()) {
    return absl::InvalidArgumentError("max_tokens: Field required");
  }
  const auto messages = request.find("messages");
  if (messages == request.end() || !messages->is_array() || messages->empty()) {
    return absl::InvalidArgumentError("messages: Field required");
  }
  for (const nlohmann::json& message : *messages) {
    const auto role = message.find("role");
    if (role == message.end() || (*role != "user" && *role != "assistant") ||
        !message.contains("content")) {
      return absl::InvalidArgumentError(absl::StrCat("invalid message: ", message.dump()));
    }
  }
  const auto streaming = request.find("stream");
  if (streaming == request.end() ? stream : !streaming->is_boolean() || *streaming != stream) {
    return absl::InvalidArgumentError("`stream` does not match the method");
  }
  return absl::OkStatus();
}

int httpStatus(const absl::Status& status) {
  switch (status.code()) {
  case absl::StatusCode::kNotFound:
    return 404;
  case absl::StatusCode::kUnauthenticated:
    return 401;
  default:
    return 400;
  }
}

// Gemini on Vertex AI's `GenerateContentResponse`s for "Hello from Gemini.": the unary response,
// and the stream's chunks, the last of which finishes the candidate and counts the tokens.
constexpr absl::string_view kGeminiUnaryResponse = R"({
  "candidates": [{
    "content": {"role": "model", "parts": [{"text": "Hello from Gemini."}]},
    "finishReason": "STOP", "avgLogprobs": -0.21
  }],
  "usageMetadata": {
    "promptTokenCount": 9, "candidatesTokenCount": 5, "totalTokenCount": 14,
    "trafficType": "ON_DEMAND",
    "promptTokensDetails": [{"modality": "TEXT", "tokenCount": 9}],
    "candidatesTokensDetails": [{"modality": "TEXT", "tokenCount": 5}]
  },
  "modelVersion": "gemini-2.5-flash", "createTime": "2026-10-15T12:00:00.000000Z",
  "responseId": "vertex-response-1"
})";
constexpr absl::string_view kGeminiStreamChunks[] = {
    R"({"candidates":[{"content":{"role":"model","parts":[{"text":"Hello"}]}}],)"
    R"("usageMetadata":{"trafficType":"ON_DEMAND"},"modelVersion":"gemini-2.5-flash",)"
    R"("createTime":"2026-10-15T12:00:00.000000Z","responseId":"vertex-response-1"})",
    R"({"candidates":[{"content":{"role":"model","parts":[{"text":" from Gemini."}]},)"
    R"("finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":9,)"
    R"("candidatesTokenCount":5,"totalTokenCount":14,"trafficType":"ON_DEMAND"},)"
    R"("modelVersion":"gemini-2.5-flash","createTime":"2026-10-15T12:00:00.000000Z",)"
    R"("responseId":"vertex-response-1"})",
};

// Claude on Vertex AI's Messages response for "Hello from Claude.", and its event stream.
constexpr absl::string_view kClaudeUnaryResponse = R"({
  "id": "msg_vrtx_01", "type": "message", "role": "assistant",
  "model": "claude-sonnet-4-5-20250929",
  "content": [{"type": "text", "text": "Hello from Claude."}],
  "stop_reason": "end_turn", "stop_sequence": null,
  "usage": {"input_tokens": 12, "output_tokens": 6}
})";
constexpr std::pair<absl::string_view, absl::string_view> kClaudeStreamEvents[] = {
    {"message_start",
     R"({"type":"message_start","message":{"id":"msg_vrtx_01","type":"message",)"
     R"("role":"assistant","model":"claude-sonnet-4-5-20250929","content":[],)"
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

// The events of an SSE body, by name, with their JSON data.
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

// Partner models on Vertex AI behind one route, in both directions: a Claude on Vertex AI client
// sent to Gemini on Vertex AI, and a Gemini on Vertex AI client sent to Claude on Vertex AI. The
// route declares only the client's API; a filter ahead of the AI Protocol Manager names the
// backend's API, its binding and the model, and the client's path supplies the project and the
// region. The upstream answers as Vertex AI does, so a request off its spec fails the test.
class TranscoderIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  TranscoderIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  // Gemini's own edits apply to a binding with only a pattern: `model` and `stream` leave the body.
  void initializeForGemini() {
    initializeRoute(envoy::type::ai::v3::ANTHROPIC_MESSAGES, "GEMINI_GENERATE_CONTENT",
                    "/{version}/projects/{project}/locations/{location}/publishers/google/models/"
                    "{model}:{generateContent|streamGenerateContent?alt=sse}",
                    kGeminiModel);
  }

  // Claude on Vertex AI needs edits of its own, so the binding is spelled out in JSON.
  void initializeForClaude() {
    initializeRoute(
        envoy::type::ai::v3::GEMINI_GENERATE_CONTENT, "ANTHROPIC_MESSAGES",
        R"({"uri_pattern": "/{version}/projects/{project}/locations/{location}/publishers/)"
        R"(anthropic/models/{model}:{rawPredict|streamRawPredict}", "mutations": [)"
        R"({"body_field": "model"}, {"body_field": "anthropic_version", )"
        R"("value": "vertex-2023-10-16", "keep_existing": true}]})",
        kClaudeModel);
  }

  IntegrationStreamDecoderPtr sendClientRequest(absl::string_view path,
                                                const nlohmann::json& body) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    return codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", std::string(path)},
                                       {":scheme", "http"},
                                       {":authority", "us-east5-aiplatform.googleapis.com"},
                                       {"content-type", "application/json"}},
        body.dump());
  }

  // Answers the upstream request as Gemini on Vertex AI would.
  void serveAsVertexGemini(bool stream) {
    if (!receiveUpstreamRequest(checkVertexGeminiRequest, stream)) {
      return;
    }
    if (!stream) {
      replyJson(kGeminiUnaryResponse);
      return;
    }
    replySseHeaders();
    // Gemini on Vertex AI ends each event with CRLFs and the stream without `[DONE]`.
    for (size_t i = 0; i < std::size(kGeminiStreamChunks); ++i) {
      upstream_request_->encodeData(absl::StrCat("data: ", kGeminiStreamChunks[i], "\r\n\r\n"),
                                    i + 1 == std::size(kGeminiStreamChunks));
    }
  }

  // Answers the upstream request as Claude on Vertex AI would.
  void serveAsVertexClaude(bool stream) {
    if (!receiveUpstreamRequest(checkVertexClaudeRequest, stream)) {
      return;
    }
    if (!stream) {
      replyJson(kClaudeUnaryResponse);
      return;
    }
    replySseHeaders();
    for (size_t i = 0; i < std::size(kClaudeStreamEvents); ++i) {
      const auto& [name, data] = kClaudeStreamEvents[i];
      upstream_request_->encodeData(absl::StrCat("event: ", name, "\ndata: ", data, "\n\n"),
                                    i + 1 == std::size(kClaudeStreamEvents));
    }
  }

  // The request went out to `path` in the client's project and region, with `body`.
  void expectUpstreamRequest(absl::string_view path, const nlohmann::json& body) {
    EXPECT_EQ(upstream_request_->headers().getPathValue(), path);
    EXPECT_EQ(upstream_request_->headers().getHostValue(), "us-east5-aiplatform.googleapis.com");
    EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-vertex-location", "us-east5"));
    EXPECT_EQ(nlohmann::json::parse(upstream_body_), body);
  }

private:
  void initializeRoute(envoy::type::ai::v3::LLMProtocol client_api, absl::string_view backend_api,
                       absl::string_view response_binding, absl::string_view resolved_model) {
    config_helper_.addConfigModifier([client_api](ConfigHelper::HttpConnectionManager& hcm) {
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
          per_route;
      per_route.mutable_request()->set_llm_protocol(client_api);
      auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
      std::ignore =
          (*route->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
              .PackFrom(per_route);
      auto* authorization = route->mutable_request_headers_to_add()->Add();
      authorization->mutable_header()->set_key("authorization");
      authorization->mutable_header()->set_value(std::string(kAccessToken));
      authorization->set_append_action(
          envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD);
      // What the client's path held, read through field access, as a dynamic forward proxy
      // would read the region for the upstream host.
      auto* location = route->mutable_request_headers_to_add()->Add()->mutable_header();
      location->set_key("x-vertex-location");
      location->set_value("%FILTER_STATE(envoy.ai.binding.read:FIELD:location)%");
    });

    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.ai_protocol_manager
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ai_protocol_manager.v3.AiProtocolManager
  request_handling: {}
  filters:
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
)EOF");

    config_helper_.prependFilter(absl::Substitute(R"EOF(
name: envoy.filters.http.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
  on_request_headers:
  - object_key: envoy.ai.llm_protocol.response
    format_string:
      text_format_source:
        inline_string: $0
  - object_key: envoy.ai.binding.response
    format_string:
      text_format_source:
        inline_string: '$1'
  - object_key: envoy.ai.model.resolved
    format_string:
      text_format_source:
        inline_string: '$2'
)EOF",
                                                  backend_api, response_binding, resolved_model));

    initialize();
  }

  // Waits for the upstream request and checks it against `check`; a request off the spec gets the
  // error status Vertex AI would send, and fails the test.
  bool receiveUpstreamRequest(absl::Status (*check)(const Http::RequestHeaderMap&,
                                                    absl::string_view, bool),
                              bool stream) {
    waitForNextUpstreamRequest();
    // `body()` drains what the stream received, so it is read once.
    upstream_body_ = upstream_request_->body().toString();
    const absl::Status valid = check(upstream_request_->headers(), upstream_body_, stream);
    if (valid.ok()) {
      return true;
    }
    ADD_FAILURE() << "Vertex AI would refuse the request: " << valid;
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", absl::StrCat(httpStatus(valid))},
                                        {"content-type", "application/json; charset=UTF-8"}},
        false);
    upstream_request_->encodeData(
        nlohmann::json{{"error", {{"code", httpStatus(valid)}, {"message", valid.message()}}}}
            .dump(),
        true);
    return false;
  }

  void replyJson(absl::string_view body) {
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"},
                                        {"content-type", "application/json; charset=UTF-8"}},
        false);
    upstream_request_->encodeData(std::string(body), true);
  }

  void replySseHeaders() {
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
        false);
  }

  std::string upstream_body_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TranscoderIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TranscoderIntegrationTest, VertexClaudeRawPredictReachesGeminiGenerateContent) {
  initializeForGemini();
  IntegrationStreamDecoderPtr response =
      sendClientRequest(absl::StrCat(kVertexClaudePath, ":rawPredict"),
                        {{"anthropic_version", "vertex-2023-10-16"},
                         {"max_tokens", 1024},
                         {"stream", false},
                         {"system", kSystem},
                         {"messages", {{{"role", "user"}, {"content", kQuestion}}}}});
  serveAsVertexGemini(/*stream=*/false);
  expectUpstreamRequest(absl::StrCat(kVertexGeminiPath, ":generateContent"),
                        {{"systemInstruction", {{"parts", {{{"text", kSystem}}}}}},
                         {"contents", {{{"role", "user"}, {"parts", {{{"text", kQuestion}}}}}}},
                         {"generationConfig", {{"maxOutputTokens", 1024}}}});

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->headers().getStatusValue(), "200") << response->body();
  const nlohmann::json message = nlohmann::json::parse(response->body());
  EXPECT_EQ(message["type"], "message");
  EXPECT_EQ(message["role"], "assistant");
  // The client hears back about the model it asked for, not the one that served it.
  EXPECT_EQ(message["model"], kClaudeModel);
  EXPECT_EQ(message["content"],
            nlohmann::json::parse(R"([{"type": "text", "text": "Hello from Gemini."}])"));
  EXPECT_EQ(message["stop_reason"], "end_turn");
  EXPECT_EQ(message["usage"]["input_tokens"], 9);
  EXPECT_EQ(message["usage"]["output_tokens"], 5);
  test_server_->waitForCounter("ai_protocol_manager.transcoder.transcoded", testing::Eq(4));
  EXPECT_EQ(test_server_->counter("ai_protocol_manager.transcoder.failed")->value(), 0);
}

TEST_P(TranscoderIntegrationTest, VertexClaudeStreamRawPredictReachesGeminiStreamGenerateContent) {
  initializeForGemini();
  IntegrationStreamDecoderPtr response =
      sendClientRequest(absl::StrCat(kVertexClaudePath, ":streamRawPredict"),
                        {{"anthropic_version", "vertex-2023-10-16"},
                         {"max_tokens", 1024},
                         {"stream", true},
                         {"messages", {{{"role", "user"}, {"content", kStreamQuestion}}}}});
  serveAsVertexGemini(/*stream=*/true);
  expectUpstreamRequest(
      absl::StrCat(kVertexGeminiPath, ":streamGenerateContent?alt=sse"),
      {{"contents", {{{"role", "user"}, {"parts", {{{"text", kStreamQuestion}}}}}}},
       {"generationConfig", {{"maxOutputTokens", 1024}}}});

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->headers().getStatusValue(), "200") << response->body();
  EXPECT_EQ(response->headers().getContentTypeValue(), "text/event-stream");
  const std::vector<std::pair<std::string, nlohmann::json>> events = sseEvents(response->body());
  std::vector<std::string> names;
  std::string text;
  for (const auto& [name, data] : events) {
    names.push_back(name);
    if (name == "content_block_delta") {
      text += data["delta"]["text"].get<std::string>();
    }
  }
  EXPECT_THAT(names, ElementsAre("message_start", "content_block_start", "content_block_delta",
                                 "content_block_delta", "content_block_stop", "message_delta",
                                 "message_stop"));
  ASSERT_EQ(events.size(), 7);
  EXPECT_EQ(events[0].second["message"]["model"], kClaudeModel);
  EXPECT_EQ(text, "Hello from Gemini.");
  EXPECT_EQ(events[5].second["delta"]["stop_reason"], "end_turn");
  EXPECT_EQ(events[5].second["usage"]["input_tokens"], 9);
  EXPECT_EQ(events[5].second["usage"]["output_tokens"], 5);
  EXPECT_EQ(test_server_->counter("ai_protocol_manager.transcoder.failed")->value(), 0);
}

TEST_P(TranscoderIntegrationTest, VertexGeminiGenerateContentReachesClaudeRawPredict) {
  initializeForClaude();
  IntegrationStreamDecoderPtr response =
      sendClientRequest(absl::StrCat(kVertexGeminiPath, ":generateContent"),
                        {{"systemInstruction", {{"parts", {{{"text", kSystem}}}}}},
                         {"contents", {{{"role", "user"}, {"parts", {{{"text", kQuestion}}}}}}},
                         {"generationConfig", {{"maxOutputTokens", 1024}}}});
  serveAsVertexClaude(/*stream=*/false);
  expectUpstreamRequest(absl::StrCat(kVertexClaudePath, ":rawPredict"),
                        {{"anthropic_version", "vertex-2023-10-16"},
                         {"max_tokens", 1024},
                         {"stream", false},
                         {"system", kSystem},
                         {"messages", {{{"role", "user"}, {"content", kQuestion}}}}});

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->headers().getStatusValue(), "200") << response->body();
  const nlohmann::json reply = nlohmann::json::parse(response->body());
  ASSERT_EQ(reply["candidates"].size(), 1);
  EXPECT_EQ(
      reply["candidates"][0]["content"],
      nlohmann::json::parse(R"({"role": "model", "parts": [{"text": "Hello from Claude."}]})"));
  EXPECT_EQ(reply["candidates"][0]["finishReason"], "STOP");
  EXPECT_EQ(reply["modelVersion"], kGeminiModel);
  EXPECT_EQ(reply["usageMetadata"]["promptTokenCount"], 12);
  EXPECT_EQ(reply["usageMetadata"]["candidatesTokenCount"], 6);
  test_server_->waitForCounter("ai_protocol_manager.transcoder.transcoded", testing::Eq(4));
  EXPECT_EQ(test_server_->counter("ai_protocol_manager.transcoder.failed")->value(), 0);
}

TEST_P(TranscoderIntegrationTest, VertexGeminiStreamGenerateContentReachesClaudeStreamRawPredict) {
  initializeForClaude();
  IntegrationStreamDecoderPtr response = sendClientRequest(
      absl::StrCat(kVertexGeminiPath, ":streamGenerateContent?alt=sse"),
      {{"contents", {{{"role", "user"}, {"parts", {{{"text", kStreamQuestion}}}}}}},
       {"generationConfig", {{"maxOutputTokens", 1024}}}});
  serveAsVertexClaude(/*stream=*/true);
  expectUpstreamRequest(absl::StrCat(kVertexClaudePath, ":streamRawPredict"),
                        {{"anthropic_version", "vertex-2023-10-16"},
                         {"max_tokens", 1024},
                         {"stream", true},
                         {"messages", {{{"role", "user"}, {"content", kStreamQuestion}}}}});

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->headers().getStatusValue(), "200") << response->body();
  EXPECT_EQ(response->headers().getContentTypeValue(), "text/event-stream");
  // Gemini streams unnamed chunks and no `[DONE]`.
  EXPECT_THAT(response->body(), testing::Not(testing::HasSubstr("[DONE]")));
  std::string text;
  nlohmann::json last;
  for (const auto& [name, data] : sseEvents(response->body())) {
    EXPECT_EQ(name, "");
    EXPECT_EQ(data["modelVersion"], kGeminiModel);
    for (const nlohmann::json& part : data["candidates"][0]["content"]["parts"]) {
      text += part["text"].get<std::string>();
    }
    last = data;
  }
  EXPECT_EQ(text, "Hello from Claude.");
  EXPECT_EQ(last["candidates"][0]["finishReason"], "STOP");
  EXPECT_EQ(last["usageMetadata"]["candidatesTokenCount"], 6);
  EXPECT_EQ(test_server_->counter("ai_protocol_manager.transcoder.failed")->value(), 0);
}

} // namespace
} // namespace Envoy
