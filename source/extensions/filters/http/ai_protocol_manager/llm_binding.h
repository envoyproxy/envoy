#pragma once

#include <functional>
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

// Looks up a request header by lowercase name, returning nullopt if absent.
using HeaderLookup = std::function<std::optional<std::string>(absl::string_view name)>;

// A request header edit for the caller to apply (`value` sets the header; nullopt removes it).
struct HeaderMutation {
  std::string name;
  std::optional<std::string> value;
};

// The runtime form of an `envoy.type.ai.v3.LLMBinding`: how a provider carries an API on HTTP,
// where that differs from the API's own. Built once, then shared and never changed.
class LlmBinding {
public:
  // One edit, from a `KeyValueMutation` (body) or a `HeaderMutation` (header).
  struct Mutation {
    // A top-level body member. Empty for a header mutation.
    std::string body_field;
    // A request header, by its lower-case name. Empty for a body mutation.
    std::string header;
    // nullopt removes the field or header. In a string, `{name}` is replaced by that request
    // value. A header's value is always a string.
    std::optional<nlohmann::json> value;
    // Write only where the field or header is absent (`ADD_IF_ABSENT`).
    bool keep_existing{false};

    bool isHeader() const { return !header.empty(); }
  };

  static absl::StatusOr<LlmBindingConstSharedPtr>
  create(const envoy::type::ai::v3::LLMBinding& proto);
  // A filter state value: a URI pattern (`/...`), or an LLMBinding in JSON (`{...}`). Whitespace
  // around it is ignored.
  static absl::StatusOr<LlmBindingConstSharedPtr> parse(absl::string_view text);

  // `pattern` nullopt: the API's own path. `mutations` apply after the API's own edits, each
  // replacing the API's edit to the same field or header, if any.
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

// What a request's path, body and headers held under the binding it arrived in.
struct LlmBindingRead {
  LlmBindingConstSharedPtr binding;
  // The path's `{model}`; empty when it has none.
  std::string model;
  // The path's other named variables, such as `{project}`. They never enter the body.
  absl::flat_hash_map<std::string, std::string> path_variables;
  // The body fields the binding writes, as the client sent them.
  absl::flat_hash_map<std::string, nlohmann::json> written_fields;
  // The headers the binding writes, as the client sent them.
  absl::flat_hash_map<std::string, std::string> written_headers;
};

// Reads `path` into `body` under `binding`, with `api_binding` filling the parts it leaves empty,
// or, when `binding` is null, under the first of `api_binding` and `client_bindings` that matches.
// `{model}` and, where its spellings differ in the path, the `{unary|stream}` choice become the
// body's `model` and `stream`, which the path overrides for a field the binding removes, and the
// body fields the binding writes are taken out. The headers it writes are recorded from `header`
// for the caller to take out (`LlmBindingRead::written_headers`). nullopt, and `body` untouched,
// when no binding matches.
std::optional<LlmBindingRead>
readClientPath(const LlmBindingConstSharedPtr& binding, const LlmBindingConstSharedPtr& api_binding,
               absl::Span<const LlmBindingConstSharedPtr> client_bindings, absl::string_view path,
               nlohmann::json& body, const HeaderLookup& header);

// Renders the path for `body` under `binding`, with `api_binding` filling the parts it leaves
// empty, and applies its mutations in order: body edits to `body`, header edits to `headers` for
// the caller to apply, deciding `keep_existing` against `header`. `{model}` is the body's model as
// rendered; any other variable is one of `arrived`'s path variables. A field or header the request
// leaves in the binding it arrived in gets the client's value back. Returns nullopt when there is
// no pattern to render. On failure, `unresolved` says whether a value the client's path did not
// hold was needed, which is the proxy's configuration at fault rather than the request.
absl::StatusOr<std::optional<std::string>>
writeRequest(const LlmBinding& binding, const LlmBinding* api_binding,
             const LlmBindingRead* arrived, nlohmann::json& body, const HeaderLookup& header,
             std::vector<HeaderMutation>& headers, bool& unresolved);

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
