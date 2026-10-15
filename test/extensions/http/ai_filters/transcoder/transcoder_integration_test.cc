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
#include "nlohmann/json.hpp"
#include "re2/re2.h"

namespace Envoy {
namespace {

using testing::ElementsAre;

constexpr absl::string_view kClientModel = "claude-sonnet-4-5@20250929";
constexpr absl::string_view kVertexClaudePath =
    "/v1/projects/acme/locations/us-east5/publishers/anthropic/models/claude-sonnet-4-5@20250929";

// A request of the AnthropicVertex SDK: the model is in the path, and the body carries the Vertex
// AI API version instead.
constexpr absl::string_view kClientBody = R"({
  "anthropic_version": "vertex-2023-10-16", "max_tokens": 256, "temperature": 0.2,
  "system": "Be brief.", "messages": [{"role": "user", "content": "Hi"}]
})";

// The request rules of Vertex AI's `generateContent` and `streamGenerateContent`: the model and the
// method in the path, and a `GenerateContentRequest` body. Vertex AI rejects a field it does not
// know, so a client field left in the body fails the request.
absl::Status checkVertexGeminiRequest(const Http::RequestHeaderMap& headers, absl::string_view body,
                                      bool stream) {
  static const re2::RE2 path("/v1/projects/[a-z][a-z0-9-]*/locations/[a-z0-9-]+/publishers/google/"
                             "models/([a-z0-9.@-]+):(generateContent|streamGenerateContent)"
                             "(\\?alt=sse)?");
  std::string method;
  std::string alt;
  if (headers.getMethodValue() != "POST" ||
      !RE2::FullMatch(headers.getPathValue(), path, nullptr, &method, &alt)) {
    return absl::NotFoundError(absl::StrCat("no such Vertex AI method: ", headers.getPathValue()));
  }
  if ((method == "streamGenerateContent") != stream || alt.empty() == stream) {
    return absl::InvalidArgumentError(
        absl::StrCat("expected the ", stream ? "SSE stream" : "unary", " method"));
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

// The error body Vertex AI sends with a 4xx.
std::string vertexError(const absl::Status& status) {
  const bool not_found = status.code() == absl::StatusCode::kNotFound;
  return nlohmann::json{{"error",
                         {{"code", not_found ? 404 : 400},
                          {"message", status.message()},
                          {"status", not_found ? "NOT_FOUND" : "INVALID_ARGUMENT"}}}}
      .dump();
}

// Vertex AI's `GenerateContentResponse`s for "Hello from Gemini.": the unary response, and the
// stream's chunks, the last of which finishes the candidate and counts the tokens.
constexpr absl::string_view kUnaryResponse = R"({
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
constexpr absl::string_view kStreamChunks[] = {
    R"({"candidates":[{"content":{"role":"model","parts":[{"text":"Hello"}]}}],)"
    R"("usageMetadata":{"trafficType":"ON_DEMAND"},"modelVersion":"gemini-2.5-flash",)"
    R"("createTime":"2026-10-15T12:00:00.000000Z","responseId":"vertex-response-1"})",
    R"({"candidates":[{"content":{"role":"model","parts":[{"text":" from Gemini."}]},)"
    R"("finishReason":"STOP"}],"usageMetadata":{"promptTokenCount":9,)"
    R"("candidatesTokenCount":5,"totalTokenCount":14,"trafficType":"ON_DEMAND"},)"
    R"("modelVersion":"gemini-2.5-flash","createTime":"2026-10-15T12:00:00.000000Z",)"
    R"("responseId":"vertex-response-1"})",
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

// Claude on Vertex AI's `rawPredict` and `streamRawPredict`, served by Gemini on Vertex AI. The
// route declares only the client's API; a filter ahead of the AI Protocol Manager names the
// backend's API, its binding and the model, and the client's path supplies the project and the
// region. The upstream answers as Vertex AI does, so a request off its spec fails the test.
class TranscoderIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                                  public HttpIntegrationTest {
public:
  TranscoderIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addConfigModifier([](ConfigHelper::HttpConnectionManager& hcm) {
      envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManagerPerRoute
          per_route;
      per_route.mutable_request()->set_llm_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
      auto* route = hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
      std::ignore =
          (*route->mutable_typed_per_filter_config())["envoy.filters.http.ai_protocol_manager"]
              .PackFrom(per_route);
      // What the client's path held, read through field access, as a dynamic forward proxy
      // would read the region for the upstream host.
      auto* location = route->mutable_request_headers_to_add()->Add()->mutable_header();
      location->set_key("x-vertex-location");
      location->set_value("%FILTER_STATE(envoy.ai.binding.request:FIELD:location)%");
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

    config_helper_.prependFilter(R"EOF(
name: envoy.filters.http.set_filter_state
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.set_filter_state.v3.Config
  on_request_headers:
  - object_key: envoy.ai.llm_protocol.response
    format_string:
      text_format_source:
        inline_string: GEMINI_GENERATE_CONTENT
  - object_key: envoy.ai.binding.response
    format_string:
      text_format_source:
        inline_string: "/{version}/projects/{project}/locations/{location}/publishers/google/models/{model}:{generateContent|streamGenerateContent?alt=sse}"
  - object_key: envoy.ai.model.resolved
    format_string:
      text_format_source:
        inline_string: gemini-2.5-flash
)EOF");

    HttpIntegrationTest::initialize();
  }

  IntegrationStreamDecoderPtr sendClientRequest(absl::string_view method, absl::string_view body) {
    codec_client_ = makeHttpConnection(lookupPort("http"));
    return codec_client_->makeRequestWithBody(
        Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                       {":path", absl::StrCat(kVertexClaudePath, ":", method)},
                                       {":scheme", "http"},
                                       {":authority", "us-east5-aiplatform.googleapis.com"},
                                       {"content-type", "application/json"}},
        std::string(body));
  }

  // Answers the upstream request as Vertex AI's Gemini endpoint would.
  void serveAsVertexGemini(bool stream) {
    waitForNextUpstreamRequest();
    // `body()` drains what the stream received, so it is read once.
    upstream_body_ = upstream_request_->body().toString();
    const absl::Status valid =
        checkVertexGeminiRequest(upstream_request_->headers(), upstream_body_, stream);
    if (!valid.ok()) {
      ADD_FAILURE() << "Vertex AI would refuse the request: " << valid;
      upstream_request_->encodeHeaders(
          Http::TestResponseHeaderMapImpl{
              {":status", valid.code() == absl::StatusCode::kNotFound ? "404" : "400"},
              {"content-type", "application/json; charset=UTF-8"}},
          false);
      upstream_request_->encodeData(vertexError(valid), true);
      return;
    }
    if (!stream) {
      upstream_request_->encodeHeaders(
          Http::TestResponseHeaderMapImpl{{":status", "200"},
                                          {"content-type", "application/json; charset=UTF-8"}},
          false);
      upstream_request_->encodeData(std::string(kUnaryResponse), true);
      return;
    }
    upstream_request_->encodeHeaders(
        Http::TestResponseHeaderMapImpl{{":status", "200"}, {"content-type", "text/event-stream"}},
        false);
    // Vertex AI ends each event with CRLFs and the stream without `[DONE]`.
    for (size_t i = 0; i < std::size(kStreamChunks); ++i) {
      upstream_request_->encodeData(absl::StrCat("data: ", kStreamChunks[i], "\r\n\r\n"),
                                    i + 1 == std::size(kStreamChunks));
    }
  }

  // The request went out as Gemini on Vertex AI in the client's project and region, for the
  // resolved model, with nothing of the Anthropic body left.
  void expectGeminiRequest(absl::string_view method) {
    EXPECT_EQ(upstream_request_->headers().getPathValue(),
              absl::StrCat("/v1/projects/acme/locations/us-east5/publishers/google/models/"
                           "gemini-2.5-flash:",
                           method));
    EXPECT_EQ(upstream_request_->headers().getHostValue(), "us-east5-aiplatform.googleapis.com");
    EXPECT_THAT(upstream_request_->headers(), ContainsHeader("x-vertex-location", "us-east5"));
    EXPECT_EQ(nlohmann::json::parse(upstream_body_), nlohmann::json::parse(R"({
      "systemInstruction": {"parts": [{"text": "Be brief."}]},
      "contents": [{"role": "user", "parts": [{"text": "Hi"}]}],
      "generationConfig": {"maxOutputTokens": 256, "temperature": 0.2}
    })"));
  }

  std::string upstream_body_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, TranscoderIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TranscoderIntegrationTest, RawPredictReachesGeminiGenerateContent) {
  initialize();
  IntegrationStreamDecoderPtr response = sendClientRequest("rawPredict", kClientBody);
  serveAsVertexGemini(/*stream=*/false);
  expectGeminiRequest("generateContent");

  ASSERT_TRUE(response->waitForEndStream());
  ASSERT_EQ(response->headers().getStatusValue(), "200") << response->body();
  const nlohmann::json message = nlohmann::json::parse(response->body());
  EXPECT_EQ(message["type"], "message");
  EXPECT_EQ(message["role"], "assistant");
  // The client hears back about the model it asked for, not the one that served it.
  EXPECT_EQ(message["model"], kClientModel);
  EXPECT_EQ(message["content"],
            nlohmann::json::parse(R"([{"type": "text", "text": "Hello from Gemini."}])"));
  EXPECT_EQ(message["stop_reason"], "end_turn");
  EXPECT_EQ(message["usage"]["input_tokens"], 9);
  EXPECT_EQ(message["usage"]["output_tokens"], 5);
  test_server_->waitForCounter("ai_protocol_manager.transcoder.transcoded", testing::Eq(4));
  EXPECT_EQ(test_server_->counter("ai_protocol_manager.transcoder.failed")->value(), 0);
}

TEST_P(TranscoderIntegrationTest, StreamRawPredictReachesGeminiStreamGenerateContent) {
  initialize();
  nlohmann::json body = nlohmann::json::parse(kClientBody);
  body["stream"] = true;
  IntegrationStreamDecoderPtr response = sendClientRequest("streamRawPredict", body.dump());
  serveAsVertexGemini(/*stream=*/true);
  expectGeminiRequest("streamGenerateContent?alt=sse");

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
  EXPECT_EQ(events[0].second["message"]["model"], kClientModel);
  EXPECT_EQ(text, "Hello from Gemini.");
  EXPECT_EQ(events[5].second["delta"]["stop_reason"], "end_turn");
  EXPECT_EQ(events[5].second["usage"]["input_tokens"], 9);
  EXPECT_EQ(events[5].second["usage"]["output_tokens"], 5);
  EXPECT_EQ(test_server_->counter("ai_protocol_manager.transcoder.failed")->value(), 0);
}

} // namespace
} // namespace Envoy
