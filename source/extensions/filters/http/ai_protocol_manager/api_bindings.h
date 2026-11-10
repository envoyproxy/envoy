#pragma once

#include <optional>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/strings/string_view.h"
#include "nlohmann/json_fwd.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// The bindings Envoy knows an API by: shared by the transcoder's engine and the filters ahead of
// it, so every reader of a request path agrees on what it names.
struct ApiBindings {
  // The API's own layout, when it is more than the body: Gemini's path names the model and the
  // streaming mode, Anthropic's header the API version. Null when the body carries everything, as
  // for the OpenAI API.
  LlmBindingConstSharedPtr api{};
  // Other layouts a client may arrive in, tried after `api`, such as Vertex AI's.
  std::vector<LlmBindingConstSharedPtr> client{};
};

// `protocol`'s bindings; both empty for an API Envoy knows no layout of.
const ApiBindings& apiBindings(LLMProtocol protocol);

// What `path` names for a `protocol` request under `binding` -- the route's or filter state's,
// null when neither names one -- or else under the API's own bindings, with `body` left as it is.
// nullopt when no binding matches.
std::optional<LlmBindingRead> peekClientPath(LLMProtocol protocol,
                                             const LlmBindingConstSharedPtr& binding,
                                             absl::string_view path, const nlohmann::json& body);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
