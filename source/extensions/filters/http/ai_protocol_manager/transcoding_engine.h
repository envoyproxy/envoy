#pragma once

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema.h"
#include "source/extensions/filters/http/ai_protocol_manager/token_usage.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
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
    // Wraps a scalar at `target_path` in a single-element array. A no-op when the field is
    // absent, null, or already an array.
    EnsureArray,
    // Wraps a scalar at `target_path` in an object keyed by `element_key`. A no-op when the
    // field is absent, null, or already an object.
    EnsureObject,
    // Replaces the object at `target_path` with the value it holds under `element_key`, but
    // only when that is its sole key. A no-op otherwise, which is what lets a rule set collapse
    // a degenerate wrapper without disturbing a genuinely structured value.
    UnwrapSingleKeyObject,
    // Parses a numeric string at `target_path` into a real JSON number, as an integer when
    // `integral` is set. A no-op when the field is absent, already numeric, or does not parse.
    CoerceNumeric,
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

  // Normalizes `path` to array shape by wrapping a scalar in a single-element array. Fields that
  // are absent, null, or already arrays are left untouched. Use this ahead of a mapping into a
  // destination that only accepts an array, where the source dialect also permits a bare scalar
  // (e.g. OpenAI `stop` -> Anthropic `stop_sequences`).
  static TranscodeRule ensureArray(std::string path);

  // Normalizes `path` to object shape by wrapping a scalar as `{key: <scalar>}`. Fields that are
  // absent, null, or already objects are left untouched. Pairs with `unwrapSingleKeyObject` to
  // move between a dialect that spells a choice as a bare string and one that spells it as a
  // tagged object (e.g. OpenAI `tool_choice: "auto"` -> Anthropic `{"type": "auto"}`).
  static TranscodeRule ensureObject(std::string path, std::string key);

  // Replaces the object at `path` with the value under `key`, but only when `key` is its only
  // member. An object carrying anything else is left alone, so a rule set can collapse the
  // degenerate `{"type": "auto"}` form while leaving `{"type": "function", "function": {...}}`
  // intact without needing conditional rules.
  static TranscodeRule unwrapSingleKeyObject(std::string path, std::string key);

  // Converts a numeric string at `path` into a real JSON number. Values that are absent, already
  // numeric, or not parseable are left untouched; an unparseable string is the source schema's
  // problem to reject, not this rule's. Use where a lenient source dialect permits a quoted
  // number (Gemini renders proto numbers as strings) and the destination requires a real one.
  static TranscodeRule toNumber(std::string path);

  // As `toNumber`, but yields an integer, for destinations that declare the field as such.
  static TranscodeRule toInteger(std::string path);

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
  std::vector<std::string> source_segments_;
  std::vector<std::string> source_paths_;
  std::vector<std::vector<std::string>> source_paths_segments_;
  std::string target_path_;
  std::vector<std::string> target_segments_;
  std::string predicate_field_;
  std::string extract_subpath_;
  std::vector<std::string> extract_subpath_segments_;
  std::vector<std::string> match_values_;
  nlohmann::json default_value_;
  absl::flat_hash_map<std::string, std::string> value_mappings_;
  UnknownValuePolicy unknown_policy_{UnknownValuePolicy::Passthrough};
  bool integral_{false};
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

// Declarative dialect pack pairing a protocol's `to_ir` (Protocol -> OpenAiChatCompletions)
// and `from_ir` (OpenAiChatCompletions -> Protocol) rule sets.
//
// `dialect_schema` is the protocol's own `PayloadSchema`; it is what a payload converted out of
// the IR is validated against before it is handed to the upstream. `ir_schema` is only used for
// static rule verification at registration time -- see `TranscodingEngine::transcodeToIr()` for
// why the IR document itself is not validated at runtime.
struct DialectTranscodePack {
  ApiProtocol protocol{ApiProtocol::Unspecified};
  TranscodeRuleSet to_ir;
  TranscodeRuleSet from_ir;
  const PayloadSchema* dialect_schema{nullptr};
  const PayloadSchema* ir_schema{nullptr};
};

// The Transcoding Engine: manages registered `DialectTranscodePack`s, verifies them against
// `PayloadSchema` definitions at startup, and converts payloads between any registered dialect
// schema and the intermediate representation (`transcodeToIr` / `transcodeFromIr`).
//
// The engine is a single-hop tool (`Dialect -> IR` or `IR -> Dialect`) and does not chain
// `Dialect A -> IR -> Dialect B` or infer target wire protocols from model names. Orchestrating
// transcoding legs, selecting target wire protocols from route/endpoint configuration, and
// bypassing conversion when source and target protocols match are the responsibility of the
// calling transcoding filter.
//
// TODO(ginama): Address the IR data-loss problem where dialect-specific fields not modeled by
// `OpenAiChatCompletions` (e.g. unmapped `generationConfig` fields) are dropped when converting
// to the IR.
class TranscodingEngine {
public:
  static constexpr ApiProtocol kIrProtocol = ApiProtocol::OpenAiChatCompletions;

  TranscodingEngine() = default;

  // Builds the default engine pre-loaded with OpenAI Chat Completions, Anthropic Messages,
  // and Gemini GenerateContent declarative transcoding packs.
  static absl::StatusOr<TranscodingEngine> createDefault();

  // Statically verifies a rule set against `source_schema` at config load time.
  // Rejects any rule set where a value-reading rule (`ValueMap`) targets a field declared
  // `.offloadable()` in `source_schema`, since such a field may arrive as an `ExternalRef`
  // binary node rather than an inline string. Provenance is tracked across structural rules
  // (including content-block array reshaping into `<field>[].text`) in execution order.
  static absl::Status validateRulesAgainstSchema(const TranscodeRuleSet& rules,
                                                 const PayloadSchema* source_schema = nullptr);

  // Registers a `DialectTranscodePack` after statically verifying its rule sets.
  //
  // `dialect_schema` and `ir_schema` override the corresponding fields on `pack` when
  // non-null; when null, whatever `pack` already carries is kept. This lets a caller either
  // pass the schemas here or set them directly on the struct, without one silently winning.
  absl::Status registerPack(DialectTranscodePack pack,
                            const PayloadSchema* dialect_schema = nullptr,
                            const PayloadSchema* ir_schema = nullptr);

  // Converts `payload` from `source_protocol` into the intermediate representation
  // (`OpenAiChatCompletions`). A no-op when `source_protocol` is already the IR protocol.
  //
  // The result is deliberately NOT validated against the IR schema. Two reasons: the source
  // payload was already validated against its own schema by the AI Protocol Manager before the
  // filter chain ran, so re-validating is duplicated work on the hot path; and the IR schema
  // requires `model`, which a Gemini request legitimately does not carry in its body (it lives
  // in the request path), so validating here would reject valid Gemini traffic.
  absl::Status transcodeToIr(ApiProtocol source_protocol, JsonWithExtBuf& payload) const {
    return transcodeToIr(source_protocol, payload.json());
  }
  absl::Status transcodeToIr(ApiProtocol source_protocol, nlohmann::json& json) const;

  // Converts `payload` out of the intermediate representation into `target_protocol`,
  // then validates it against that protocol's schema so a payload the upstream would reject is
  // caught here instead of over the network. Rule execution is skipped when `target_protocol`
  // is the IR protocol, but validation still runs.
  absl::Status transcodeFromIr(ApiProtocol target_protocol, JsonWithExtBuf& payload) const {
    return transcodeFromIr(target_protocol, payload.json());
  }
  absl::Status transcodeFromIr(ApiProtocol target_protocol, nlohmann::json& json) const;

private:
  absl::flat_hash_map<ApiProtocol, DialectTranscodePack> packs_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
