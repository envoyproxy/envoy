#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/type/ai/v3/llm_binding.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class LlmBinding;
using LlmBindingConstSharedPtr = std::shared_ptr<const LlmBinding>;

// The runtime form of an `envoy.type.ai.v3.LLMBinding`: how a provider carries an API on HTTP,
// where that differs from the API's own. Built once, then shared and never changed.
class LlmBinding {
public:
  struct Mutation {
    // A top-level body member.
    std::string body_field;
    // nullopt removes the field. In a string, `{name}` is replaced by that request value.
    std::optional<nlohmann::json> value;
    // Write only where the field is absent.
    bool keep_existing{false};
  };

  static absl::StatusOr<LlmBindingConstSharedPtr>
  create(const envoy::type::ai::v3::LLMBinding& proto);
  // A filter state value: a URI pattern (`/...`), or an LLMBinding in JSON (`{...}`).
  static absl::StatusOr<LlmBindingConstSharedPtr> parse(absl::string_view text);

  // `pattern` nullopt: the API's own path. `mutations` empty: the API's own edits.
  LlmBinding(std::optional<UriPattern>&& pattern, std::vector<Mutation>&& mutations);

  const std::optional<UriPattern>& pattern() const { return pattern_; }
  const std::vector<Mutation>& mutations() const { return mutations_; }
  // Whether a mutation removes `model` or `stream`, which the path then carries alone.
  bool removesModel() const { return removes_model_; }
  bool removesStream() const { return removes_stream_; }

private:
  std::optional<UriPattern> pattern_;
  std::vector<Mutation> mutations_;
  bool removes_model_{false};
  bool removes_stream_{false};
};

// What a request's path and body held under the binding it arrived in.
struct LlmBindingRead {
  LlmBindingConstSharedPtr binding;
  // The path's `{model}`; empty when it has none.
  std::string model;
  // The path's other named variables, such as `{project}`. They never enter the body.
  absl::flat_hash_map<std::string, std::string> path_variables;
  // The body fields the binding writes, as the client sent them.
  absl::flat_hash_map<std::string, nlohmann::json> written_fields;
};

// Reads `path` into `body` under `binding`, with `api_binding` filling the parts it leaves empty,
// or, when `binding` is null, under the first of `api_binding` and `client_bindings` that matches.
// `{model}` and, where its spellings differ in the path, the `{unary|stream}` choice become the
// body's `model` and `stream`, which the path overrides for a field the binding removes, and the
// body fields the binding writes are taken out. nullopt, and `body` untouched, when no binding
// matches.
std::optional<LlmBindingRead>
readClientPath(const LlmBindingConstSharedPtr& binding, const LlmBindingConstSharedPtr& api_binding,
               absl::Span<const LlmBindingConstSharedPtr> client_bindings, absl::string_view path,
               nlohmann::json& body);

// Renders the path for `body` under `binding`, with `api_binding` filling the parts it leaves
// empty, and applies its mutations in order. `{model}` is the body's model as rendered; any other
// variable is one of `arrived`'s path variables. A field the request leaves in the binding it
// arrived in gets the client's value back. Returns nullopt when there is no pattern to render. On
// failure, `unresolved` says whether a value the client's path did not hold was needed, which is
// the proxy's configuration at fault rather than the request.
absl::StatusOr<std::optional<std::string>> writeRequest(const LlmBinding& binding,
                                                        const LlmBinding* api_binding,
                                                        const LlmBindingRead* arrived,
                                                        nlohmann::json& body, bool& unresolved);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
