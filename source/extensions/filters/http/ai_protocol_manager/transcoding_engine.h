#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"
#include "source/extensions/filters/http/ai_protocol_manager/schema.h"
#include "source/extensions/filters/http/ai_protocol_manager/sse/sse_event.h"
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

struct TranscodeContext;

// The shape a `TranscodePredicate::fieldIs()` test or a `TranscodeRule::require()` rule expects of
// a node.
enum class JsonShape {
  Object,
  Array,
  // An array with at least one element.
  NonEmptyArray,
  // An inline string. An offloaded `ExternalRef` is not one; see `Text`.
  String,
  // Text of any size: an inline string, or an `ExternalRef` to a large string held outside the
  // DOM. Use this, not `String`, for any field a schema declares `.offloadable()`.
  Text,
};

// A read-only test on a JSON object, which makes a rule conditional (`when`, `collectText`,
// `require`). Predicates never move data, so unlike rule paths, their dot-separated paths may index
// arrays with numeric segments (e.g. `choices.0.delta.content`). A default-constructed predicate
// matches everything.
class TranscodePredicate {
public:
  enum class Kind {
    Always,
    FieldEquals,
    FieldIs,
    Not,
  };

  TranscodePredicate() = default;

  // Matches everything.
  static TranscodePredicate always();

  // Matches when `path` holds a value equal to `value`. An offloaded `ExternalRef` equals nothing,
  // which is why the startup verifier rejects this on offloadable fields.
  static TranscodePredicate fieldEquals(std::string path, nlohmann::json value);

  // Matches when `path` holds a value of `shape`.
  static TranscodePredicate fieldIs(std::string path, JsonShape shape);

  // Matches when `predicate` does not.
  static TranscodePredicate negate(TranscodePredicate predicate);

  bool matches(const nlohmann::json& json) const;

  // Introspection accessors (used by the startup verifier):
  Kind kind() const { return kind_; }
  const std::string& path() const { return path_; }
  const nlohmann::json& value() const { return value_; }
  JsonShape shape() const { return shape_; }
  const std::vector<TranscodePredicate>& operands() const { return operands_; }

private:
  explicit TranscodePredicate(Kind kind) : kind_(kind) {}

  Kind kind_{Kind::Always};
  std::string path_;
  std::vector<std::string> segments_;
  nlohmann::json value_;
  JsonShape shape_{JsonShape::Object};
  // The negated predicate, for `Not`.
  std::vector<TranscodePredicate> operands_;
};

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
    // Writes `default_value` to `target_path`, replacing whatever is there.
    SetConst,
    // Writes the caller-supplied `context_field` to `target_path` if `target_path` is absent or
    // null and the caller supplied a value.
    SetFromContext,
    // Sets `element_key` on each object element of array `target_path` to the element's position,
    // unless the element already carries it.
    Enumerate,
    // Removes every member of the object at `target_path` whose key is not in `match_values`.
    RetainOnly,
    // Replaces array `source_path` with its first element, moved to `target_path`.
    TakeFirst,
    // Gathers the text at `extract_subpath` of every element of array `source_path` that matches
    // `predicate` into `target_path`, then removes `source_path`.
    CollectText,
    // Applies `sub_rules` to the current object if it matches `predicate`.
    When,
    // Fails unless the current object matches `predicate` (a `fieldIs` test on `target_path`).
    Require,
    // Copies the value at `source_path` into the stream state slot named `slot`.
    CaptureToState,
    // Writes stream state slot `slot` to `target_path` if `target_path` is absent or null.
    SetFromState,
    // Re-renders a payload's token usage from `usage_from`'s shape into `usage_to`'s.
    ConvertUsage,
    // Merges a stream event's token usage into the stream's running total, and optionally renders
    // that total in `usage_to`'s shape.
    AccumulateUsage,
  };

  enum class UnknownValuePolicy {
    Passthrough,
    Drop,
    Reject,
    // Replaces the unmapped value with a fixed fallback (see `ValueFallback`).
    Fallback,
  };

  struct ValueMapping {
    std::string from;
    std::string to;
  };

  // The value a `valueMap` writes for a string none of its mappings cover.
  struct ValueFallback {
    std::string value;
  };

  // A value the caller supplies through `TranscodeContext` because the payload does not carry it.
  enum class ContextField {
    // The model the request named (`TranscodeContext::request_model`).
    RequestModel,
    // The current time in seconds since the Unix epoch (`TranscodeContext::now_unix_seconds`).
    NowUnixSeconds,
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

  // As above, but a string none of `mappings` covers becomes `fallback.value`. Non-string values
  // are left alone.
  static TranscodeRule valueMap(std::string path, std::vector<ValueMapping> mappings,
                                ValueFallback fallback);

  // Runs `rules` on each element of the array at `array_path`.
  static TranscodeRule forEach(std::string array_path, std::initializer_list<TranscodeRule> rules);
  static TranscodeRule forEach(std::string array_path, std::vector<TranscodeRule> rules);

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

  // Sets `path` to `value`, replacing any existing value. For members whose value is fixed by the
  // destination dialect (e.g. OpenAI's `object: "chat.completion"`).
  static TranscodeRule setConst(std::string path, nlohmann::json value);

  // Sets `path` from the caller-supplied `field` if `path` is missing or null. A field the caller
  // left empty (or zero) writes nothing, so a later `setDefault` can still apply.
  static TranscodeRule setFromContext(std::string path, ContextField field);

  // Sets `key` on each object element of the array at `array_path` to the element's position,
  // unless the element already carries a non-null `key`.
  static TranscodeRule enumerate(std::string array_path, std::string key);

  // Removes every member of the object at `path` (empty for the current object) whose key is not
  // in `keys`. Ends a mapping into a dialect that rejects, or must not leak, unknown members.
  static TranscodeRule retainOnly(std::string path, std::initializer_list<std::string> keys);

  // Replaces the array at `array_path` with its first element, moved to `to_path`. An empty array
  // is removed and writes nothing. For destinations that carry one of what the IR has a list of
  // (e.g. IR `choices[0]` -> the root of an Anthropic message).
  static TranscodeRule takeFirst(std::string array_path, std::string to_path);

  // Gathers `element[key]` from every object element of the array at `array_path` that matches
  // `where` and holds text, writes it to `to_path`, and removes the array. A single match is moved,
  // so an offloaded `ExternalRef` survives without being materialized; several inline strings are
  // concatenated; no match yields "". Several matches that include an `ExternalRef` cannot be
  // joined without materializing it, so that is an `Unimplemented` error, raised before anything
  // is modified. An absent or non-array `array_path` is left alone.
  static TranscodeRule collectText(std::string array_path, std::string key, std::string to_path,
                                   TranscodePredicate where = TranscodePredicate());

  // Runs `rules` on the current object if it matches `predicate`.
  static TranscodeRule when(TranscodePredicate predicate,
                            std::initializer_list<TranscodeRule> rules);
  static TranscodeRule when(TranscodePredicate predicate, std::vector<TranscodeRule> rules);

  // Fails with `InvalidArgument` unless `path` holds a value of `shape`. Guards the rules that
  // follow, which would otherwise quietly skip a payload that is not what the dialect promises.
  static TranscodeRule require(std::string path, JsonShape shape);

  // Copies the value at `path` into the stream state slot `slot`, for a later event's
  // `setFromState`. Only valid on stream legs; fails without `TranscodeContext::stream_state`. A
  // value that is or holds an offloaded `ExternalRef` cannot be captured, since the buffer it
  // points into does not outlive the event.
  static TranscodeRule captureToState(std::string path, std::string slot);

  // Sets `path` from the stream state slot `slot` if `path` is missing or null and the slot was
  // captured. Only valid on stream legs; fails without `TranscodeContext::stream_state`.
  static TranscodeRule setFromState(std::string path, std::string slot);

  // Re-renders the payload's token usage from dialect `from` into dialect `to`, through the
  // canonical `TokenUsage` both adapters agree on (`LLMProtocolAdapter::extractUsage()`, then
  // `renderUsage()`). `from`'s root usage member (`usagePath()`) is removed; `to`'s is written only
  // if there was usage.
  static TranscodeRule convertUsage(LLMProtocol from, LLMProtocol to);

  // Merges the stream event's token usage, read as dialect `from`, into the stream's running total
  // (`TranscodeStreamState::usage`) and removes `from`'s root usage member. Usage the adapter reads
  // from elsewhere (Anthropic `message_start` nests it under `message`) is left for the event's
  // other rules. When `render_to` is set, the running total so far is written in that dialect's
  // shape. For dialects that report usage in pieces across a stream (Anthropic: input in
  // `message_start`, output in `message_delta`). Only valid on stream legs; fails without
  // `TranscodeContext::stream_state`.
  static TranscodeRule accumulateUsage(LLMProtocol from,
                                       LLMProtocol render_to = LLMProtocol::Unspecified);

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
  const TranscodePredicate& predicate() const { return predicate_; }
  ContextField contextField() const { return context_field_; }
  const std::string& slot() const { return slot_; }
  LLMProtocol usageFrom() const { return usage_from_; }
  LLMProtocol usageTo() const { return usage_to_; }

  // Executes this rule in-place on `json`. `ctx` supplies what `setFromContext` and the stream
  // state rules read and write; rules that need none of it run without one.
  absl::Status apply(nlohmann::json& json, TranscodeContext* ctx = nullptr) const;

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
  // The value `SetDefault` / `SetConst` write, or a `ValueMap`'s fallback.
  nlohmann::json default_value_;
  absl::flat_hash_map<std::string, std::string> value_mappings_;
  UnknownValuePolicy unknown_policy_{UnknownValuePolicy::Passthrough};
  bool integral_{false};
  std::vector<TranscodeRule> sub_rules_;
  TranscodePredicate predicate_;
  ContextField context_field_{ContextField::RequestModel};
  std::string slot_;
  LLMProtocol usage_from_{LLMProtocol::Unspecified};
  LLMProtocol usage_to_{LLMProtocol::Unspecified};
};

// A compiled, immutable sequence of declarative `TranscodeRule`s that transforms a payload
// from `source_protocol` to `target_protocol`.
class TranscodeRuleSet {
public:
  TranscodeRuleSet() = default;
  TranscodeRuleSet(LLMProtocol source_protocol, LLMProtocol target_protocol,
                   std::initializer_list<TranscodeRule> rules)
      : source_protocol_(source_protocol), target_protocol_(target_protocol), rules_(rules) {}
  TranscodeRuleSet(LLMProtocol source_protocol, LLMProtocol target_protocol,
                   std::vector<TranscodeRule> rules)
      : source_protocol_(source_protocol), target_protocol_(target_protocol),
        rules_(std::move(rules)) {}

  LLMProtocol sourceProtocol() const { return source_protocol_; }
  LLMProtocol targetProtocol() const { return target_protocol_; }
  const std::vector<TranscodeRule>& rules() const { return rules_; }

  // Executes all rules in order on `payload`; see `TranscodeRule::apply()` for `ctx`.
  absl::Status execute(JsonWithExtBuf& payload, TranscodeContext* ctx = nullptr) const {
    return execute(payload.json(), ctx);
  }
  absl::Status execute(nlohmann::json& json, TranscodeContext* ctx = nullptr) const;

private:
  LLMProtocol source_protocol_{LLMProtocol::Unspecified};
  LLMProtocol target_protocol_{LLMProtocol::Unspecified};
  std::vector<TranscodeRule> rules_;
};

// A payload's place in the exchange.
enum class PayloadKind {
  // A request body.
  Request,
  // A unary (non-streamed) response body.
  Response,
  // One SSE event of a streamed response.
  StreamEvent,
};

// Which way one hop converts. The engine is single-hop: `ToIr` converts a dialect payload into the
// IR, and `FromIr` converts an IR payload into the dialect.
enum class TranscodeDirection {
  ToIr,
  FromIr,
};

// One hop the engine can run: which payload, which way, and which dialect sits on the non-IR side
// of the hop (the source for `ToIr`, the destination for `FromIr`).
struct TranscodeLeg {
  PayloadKind kind{PayloadKind::Request};
  TranscodeDirection direction{TranscodeDirection::ToIr};
  LLMProtocol dialect{LLMProtocol::Unspecified};
};

// Per-stream memory for one streamed response, owned by the caller (one per stream, never shared)
// and handed to each of the stream's events through `TranscodeContext::stream_state`.
struct TranscodeStreamState {
  // Values `captureToState` saved for a later event's `setFromState`.
  absl::flat_hash_map<std::string, nlohmann::json> slots{};
  // The native usage `accumulateUsage` has merged so far. Never finalized itself: renders
  // finalize a copy.
  TokenUsage usage{};
  // Set once the stream has ended, by a `Terminate` case or by `finishStream()`, after which
  // `finishStream()` appends nothing.
  bool terminated{false};
};

// What a leg needs beyond the payload, and what it produces outside the payload. The engine reads
// no headers and no clocks itself; the caller passes in what it has and applies what comes back.
struct TranscodeContext {
  // Inputs.
  // The request's `:path`, from which a request `ToIr` leg reads what its dialect names in the path
  // rather than the body (see `DialectTranscodePack::envelope`).
  absl::string_view request_path{};
  // The model the request named, for a response that does not name its own (see
  // `TranscodeRule::ContextField::RequestModel`).
  absl::string_view request_model{};
  // The current time, for a response the IR requires a `created` timestamp on but the dialect
  // does not carry one (see `TranscodeRule::ContextField::NowUnixSeconds`).
  int64_t now_unix_seconds{0};
  // The stream's memory: required on `StreamEvent` legs, whose stream-state and
  // usage-accumulation rules read and write it.
  TranscodeStreamState* stream_state{nullptr};

  // Outputs.
  // Set by request legs to the request's IR `model` (read after a `ToIr` leg and before a
  // `FromIr` one), so the caller can hand it back as the fallback model on the response legs.
  std::string ir_model{};
  // Set by a successful request `FromIr` leg whose dialect names request members in the path (see
  // `DialectTranscodePack::envelope`): the `:path` the request must be sent to, which the caller
  // applies along with the body. Every other request leg clears it.
  std::optional<std::string> rewritten_path{};
};

// The rule sets for one payload kind of a dialect: `to_ir` converts the dialect into the IR
// (`OpenAiChatCompletions`) and `from_ir` converts the IR into the dialect. A leg left empty is
// the identity.
struct LegRules {
  TranscodeRuleSet to_ir{};
  TranscodeRuleSet from_ir{};
};

// Which SSE events a `StreamEventCase` applies to. A default-constructed match accepts every event
// whose payload is JSON.
class StreamEventMatch {
public:
  enum class Kind {
    // A JSON payload that matches `predicate()`.
    Json,
    // A JSON payload whose `type` member is `type()`, or which has no string `type` member and is
    // named `type()` by its SSE `event:` field. Anthropic names each event both ways.
    EventType,
    // OpenAI's stream terminator: the raw, non-JSON payload `[DONE]`.
    IsDone,
    // Any payload that is not JSON: raw data, raw data held by reference, or no data at all.
    NotJson,
  };

  StreamEventMatch() = default;

  static StreamEventMatch json(TranscodePredicate predicate = TranscodePredicate());
  static StreamEventMatch eventType(std::string type);
  static StreamEventMatch isDone();
  static StreamEventMatch notJson();

  // Takes a mutable `event` because reading a raw payload linearizes its buffer.
  bool matches(SseEvent& event) const;

  // Introspection accessors (used by the startup verifier):
  Kind kind() const { return kind_; }
  const TranscodePredicate& predicate() const { return predicate_; }
  const std::string& type() const { return type_; }

private:
  explicit StreamEventMatch(Kind kind) : kind_(kind) {}

  Kind kind_{Kind::Json};
  TranscodePredicate predicate_;
  std::string type_;
};

// What a stream grammar does with an event that one of its cases matched.
enum class StreamDisposition {
  // Runs the case's rules on the event's JSON payload and forwards the result.
  Transcode,
  // Forwards the event as it is.
  Passthrough,
  // Forwards nothing.
  Drop,
  // Ends the stream: forwards the grammar's `on_terminate` events in the event's place.
  Terminate,
};

// An event a stream grammar writes itself, rather than transcodes from one the source sent.
struct StreamEmit {
  // The SSE `event:` name; empty for none.
  std::string event{};
  // The JSON payload, or null for the raw payload `raw_data`.
  nlohmann::json json{};
  // The payload of an event whose payload is not JSON (e.g. OpenAI's `[DONE]`).
  std::string raw_data{};
};

// One row of a stream grammar: the events it matches, and what becomes of them. Only a JSON
// payload can be transcoded, so a `Transcode` case must match with `json()` or `eventType()`.
struct StreamEventCase {
  StreamEventMatch match{};
  StreamDisposition disposition{StreamDisposition::Transcode};
  // For `Transcode`: the rules that convert the event's payload.
  TranscodeRuleSet rules{};
  // For `Transcode`: the SSE `event:` name of the converted event; empty for none.
  std::string output_event{};
};

// How one dialect's streamed response events convert into another's. The first case that matches
// an event decides what becomes of it, and an event no case matches is an error. A grammar without
// cases is the identity.
struct StreamGrammar {
  std::vector<StreamEventCase> cases{};
  // What a `Terminate` case forwards in place of the source's terminator.
  std::vector<StreamEmit> on_terminate{};
  // What to append when the source stream ends without having terminated: the terminator the
  // destination dialect needs but the source dialect never sends.
  std::vector<StreamEmit> on_source_end{};
};

// The stream grammars of one dialect: `to_ir` converts the dialect's events into IR chunks and
// `from_ir` converts IR chunks into the dialect's events.
struct StreamGrammars {
  StreamGrammar to_ir{};
  StreamGrammar from_ir{};
};

// Where a dialect's API names request members in the request path rather than the body. Gemini
// names the model and the streaming mode there, calling a custom method on the model:
// `/v1beta/models/{model}:generateContent`, or `:streamGenerateContent` to stream the response.
//
// A request `ToIr` leg lifts what `TranscodeContext::request_path` names into the IR body, unless
// the body names it itself. A request `FromIr` leg moves it out of the body into
// `TranscodeContext::rewritten_path`: `{prefix}{model}{unary_method}` or
// `{prefix}{model}{stream_method}`.
struct PathTemplate {
  // What a rendered path puts before the model. A parsed path need only end its own prefix with
  // this one's last segment (`/models/`), since Vertex AI nests Gemini's models under a project and
  // a location.
  std::string prefix{};
  // The custom method (`:verb`) that follows the model, for a unary and for a streamed response.
  // A rendered path keeps any query a method ends with; a parsed path's query is ignored.
  std::string unary_method{};
  std::string stream_method{};
  // The IR members the path carries: the model, and whether the response is streamed.
  std::string model_field{"model"};
  std::string stream_field{"stream"};
};

// Declarative dialect pack: every rule needed to move one dialect's payloads to and from the IR.
//
// `dialect_schema` is the protocol's own `PayloadSchema`; it is what a request converted out of
// the IR is validated against before it is handed to the upstream. `ir_schema` is only used for
// static rule verification at registration time -- see `TranscodingEngine::transcodeToIr()` for
// why the IR document itself is not validated at runtime.
struct DialectTranscodePack {
  LLMProtocol protocol{LLMProtocol::Unspecified};
  // Request bodies.
  LegRules request{};
  // Unary response bodies.
  LegRules response{};
  // Streamed response events.
  StreamGrammars stream{};
  // Request members the dialect's API names in the request path, if any.
  std::optional<PathTemplate> envelope{};
  const PayloadSchema* dialect_schema{nullptr};
  const PayloadSchema* ir_schema{nullptr};
};

// The Transcoding Engine: manages registered `DialectTranscodePack`s, verifies them against
// `PayloadSchema` definitions at startup, and converts payloads between any registered dialect
// and the intermediate representation, one `TranscodeLeg` at a time.
//
// The engine is a single-hop tool (`Dialect -> IR` or `IR -> Dialect`) and does not chain
// `Dialect A -> IR -> Dialect B` or infer target wire protocols from model names. Orchestrating
// transcoding legs, selecting target wire protocols from route/endpoint configuration, and
// bypassing conversion when source and target protocols match are the responsibility of the
// calling transcoding filter. Every rule about what a dialect's payloads look like lives here.
//
// TODO(ginama): Address the IR data-loss problem where dialect-specific fields not modeled by
// `OpenAiChatCompletions` (e.g. unmapped `generationConfig` fields) are dropped when converting
// to the IR.
class TranscodingEngine {
public:
  static constexpr LLMProtocol kIrProtocol = LLMProtocol::OpenAiChatCompletions;

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

  // Registers a `DialectTranscodePack` after statically verifying it. The request rule sets are
  // checked against the schemas (see `validateRulesAgainstSchema()`), and every leg is checked for
  // what would otherwise fail on each payload it applies to: a request or response rule that needs
  // per-stream state, a stream case that transcodes events without a JSON payload, a
  // `setFromState` whose slot no `captureToState` in its grammar writes, and an SSE event name or
  // raw data a grammar writes that would break the event's framing.
  //
  // `dialect_schema` and `ir_schema` override the corresponding fields on `pack` when
  // non-null; when null, whatever `pack` already carries is kept. This lets a caller either
  // pass the schemas here or set them directly on the struct, without one silently winning.
  absl::Status registerPack(DialectTranscodePack pack,
                            const PayloadSchema* dialect_schema = nullptr,
                            const PayloadSchema* ir_schema = nullptr);

  // Runs one leg on `json`.
  //
  // A `Request` leg rewrites `json` in place, so a failure part way through leaves it neither in
  // the source shape nor in the target one; the caller must not forward it. A `Response` leg is
  // all-or-nothing: it runs on a copy and replaces `json` only on success, so after a failure the
  // caller still holds the original and can forward it untranslated. `StreamEvent` legs go
  // through `transcodeStreamEvent()` instead.
  //
  // A `Request` leg also moves what its dialect names in the request path rather than the body
  // (`DialectTranscodePack::envelope`): `ToIr` lifts it from `ctx.request_path` into the IR body,
  // and `FromIr` moves it out of the body into `ctx.rewritten_path`, which the caller must send the
  // request to.
  //
  // A leg whose dialect is the IR is the identity, except that a request `FromIr` leg still
  // validates the payload against the IR's schema.
  absl::Status transcode(const TranscodeLeg& leg, TranscodeContext& ctx,
                         nlohmann::json& json) const;

  // Runs one `StreamEvent` leg on `event`, one event of a streamed response, and returns the
  // events to forward in its place: none when the grammar drops it, several when it writes more.
  // On success `event` is consumed. On failure `event` and `ctx.stream_state` are left exactly as
  // they were, so the caller can forward the event untranslated and carry on with the stream.
  //
  // `ctx.stream_state` is required, and must be the same object for every event of one stream.
  // A leg whose dialect is the IR is the identity.
  absl::StatusOr<std::vector<SseEventPtr>>
  transcodeStreamEvent(const TranscodeLeg& leg, TranscodeContext& ctx, SseEventPtr& event) const;

  // Ends a `StreamEvent` leg once its source stream has ended, and returns the events to append:
  // the grammar's `on_source_end`, unless the stream has already terminated.
  absl::StatusOr<std::vector<SseEventPtr>> finishStream(const TranscodeLeg& leg,
                                                        TranscodeContext& ctx) const;

  // The model that `path`, a request path of `dialect`'s API, names: empty unless the dialect
  // names its model in the path (`DialectTranscodePack::envelope`) and `path` is one of its model
  // methods. For a caller that needs the model without running a request leg.
  std::string modelFromRequestPath(LLMProtocol dialect, absl::string_view path) const;

  // Converts request `payload` from `source_protocol` into the intermediate representation
  // (`OpenAiChatCompletions`). A no-op when `source_protocol` is already the IR protocol or is
  // `Unspecified`. Only the body is converted: unlike `transcode()`, this has no request path to
  // lift what the dialect names there from.
  //
  // The result is deliberately NOT validated against the IR schema. Two reasons: the source
  // payload was already validated against its own schema by the AI Protocol Manager before the
  // filter chain ran, so re-validating is duplicated work on the hot path; and the IR schema
  // requires `model`, which a Gemini request legitimately does not carry in its body (it lives
  // in the request path), so validating here would reject valid Gemini traffic.
  absl::Status transcodeToIr(LLMProtocol source_protocol, JsonWithExtBuf& payload) const {
    return transcodeToIr(source_protocol, payload.json());
  }
  absl::Status transcodeToIr(LLMProtocol source_protocol, nlohmann::json& json) const;

  // Converts request `payload` out of the intermediate representation into `target_protocol`,
  // then validates it against that protocol's schema so a payload the upstream would reject is
  // caught here instead of over the network. Rule execution is skipped when `target_protocol`
  // is the IR protocol, but validation still runs. A no-op when `target_protocol` is
  // `Unspecified`.
  //
  // Only the body is converted. What the dialect names in the request path (Gemini's `model` and
  // `stream`) stays in the body, since there is no context to hand a path back through; a request
  // bound for the upstream goes through `transcode()`, which moves it into the path.
  absl::Status transcodeFromIr(LLMProtocol target_protocol, JsonWithExtBuf& payload) const {
    return transcodeFromIr(target_protocol, payload.json());
  }
  absl::Status transcodeFromIr(LLMProtocol target_protocol, nlohmann::json& json) const;

private:
  absl::StatusOr<const DialectTranscodePack*> findPack(LLMProtocol dialect) const;
  // The grammar a `StreamEvent` leg runs, or null when the leg is the identity.
  absl::StatusOr<const StreamGrammar*> findStreamGrammar(const TranscodeLeg& leg,
                                                         const TranscodeContext& ctx) const;

  absl::flat_hash_map<LLMProtocol, DialectTranscodePack> packs_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
