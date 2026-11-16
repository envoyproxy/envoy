#include "source/extensions/filters/http/ai_protocol_manager/api_bindings.h"

#include <memory>
#include <utility>

#include "source/common/common/macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// A binding Envoy defines. Its pattern is a constant, so it parses.
LlmBindingConstSharedPtr builtInBinding(absl::string_view pattern,
                                        std::vector<LlmBinding::Mutation>&& mutations) {
  return std::make_shared<const LlmBinding>(*UriPattern::parse(pattern), std::move(mutations));
}

// Gemini names the model and the streaming mode in the path alone.
std::vector<LlmBinding::Mutation> geminiMutations() {
  return {{.body_field = "model"}, {.body_field = "stream"}};
}

ApiBindings anthropicBindings() {
  return ApiBindings{
      // The Anthropic API's layout: a fixed path, with the API version in a header rather than in
      // the body or the path. A request leaves in it unless a binding names another.
      .api = builtInBinding("/v1/messages", {{.header = "anthropic-version",
                                              .value = nlohmann::json("2023-06-01"),
                                              .keep_existing = true}}),
      // Claude on Vertex AI: the model in the path alone, and the Vertex API version in the body
      // rather than in the API's header, which is removed so it does not leak to Vertex.
      .client = {builtInBinding(
          "/{version}/projects/{project}/locations/{location}/publishers/anthropic/models/"
          "{model}:{rawPredict|streamRawPredict}",
          {{.body_field = "model"},
           {.body_field = "anthropic_version",
            .value = nlohmann::json("vertex-2023-10-16"),
            .keep_existing = true},
           {.header = "anthropic-version"}})},
  };
}

ApiBindings geminiBindings() {
  return ApiBindings{
      // The Gemini API's layout, under `v1beta` as rendered and under any other version as
      // received. A request leaves in it unless a binding names another, such as Vertex AI's,
      // which nests the same methods under a project and a location.
      .api = builtInBinding(
          "/v1beta/models/{model}:{generateContent|streamGenerateContent?alt=sse} "
          "/{version}/models/{model}:{generateContent|streamGenerateContent?alt=sse}",
          geminiMutations()),
      .client = {builtInBinding(
          "/{version}/projects/{project}/locations/{location}/publishers/google/models/"
          "{model}:{generateContent|streamGenerateContent?alt=sse}",
          geminiMutations())},
  };
}

} // namespace

const ApiBindings& apiBindings(LLMProtocol protocol) {
  switch (protocol) {
  case LLMProtocol::AnthropicMessages: {
    CONSTRUCT_ON_FIRST_USE(ApiBindings, anthropicBindings());
  }
  case LLMProtocol::GeminiGenerateContent: {
    CONSTRUCT_ON_FIRST_USE(ApiBindings, geminiBindings());
  }
  default: {
    CONSTRUCT_ON_FIRST_USE(ApiBindings);
  }
  }
}

std::optional<LlmBindingRead> peekClientPath(LLMProtocol protocol,
                                             const LlmBindingConstSharedPtr& binding,
                                             absl::string_view path, const nlohmann::json& body) {
  const ApiBindings& api = apiBindings(protocol);
  return peekClientPath(binding, api.api, api.client, path, body);
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
