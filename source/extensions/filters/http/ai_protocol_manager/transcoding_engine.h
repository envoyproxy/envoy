#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Declarative, schema-backed JSON transcoding rule specification.
//
// Styled after `Schema` (schema.h): each `TranscodeRule` describes a declarative
// field or structural transformation ("this field maps to that field", "this value
// maps to that value") without bespoke per-model C++ logic.
//
// All structural rules operate on `nlohmann::json` nodes via `std::move`, preserving
// `JsonWithExtBuf::ExternalRef` binary reference nodes in O(1) without materializing
// offloaded strings.
class TranscodeRule {
public:
  enum class Op {
    // Moves a field from `source_path` to `target_path` (if present).
    Move,
    // Moves the first present field in `source_paths` to `target_path`, dropping remaining
    // candidates in `source_paths`.
    FirstOf,
    // Removes `source_path` from the JSON object if present.
    Drop,
    // Sets `target_path` to `default_value` if `target_path` is absent or null.
    SetDefault,
    // Translates scalar string values at `target_path` according to `value_map`.
    ValueMap,
    // Applies `sub_rules` to every object element of the array at `target_path`.
    ForEach,
    // Partition/extracts elements from array `source_path` where `predicate_field` is in
    // `match_values`, moving their `extract_subpath` into `target_path` and keeping
    // remaining elements in `source_path`.
    ExtractFromArray,
    // Prepends an object element into array `target_path` constructed from `source_path`
    // (removing `source_path`), setting `predicate_field` = `role_value` and
    // `extract_subpath` = moved value.
    PrependToArray,
    // Wraps a field `source_path` into a single-element object array `target_path`:
    // `[{element_key: <moved source_path>}]` (if `source_path` is already an array, moves it).
    WrapInArrayObject,
    // Unwraps the first element's `element_key` from object array `source_path` into
    // `target_path` (e.g., `parts[0].text` -> `content`).
    UnwrapArrayObject,
    // Merges consecutive elements in array `target_path` that share the same `key_field`
    // value, combining their `merge_field` values.
    MergeConsecutiveByKey,
  };

  enum class UnknownValuePolicy {
    Passthrough,
    Drop,
    Reject,
  };

  struct ValueMapping {
    std::string from;
    std::string to;
  };

  // Static factory builders for declarative rule construction:

  // Moves `from_path` (dot-separated, e.g. "generationConfig.maxOutputTokens") to `to_path`.
  static TranscodeRule move(std::string from_path, std::string to_path);

  // Moves the first present field in `from_paths` to `to_path` and drops the remaining
  // candidates in `from_paths`.
  static TranscodeRule firstOf(std::initializer_list<std::string> from_paths, std::string to_path);

  // Removes `path` from the payload if present.
  static TranscodeRule drop(std::string path);

  // Sets `path` to `default_value` if `path` is missing or null.
  static TranscodeRule setDefault(std::string path, nlohmann::json default_value);

  // Maps string values at `path` using `mappings` ("this value should be interpreted as that").
  static TranscodeRule
  valueMap(std::string path, std::initializer_list<ValueMapping> mappings,
           UnknownValuePolicy unknown_policy = UnknownValuePolicy::Passthrough);

  // Runs `rules` on each element of the array at `array_path`.
  static TranscodeRule forEach(std::string array_path, std::initializer_list<TranscodeRule> rules);

  // Extracts elements from `array_path` whose `predicate_field` matches any of `match_values`.
  // The matched element's `extract_subpath` is moved to `target_path` (removing the element
  // from `array_path`).
  static TranscodeRule extractFromArray(std::string array_path, std::string predicate_field,
                                        std::initializer_list<std::string> match_values,
                                        std::string extract_subpath, std::string target_path);

  // Moves `source_path` into a new element prepended to `array_path`. The new element maps
  // `key_field` to `key_value` and holds the moved node under `value_subpath`.
  static TranscodeRule prependToArray(std::string source_path, std::string array_path,
                                      std::string key_field, std::string key_value,
                                      std::string value_subpath);

  // Wraps `from_path` inside the current object into `to_array_path: [{element_key: <moved>}]`.
  static TranscodeRule wrapInArrayObject(std::string from_path, std::string to_array_path,
                                         std::string element_key);

  // Unwraps `from_array_path[0][element_key]` into `to_path` inside the current object.
  static TranscodeRule unwrapArrayObject(std::string from_array_path, std::string element_key,
                                         std::string to_path);

  // Merges consecutive array elements at `array_path` that have identical `key_field` values
  // by concatenating/merging their `merge_field` values into an array of blocks or string.
  static TranscodeRule mergeConsecutiveByKey(std::string array_path, std::string key_field,
                                             std::string merge_field);

  // Introspection accessors (used by the startup Verifier and Executor):
  Op op() const { return op_; }
  const std::string& sourcePath() const { return source_path_; }
  const std::vector<std::string>& sourcePaths() const { return source_paths_; }
  const std::string& targetPath() const { return target_path_; }
  const std::string& predicateField() const { return predicate_field_; }
  const std::string& extractSubpath() const { return extract_subpath_; }
  const std::vector<std::string>& matchValues() const { return match_values_; }
  const nlohmann::json& defaultValue() const { return default_value_; }
  const absl::flat_hash_map<std::string, std::string>& valueMappings() const {
    return value_mappings_;
  }
  UnknownValuePolicy unknownValuePolicy() const { return unknown_policy_; }
  const std::vector<TranscodeRule>& subRules() const { return sub_rules_; }

  // Executes this rule in-place on `json`.
  absl::Status apply(nlohmann::json& json) const;

private:
  explicit TranscodeRule(Op op) : op_(op) {}

  Op op_;
  std::string source_path_;
  std::vector<std::string> source_paths_;
  std::string target_path_;
  std::string predicate_field_;
  std::string extract_subpath_;
  std::vector<std::string> match_values_;
  nlohmann::json default_value_;
  absl::flat_hash_map<std::string, std::string> value_mappings_;
  UnknownValuePolicy unknown_policy_{UnknownValuePolicy::Passthrough};
  std::vector<TranscodeRule> sub_rules_;
};

// A compiled, immutable sequence of declarative `TranscodeRule`s that transforms a payload
// from `source_protocol` to `target_protocol`.
class TranscodeRuleSet {
public:
  TranscodeRuleSet() = default;
  TranscodeRuleSet(ApiProtocol source_protocol, ApiProtocol target_protocol,
                   std::initializer_list<TranscodeRule> rules)
      : source_protocol_(source_protocol), target_protocol_(target_protocol), rules_(rules) {}
  TranscodeRuleSet(ApiProtocol source_protocol, ApiProtocol target_protocol,
                   std::vector<TranscodeRule> rules)
      : source_protocol_(source_protocol), target_protocol_(target_protocol),
        rules_(std::move(rules)) {}

  ApiProtocol sourceProtocol() const { return source_protocol_; }
  ApiProtocol targetProtocol() const { return target_protocol_; }
  const std::vector<TranscodeRule>& rules() const { return rules_; }

  // Executes all rules in order on `payload`.
  absl::Status execute(JsonWithExtBuf& payload) const { return execute(payload.json()); }
  absl::Status execute(nlohmann::json& json) const;

private:
  ApiProtocol source_protocol_{ApiProtocol::Unspecified};
  ApiProtocol target_protocol_{ApiProtocol::Unspecified};
  std::vector<TranscodeRule> rules_;
};

// Declarative dialect pack pairing a protocol's `inbound` (Protocol -> OpenAiChatCompletions)
// and `outbound` (OpenAiChatCompletions -> Protocol) rule sets with model-prefix selection.
//
// `dialect_schema` is the protocol's own `PayloadSchema`; it is what an outbound payload is
// validated against before it is handed to the upstream. `hub_schema` is only used for static
// rule verification at registration time -- see `TranscodingEngine::transcodeInbound()` for why
// the hub document itself is not validated at runtime.
struct DialectTranscodePack {
  ApiProtocol protocol{ApiProtocol::Unspecified};
  TranscodeRuleSet inbound;
  TranscodeRuleSet outbound;
  std::vector<std::string> model_prefixes;
  const PayloadSchema* dialect_schema{nullptr};
  const PayloadSchema* hub_schema{nullptr};
};

// The Transcoding Engine: manages registered `DialectTranscodePack`s, verifies them against
// `PayloadSchema` definitions at startup, resolves target protocols from model names, and
// executes inbound and outbound transcoding.
class TranscodingEngine {
public:
  static constexpr ApiProtocol kHubProtocol = ApiProtocol::OpenAiChatCompletions;

  TranscodingEngine() = default;

  // Builds the default engine pre-loaded with OpenAI Chat Completions, Anthropic Messages,
  // and Gemini GenerateContent declarative transcoding packs.
  static absl::StatusOr<TranscodingEngine> createDefault();

  // Statically verifies a rule set against `source_schema` at config load time.
  // Rejects any rule set where a value-reading rule (`ValueMap`) targets a field declared
  // `.offloadable()` in `source_schema`, since such a field may arrive as an `ExternalRef`
  // binary node rather than an inline string.
  static absl::Status validateRulesAgainstSchema(const TranscodeRuleSet& rules,
                                                 const PayloadSchema* source_schema = nullptr);

  // Registers a `DialectTranscodePack` after statically verifying its rule sets.
  //
  // `dialect_schema` and `hub_schema` override the corresponding fields on `pack` when
  // non-null; when null, whatever `pack` already carries is kept. This lets a caller either
  // pass the schemas here or set them directly on the struct, without one silently winning.
  absl::Status registerPack(DialectTranscodePack pack,
                            const PayloadSchema* dialect_schema = nullptr,
                            const PayloadSchema* hub_schema = nullptr);

  // Resolves the target `ApiProtocol` from a model identifier (e.g. "claude-sonnet-4" ->
  // `AnthropicMessages`, "gemini-2.5-pro" -> `GeminiGenerateContent`).
  ApiProtocol resolveTargetProtocol(absl::string_view model) const;

  // Step 1: Transcodes `payload` from `source_protocol` into the hub shape
  // (`OpenAiChatCompletions`). A no-op when `source_protocol` is the hub protocol.
  //
  // The result is deliberately NOT validated against the hub schema. Two reasons: the source
  // payload was already validated against its own schema by the AI Protocol Manager before the
  // filter chain ran, so re-validating is duplicated work on the hot path; and the hub schema
  // requires `model`, which a Gemini request legitimately does not carry in its body (it lives
  // in the request path), so validating here would reject valid Gemini traffic.
  absl::Status transcodeInbound(ApiProtocol source_protocol, JsonWithExtBuf& payload) const {
    return transcodeInbound(source_protocol, payload.json());
  }
  absl::Status transcodeInbound(ApiProtocol source_protocol, nlohmann::json& json) const;

  // Step 2: Transcodes `payload` from the hub shape (`OpenAiChatCompletions`) into
  // `target_protocol`, then validates it against that protocol's schema so a payload the
  // upstream would reject is caught here instead of over the network. Rule execution is skipped
  // when `target_protocol` is the hub protocol, but validation still runs.
  absl::Status transcodeOutbound(ApiProtocol target_protocol, JsonWithExtBuf& payload) const {
    return transcodeOutbound(target_protocol, payload.json());
  }
  absl::Status transcodeOutbound(ApiProtocol target_protocol, nlohmann::json& json) const;

  // End-to-end convenience helper: `source_protocol` -> Hub -> `target_protocol`.
  absl::Status transcode(ApiProtocol source_protocol, ApiProtocol target_protocol,
                         JsonWithExtBuf& payload) const {
    return transcode(source_protocol, target_protocol, payload.json());
  }
  absl::Status transcode(ApiProtocol source_protocol, ApiProtocol target_protocol,
                         nlohmann::json& json) const;

private:
  struct ModelPrefixEntry {
    std::string prefix;
    ApiProtocol protocol;
  };

  absl::flat_hash_map<ApiProtocol, DialectTranscodePack> packs_;
  std::vector<ModelPrefixEntry> model_prefixes_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
