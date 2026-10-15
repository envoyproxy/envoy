#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/type/ai/v3/llm_binding.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/uri_pattern.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/span.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

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

  static absl::StatusOr<std::shared_ptr<const LlmBinding>>
  create(const envoy::type::ai::v3::LLMBinding& proto);
  // A filter state value: a URI pattern (`/...`), or an LLMBinding in JSON (`{...}`).
  static absl::StatusOr<std::shared_ptr<const LlmBinding>> parse(absl::string_view text);

  // `pattern` nullopt: the API's own path. `mutations` empty: the API's own edits.
  LlmBinding(std::optional<UriPattern> pattern, std::vector<Mutation> mutations);

  const std::optional<UriPattern>& pattern() const { return pattern_; }
  const std::vector<Mutation>& mutations() const { return mutations_; }
  // Whether a mutation removes `model` or `stream`, which the path then carries alone.
  bool removesModel() const { return removes_model_; }
  bool removesStream() const { return removes_stream_; }
  // The names the mutations' `{name}` placeholders read, other than `model`.
  const std::vector<std::string>& placeholders() const { return placeholders_; }

private:
  std::optional<UriPattern> pattern_;
  std::vector<Mutation> mutations_;
  bool removes_model_{false};
  bool removes_stream_{false};
  std::vector<std::string> placeholders_;
};
using LlmBindingConstSharedPtr = std::shared_ptr<const LlmBinding>;

// The pattern `binding` reads and renders: its own, or `api_binding`'s when it has none. Null
// when neither has one.
const UriPattern* patternOf(const LlmBinding& binding, const LlmBinding* api_binding);
// The binding whose mutations apply: `binding`, or `api_binding` when `binding` has none.
const LlmBinding& editsOf(const LlmBinding& binding, const LlmBinding* api_binding);

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

// Reads `path` under `binding` into `body`: `{model}` and the `{unary|stream}` choice become the
// body's `model` and `stream`, which the path overrides for a field the binding removes, and the
// body fields the binding writes are taken out. `api_binding` fills the parts `binding` leaves
// empty. nullopt, and `body` untouched, when `path` does not match.
std::optional<LlmBindingRead> readRequest(const LlmBindingConstSharedPtr& binding,
                                          const LlmBinding* api_binding, absl::string_view path,
                                          nlohmann::json& body);

// Reads `path` under `binding`, or, when it is null, under the first of `api_binding` and
// `client_bindings` that matches.
std::optional<LlmBindingRead>
readClientPath(const LlmBindingConstSharedPtr& binding, const LlmBindingConstSharedPtr& api_binding,
               absl::Span<const LlmBindingConstSharedPtr> client_bindings, absl::string_view path,
               nlohmann::json& body);

// Fails when `writeRequest()` under `binding` would need a value the client's path did not
// supply: a variable or placeholder other than `model` missing from `arrived`'s path variables,
// or an unnamed wildcard. Such a failure is the proxy's configuration at fault.
absl::Status checkResolvable(const LlmBinding& binding, const LlmBinding* api_binding,
                             const LlmBindingRead* arrived);

// Renders the path for `body` under `binding` and applies its mutations, in order. `{model}` is
// the body's model; any other variable is one of `arrived`'s path variables. When the request
// leaves in the binding it arrived in, the body fields that binding wrote get the client's values
// back. Returns nullopt when there is no pattern to render.
absl::StatusOr<std::optional<std::string>> writeRequest(const LlmBinding& binding,
                                                        const LlmBinding* api_binding,
                                                        const LlmBindingRead* arrived,
                                                        nlohmann::json& body);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
