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

// The runtime form of an `envoy.type.ai.v3.LLMBinding`: how a provider carries an API on HTTP,
// where that differs from the API's own. Built once, then shared and never changed.
struct LlmBinding {
  struct Mutation {
    enum class Target { BodyField, Header };

    Target target{Target::BodyField};
    // A top-level body member, or a lower-case header name.
    std::string name;
    // nullopt removes the target. `{name}` in a string is filled as a path variable is.
    std::optional<nlohmann::json> value;
    // Write only where the target is absent.
    bool keep_existing{false};
  };

  static absl::StatusOr<std::shared_ptr<const LlmBinding>>
  create(const envoy::type::ai::v3::LLMBinding& proto);
  // A filter state value: a URI pattern (`/...`), or an LLMBinding in JSON (`{...}`).
  static absl::StatusOr<std::shared_ptr<const LlmBinding>> parse(absl::string_view text);

  // `pattern`, or `api_binding`'s when this one has none; null when neither has one.
  const UriPattern* patternOver(const LlmBinding* api_binding) const;
  // `mutations`, or `api_binding`'s when this one has none.
  absl::Span<const Mutation> mutationsOver(const LlmBinding* api_binding) const;

  // nullopt: the API's own path.
  std::optional<UriPattern> pattern;
  // Empty: the API's own edits.
  std::vector<Mutation> mutations;
};
using LlmBindingConstSharedPtr = std::shared_ptr<const LlmBinding>;

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

// A header edit for the caller to apply: nullopt `value` removes the header.
struct HeaderMutation {
  std::string name;
  std::optional<std::string> value;
  bool keep_existing{false};
};

struct LlmBindingWrite {
  std::optional<std::string> path;
  std::vector<HeaderMutation> headers;
};

// Reads `path` under `binding` into `body`: `{model}` and the `{unary|stream}` choice become the
// body's `model` and `stream`, which the path overrides for a field the binding removes, and the
// body fields the binding writes are taken out. `api_binding` fills the parts `binding` leaves
// empty. nullopt, and `body` untouched, when `path` does not match.
std::optional<LlmBindingRead> readRequest(const LlmBindingConstSharedPtr& binding,
                                          const LlmBinding* api_binding, absl::string_view path,
                                          nlohmann::json& body);

// Renders the path for `body` under `binding` and applies its mutations, in order. A variable is
// filled from the body field of its name, else from `arrived`'s path variables. When the request
// leaves in the binding it arrived in, the body fields that binding wrote get the client's values
// back. Fails with `kFailedPrecondition` on a variable without a value, and `kInvalidArgument` on
// a value that cannot fill one.
absl::StatusOr<LlmBindingWrite> writeRequest(const LlmBinding& binding,
                                             const LlmBinding* api_binding,
                                             const LlmBindingRead* arrived, nlohmann::json& body);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
