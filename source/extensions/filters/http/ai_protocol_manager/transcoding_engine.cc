#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_adapter.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/strip.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// Splits a dot-delimited JSON path (e.g. "generationConfig.maxOutputTokens") into owned segments
// at rule construction time so execution never re-tokenizes paths per message.
std::vector<std::string> splitPath(absl::string_view path) {
  if (path.empty()) {
    return {};
  }
  return absl::StrSplit(path, '.');
}

// Extracts and removes the node at `parts` from `root`, returning `std::nullopt` if any
// segment is absent or not an object. Cleans up empty parent objects created along `parts`.
std::optional<nlohmann::json> extractNodeByPath(nlohmann::json& root,
                                                absl::Span<const std::string> parts) {
  if (parts.empty() || !root.is_object()) {
    return std::nullopt;
  }

  std::vector<nlohmann::json*> parents;
  nlohmann::json* curr = &root;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    auto it = curr->find(parts[i]);
    if (it == curr->end() || !it->is_object()) {
      return std::nullopt;
    }
    parents.push_back(curr);
    curr = &(*it);
  }

  auto last_it = curr->find(parts.back());
  if (last_it == curr->end()) {
    return std::nullopt;
  }

  nlohmann::json extracted = std::move(*last_it);
  curr->erase(last_it);

  // Prune any intermediate objects that became empty after removing the leaf.
  for (size_t i = parents.size(); i > 0; --i) {
    nlohmann::json* parent = parents[i - 1];
    const std::string& child_key = parts[i - 1];
    auto child_it = parent->find(child_key);
    if (child_it != parent->end() && child_it->is_object() && child_it->empty()) {
      parent->erase(child_it);
    } else {
      break;
    }
  }

  return extracted;
}

// Navigates to `parts` inside `root` (read/write), returning `nullptr` if absent.
nlohmann::json* findNodeByPath(nlohmann::json& root, absl::Span<const std::string> parts) {
  if (parts.empty()) {
    return &root;
  }
  nlohmann::json* curr = &root;
  for (const std::string& part : parts) {
    if (!curr->is_object()) {
      return nullptr;
    }
    auto it = curr->find(part);
    if (it == curr->end()) {
      return nullptr;
    }
    curr = &(*it);
  }
  return curr;
}

// Writes `value` into `root` at `parts`, creating intermediate objects as needed.
void setNodeByPath(nlohmann::json& root, absl::Span<const std::string> parts,
                   nlohmann::json&& value) {
  if (parts.empty()) {
    root = std::move(value);
    return;
  }
  if (!root.is_object()) {
    root = nlohmann::json::object();
  }
  nlohmann::json* curr = &root;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    const std::string& key = parts[i];
    auto it = curr->find(key);
    if (it == curr->end() || !it->is_object()) {
      (*curr)[key] = nlohmann::json::object();
    }
    curr = &((*curr)[key]);
  }
  (*curr)[parts.back()] = std::move(value);
}

// Converts any message content node (string, ExternalRef, or array of blocks) into a normalized
// array of content block objects (`{"type": "text", "text": <moved_node>}`) so consecutive
// messages can be combined without stringifying `ExternalRef` nodes.
nlohmann::json toContentBlockArray(nlohmann::json&& content) {
  if (content.is_array()) {
    return std::move(content);
  }
  nlohmann::json block = nlohmann::json::object();
  block["type"] = "text";
  block["text"] = std::move(content);
  nlohmann::json arr = nlohmann::json::array();
  arr.push_back(std::move(block));
  return arr;
}

// Read-only lookup for predicates. Unlike `findNodeByPath`, a numeric segment indexes into an
// array, so a predicate can look at e.g. `choices.0.finish_reason`.
const nlohmann::json* peekNodeByPath(const nlohmann::json& root,
                                     absl::Span<const std::string> parts) {
  const nlohmann::json* curr = &root;
  for (const std::string& part : parts) {
    if (curr->is_object()) {
      auto it = curr->find(part);
      if (it == curr->end()) {
        return nullptr;
      }
      curr = &(*it);
    } else if (curr->is_array()) {
      size_t index = 0;
      if (!absl::SimpleAtoi(part, &index) || index >= curr->size()) {
        return nullptr;
      }
      curr = &(*curr)[index];
    } else {
      return nullptr;
    }
  }
  return curr;
}

bool hasShape(const nlohmann::json& node, JsonShape shape) {
  switch (shape) {
  case JsonShape::Object:
    return node.is_object();
  case JsonShape::Array:
    return node.is_array();
  case JsonShape::NonEmptyArray:
    return node.is_array() && !node.empty();
  case JsonShape::String:
    return node.is_string();
  case JsonShape::Text:
    return node.is_string() || JsonWithExtBuf::isExternalRef(node);
  }
  return false;
}

// True if `node` is, or holds anywhere inside it, an `ExternalRef`.
bool containsExternalRef(const nlohmann::json& node) {
  if (JsonWithExtBuf::isExternalRef(node)) {
    return true;
  }
  if (node.is_structured()) {
    for (const nlohmann::json& child : node) {
      if (containsExternalRef(child)) {
        return true;
      }
    }
  }
  return false;
}

absl::string_view shapeDescription(JsonShape shape) {
  switch (shape) {
  case JsonShape::Object:
    return "an object";
  case JsonShape::Array:
    return "an array";
  case JsonShape::NonEmptyArray:
    return "a non-empty array";
  case JsonShape::String:
    return "a string";
  case JsonShape::Text:
    break;
  }
  return "text";
}

// Writes finalized canonical `usage` onto `json` in `dialect`'s native shape, at the dialect's
// usage member. Writes nothing for a dialect without one, or when there is nothing to render.
void writeUsage(nlohmann::json& json, const TokenUsage& usage, LLMProtocol dialect) {
  const LLMProtocolAdapter& adapter = AdapterRegistry::get(dialect);
  const absl::string_view usage_path = adapter.usagePath();
  if (usage_path.empty()) {
    return;
  }
  nlohmann::json rendered = adapter.renderUsage(usage);
  if (rendered.empty()) {
    return;
  }
  json[std::string(usage_path)] = std::move(rendered);
}

// Reads `json`'s usage as `dialect` and removes the dialect's usage member.
TokenUsage takeUsage(nlohmann::json& json, LLMProtocol dialect) {
  const LLMProtocolAdapter& adapter = AdapterRegistry::get(dialect);
  TokenUsage usage = adapter.extractUsage(json).usage;
  if (const absl::string_view usage_path = adapter.usagePath(); !usage_path.empty()) {
    json.erase(std::string(usage_path));
  }
  return usage;
}

absl::Status missingStreamState(absl::string_view rule_name) {
  return absl::FailedPreconditionError(
      absl::StrCat(rule_name, " rule needs per-stream state and can only run on a stream leg"));
}

// The raw payload that ends an OpenAI (and so an IR) event stream.
constexpr absl::string_view kSseDone = "[DONE]";

std::string joinRulePath(absl::string_view prefix, absl::string_view relative_path) {
  if (prefix.empty()) {
    return std::string(relative_path);
  }
  return absl::StrCat(prefix, ".", relative_path);
}

// When a rule moves `from_prefix` to `to_prefix`, checks if `path` is that field itself or a
// child field inside it (e.g. moving `"messages"` -> `"turns"` rewrites `"messages[].content"`
// to `"turns[].content"`, while ignoring unrelated fields like `"messages_count"` or `"tools"`).
// Returns the new path if affected, or `std::nullopt` if `path` did not move.
std::optional<std::string> rewritePathPrefix(absl::string_view path, absl::string_view from_prefix,
                                             absl::string_view to_prefix) {
  if (path == from_prefix) {
    return std::string(to_prefix);
  }
  if (absl::StartsWith(path, from_prefix)) {
    const absl::string_view suffix = path.substr(from_prefix.size());
    if (absl::StartsWith(suffix, ".") || absl::StartsWith(suffix, "[]")) {
      return absl::StrCat(to_prefix, suffix);
    }
  }
  return std::nullopt;
}

// Moves every offloadable path under `from_prefix` to `to_prefix`. When `keep_source` is true
// (e.g. `ExtractFromArray`, which leaves non-matching elements in the source array), the source
// path also stays offloadable.
void relocateOffloadablePaths(absl::flat_hash_set<std::string>& offloadable_set,
                              absl::string_view from_prefix, absl::string_view to_prefix,
                              bool keep_source = false) {
  std::vector<std::string> to_remove;
  std::vector<std::string> to_insert;
  for (const std::string& path : offloadable_set) {
    if (auto rewritten = rewritePathPrefix(path, from_prefix, to_prefix); rewritten.has_value()) {
      if (!keep_source) {
        to_remove.push_back(path);
      }
      to_insert.push_back(*std::move(rewritten));
    }
  }
  for (const std::string& path : to_remove) {
    offloadable_set.erase(path);
  }
  for (std::string& path : to_insert) {
    offloadable_set.insert(std::move(path));
  }
}

void dropOffloadablePaths(absl::flat_hash_set<std::string>& offloadable_set,
                          absl::string_view dropped_prefix) {
  for (auto it = offloadable_set.begin(); it != offloadable_set.end();) {
    if (rewritePathPrefix(*it, dropped_prefix, "").has_value()) {
      offloadable_set.erase(it++);
    } else {
      ++it;
    }
  }
}

// Mirrors `RetainOnly`: drops every offloadable path inside the object at `object_path` (empty for
// the root) that sits under a member not in `kept_keys`.
void retainOffloadablePaths(absl::flat_hash_set<std::string>& offloadable_set,
                            absl::string_view object_path,
                            const std::vector<std::string>& kept_keys) {
  for (auto it = offloadable_set.begin(); it != offloadable_set.end();) {
    absl::string_view path = *it;
    if (!object_path.empty()) {
      if (!absl::StartsWith(path, object_path) || path.size() <= object_path.size() ||
          path[object_path.size()] != '.') {
        ++it;
        continue;
      }
      path.remove_prefix(object_path.size() + 1);
    }
    const absl::string_view member = path.substr(0, path.find_first_of(".["));
    if (std::find(kept_keys.begin(), kept_keys.end(), member) == kept_keys.end()) {
      offloadable_set.erase(it++);
    } else {
      ++it;
    }
  }
}

// A predicate path in the verifier's notation, under `prefix`: numeric segments, which index an
// array, become `[]` (`choices.0.delta.content` -> `choices[].delta.content`).
std::string predicateVerifierPath(absl::string_view prefix, absl::string_view path) {
  std::string out(prefix);
  for (absl::string_view segment : absl::StrSplit(path, '.', absl::SkipEmpty())) {
    size_t index = 0;
    if (absl::SimpleAtoi(segment, &index)) {
      absl::StrAppend(&out, "[]");
    } else {
      absl::StrAppend(&out, out.empty() ? "" : ".", segment);
    }
  }
  return out;
}

// Rejects a predicate that reads the value of a field which may arrive offloaded. An `ExternalRef`
// equals nothing and is not a `String`, so such a test would quietly take the wrong branch for
// exactly the large payloads offloading exists for.
absl::Status verifyPredicate(const TranscodePredicate& predicate, absl::string_view prefix,
                             const absl::flat_hash_set<std::string>& offloadable_set) {
  switch (predicate.kind()) {
  case TranscodePredicate::Kind::Always:
    return absl::OkStatus();
  case TranscodePredicate::Kind::Not:
    return verifyPredicate(predicate.operands().front(), prefix, offloadable_set);
  case TranscodePredicate::Kind::FieldIs:
    if (predicate.shape() != JsonShape::String) {
      return absl::OkStatus();
    }
    break;
  case TranscodePredicate::Kind::FieldEquals:
    break;
  }
  const std::string path = predicateVerifierPath(prefix, predicate.path());
  if (offloadable_set.contains(path)) {
    return absl::InvalidArgumentError(absl::StrCat(
        "transcoding verifier error: predicate cannot read the value of offloadable field '", path,
        "' because large values are represented as ExternalRef nodes; test for JsonShape::Text "
        "instead"));
  }
  return absl::OkStatus();
}

// Walks `rules` in execution order, evolving `offloadable_set` as structural rules relocate fields
// and rejecting any `ValueMap` whose target path currently holds an offloadable field.
absl::Status verifyRulesTrackProvenance(const std::vector<TranscodeRule>& rules,
                                        absl::string_view prefix,
                                        absl::flat_hash_set<std::string>& offloadable_set) {
  for (const TranscodeRule& rule : rules) {
    switch (rule.op()) {
    case TranscodeRule::Op::Move:
      relocateOffloadablePaths(offloadable_set, joinRulePath(prefix, rule.sourcePath()),
                               joinRulePath(prefix, rule.targetPath()));
      break;
    case TranscodeRule::Op::FirstOf: {
      const std::string target = joinRulePath(prefix, rule.targetPath());
      for (const std::string& candidate : rule.sourcePaths()) {
        relocateOffloadablePaths(offloadable_set, joinRulePath(prefix, candidate), target);
      }
      break;
    }
    case TranscodeRule::Op::Drop:
      dropOffloadablePaths(offloadable_set, joinRulePath(prefix, rule.sourcePath()));
      break;
    case TranscodeRule::Op::EnsureObject: {
      const std::string target = joinRulePath(prefix, rule.targetPath());
      relocateOffloadablePaths(offloadable_set, target,
                               absl::StrCat(target, ".", rule.extractSubpath()),
                               /*keep_source=*/true);
      break;
    }
    case TranscodeRule::Op::UnwrapSingleKeyObject: {
      const std::string target = joinRulePath(prefix, rule.targetPath());
      relocateOffloadablePaths(offloadable_set, absl::StrCat(target, ".", rule.extractSubpath()),
                               target, /*keep_source=*/true);
      break;
    }
    case TranscodeRule::Op::WrapInArrayObject:
      relocateOffloadablePaths(
          offloadable_set, joinRulePath(prefix, rule.sourcePath()),
          absl::StrCat(joinRulePath(prefix, rule.targetPath()), "[].", rule.extractSubpath()));
      break;
    case TranscodeRule::Op::UnwrapArrayObject: {
      const std::string target = joinRulePath(prefix, rule.targetPath());
      relocateOffloadablePaths(
          offloadable_set,
          absl::StrCat(joinRulePath(prefix, rule.sourcePath()), "[].", rule.extractSubpath()),
          target);
      // Multiple unwrapped parts fan out into `[{"type": "text", "text": ...}]` blocks.
      relocateOffloadablePaths(offloadable_set, target, absl::StrCat(target, "[].text"),
                               /*keep_source=*/true);
      break;
    }
    case TranscodeRule::Op::ExtractFromArray: {
      const std::string target = joinRulePath(prefix, rule.targetPath());
      relocateOffloadablePaths(
          offloadable_set,
          absl::StrCat(joinRulePath(prefix, rule.sourcePath()), "[].", rule.extractSubpath()),
          target, /*keep_source=*/true);
      // Multiple matched elements concatenate into `[{"type": "text", "text": ...}]` blocks.
      relocateOffloadablePaths(offloadable_set, target, absl::StrCat(target, "[].text"),
                               /*keep_source=*/true);
      break;
    }
    case TranscodeRule::Op::PrependToArray:
      relocateOffloadablePaths(
          offloadable_set, joinRulePath(prefix, rule.sourcePath()),
          absl::StrCat(joinRulePath(prefix, rule.targetPath()), "[].", rule.extractSubpath()));
      break;
    case TranscodeRule::Op::ValueMap: {
      const std::string full_path = joinRulePath(prefix, rule.targetPath());
      if (offloadable_set.contains(full_path)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "transcoding verifier error: value_map rule cannot target offloadable field '",
            full_path, "' because large values are represented as ExternalRef nodes"));
      }
      break;
    }
    case TranscodeRule::Op::ForEach: {
      const std::string array_prefix = absl::StrCat(joinRulePath(prefix, rule.targetPath()), "[]");
      absl::Status status =
          verifyRulesTrackProvenance(rule.subRules(), array_prefix, offloadable_set);
      if (!status.ok()) {
        return status;
      }
      break;
    }
    case TranscodeRule::Op::EnsureArray: {
      const std::string target = joinRulePath(prefix, rule.targetPath());
      relocateOffloadablePaths(offloadable_set, target, absl::StrCat(target, "[]"),
                               /*keep_source=*/true);
      break;
    }
    case TranscodeRule::Op::MergeConsecutiveByKey: {
      const std::string merge_field =
          absl::StrCat(joinRulePath(prefix, rule.targetPath()), "[].", rule.extractSubpath());
      relocateOffloadablePaths(offloadable_set, merge_field, absl::StrCat(merge_field, "[].text"),
                               /*keep_source=*/true);
      break;
    }
    case TranscodeRule::Op::SetConst:
      // Unconditional, so whatever sat there is gone. (Conditional writes below leave an existing
      // value, offloaded or not, where it is.)
      dropOffloadablePaths(offloadable_set, joinRulePath(prefix, rule.targetPath()));
      break;
    case TranscodeRule::Op::RetainOnly:
      retainOffloadablePaths(offloadable_set,
                             rule.targetPath().empty() ? std::string(prefix)
                                                       : joinRulePath(prefix, rule.targetPath()),
                             rule.matchValues());
      break;
    case TranscodeRule::Op::TakeFirst: {
      const std::string array = joinRulePath(prefix, rule.sourcePath());
      relocateOffloadablePaths(offloadable_set, absl::StrCat(array, "[]"),
                               joinRulePath(prefix, rule.targetPath()));
      dropOffloadablePaths(offloadable_set, array);
      break;
    }
    case TranscodeRule::Op::CollectText: {
      const std::string array = joinRulePath(prefix, rule.sourcePath());
      const std::string element = absl::StrCat(array, "[]");
      absl::Status status = verifyPredicate(rule.predicate(), element, offloadable_set);
      if (!status.ok()) {
        return status;
      }
      // A single match is moved as is, so the destination may hold an `ExternalRef`.
      relocateOffloadablePaths(offloadable_set, absl::StrCat(element, ".", rule.extractSubpath()),
                               joinRulePath(prefix, rule.targetPath()));
      dropOffloadablePaths(offloadable_set, array);
      break;
    }
    case TranscodeRule::Op::When: {
      absl::Status status = verifyPredicate(rule.predicate(), prefix, offloadable_set);
      if (!status.ok()) {
        return status;
      }
      // The rules may or may not run, so afterwards a field is offloadable if it is on either
      // path.
      absl::flat_hash_set<std::string> branch = offloadable_set;
      status = verifyRulesTrackProvenance(rule.subRules(), prefix, branch);
      if (!status.ok()) {
        return status;
      }
      offloadable_set.insert(branch.begin(), branch.end());
      break;
    }
    case TranscodeRule::Op::Require: {
      absl::Status status = verifyPredicate(rule.predicate(), prefix, offloadable_set);
      if (!status.ok()) {
        return status;
      }
      break;
    }
    case TranscodeRule::Op::CaptureToState: {
      // The captured value outlives the event, so it must neither be nor hold a reference into
      // the event's buffer.
      const std::string source = joinRulePath(prefix, rule.sourcePath());
      for (const std::string& path : offloadable_set) {
        if (rewritePathPrefix(path, source, "").has_value()) {
          return absl::InvalidArgumentError(absl::StrCat(
              "transcoding verifier error: capture_to_state rule cannot capture '", source,
              "' because offloadable field '", path,
              "' may be an ExternalRef into a buffer that does not outlive the event"));
        }
      }
      break;
    }
    case TranscodeRule::Op::SetDefault:
    case TranscodeRule::Op::CoerceNumeric:
    case TranscodeRule::Op::SetFromContext:
    case TranscodeRule::Op::SetFromState:
    case TranscodeRule::Op::Enumerate:
    case TranscodeRule::Op::ConvertUsage:
    case TranscodeRule::Op::AccumulateUsage:
      break;
    }
  }
  return absl::OkStatus();
}

// Anthropic requires `max_tokens`, but it is optional for every other dialect. When a client omits
// it entirely the engine has to synthesize a value or the upstream rejects the request outright.
// TODO(ginama): make this configurable per route rather than a compiled-in default.
constexpr int kDefaultAnthropicMaxTokens = 4096;

// Anthropic Messages request -> IR request.
TranscodeRuleSet anthropicRequestToIr() {
  return TranscodeRuleSet(
      LLMProtocol::AnthropicMessages, TranscodingEngine::kIrProtocol,
      {
          // 1. Prepend top-level `system` prompt into `messages[]` as `{role: "system", ...}`
          TranscodeRule::prependToArray("system", "messages", "role", "system", "content"),
          // 2. Map Anthropic `max_tokens` and `stop_sequences` to IR (OpenAI Chat) names
          TranscodeRule::move("max_tokens", "max_completion_tokens"),
          TranscodeRule::move("stop_sequences", "stop"),
          // 3. Map Anthropic `tools[]` (`{name, description, input_schema}`) to OpenAI
          //    `tools[]` (`{type: "function", function: {name, description, parameters}}`)
          TranscodeRule::forEach("tools",
                                 {
                                     TranscodeRule::move("name", "function.name"),
                                     TranscodeRule::move("description", "function.description"),
                                     TranscodeRule::move("input_schema", "function.parameters"),
                                     TranscodeRule::setDefault("type", "function"),
                                 }),
          // 4. Convert Anthropic `tool_choice` (always an object) to OpenAI IR:
          //      {"type": "auto"|"none"}        -> "auto" | "none"
          //      {"type": "any"}                -> "required"
          //      {"type": "tool", "name": "fn"} -> {"type": "function", "function": {"name":
          //      "fn"}}
          //    `disable_parallel_tool_use` has no OpenAI equivalent and must be dropped first
          //    so `unwrapSingleKeyObject` sees a single-key `{"type": "..."}` object and
          //    collapses it to a string (leaving two-key `{"type": "function", "function":
          //    {...}}` intact).
          TranscodeRule::drop("tool_choice.disable_parallel_tool_use"),
          TranscodeRule::valueMap("tool_choice.type", {{"any", "required"}, {"tool", "function"}}),
          TranscodeRule::move("tool_choice.name", "tool_choice.function.name"),
          TranscodeRule::unwrapSingleKeyObject("tool_choice", "type"),
      });
}

// IR request -> Anthropic Messages request.
TranscodeRuleSet anthropicRequestFromIr() {
  return TranscodeRuleSet(
      TranscodingEngine::kIrProtocol, LLMProtocol::AnthropicMessages,
      {
          // 1. Extract `system` / `developer` messages from `messages[]` into top-level
          // `system`
          TranscodeRule::extractFromArray("messages", "role", {"system", "developer"}, "content",
                                          "system"),
          // 2. Map OpenAI `tool` / `function` roles to `user` and merge adjacent same-role
          //    messages (Anthropic requires strictly alternating `user` / `assistant` roles)
          TranscodeRule::forEach(
              "messages",
              {
                  TranscodeRule::valueMap("role", {{"tool", "user"}, {"function", "user"}}),
              }),
          TranscodeRule::mergeConsecutiveByKey("messages", "role", "content"),
          // 3. Map token cap (`max_completion_tokens` or `max_tokens`) and apply Anthropic's
          //    required `max_tokens` default if the client omitted both
          TranscodeRule::firstOf({"max_completion_tokens", "max_tokens"}, "max_tokens"),
          TranscodeRule::setDefault("max_tokens", kDefaultAnthropicMaxTokens),
          // 4. Map `stop` -> `stop_sequences`. The IR accepts either a bare string or an
          //    array here, while `stop_sequences` is array-only, so normalize first.
          TranscodeRule::ensureArray("stop"),
          TranscodeRule::move("stop", "stop_sequences"),
          // 5. Map OpenAI `tools[]` (`function.{name, description, parameters}`) to Anthropic
          //    `tools[]` (`{name, description, input_schema}`)
          TranscodeRule::forEach("tools",
                                 {
                                     TranscodeRule::move("function.name", "name"),
                                     TranscodeRule::move("function.description", "description"),
                                     TranscodeRule::move("function.parameters", "input_schema"),
                                     TranscodeRule::drop("type"),
                                     TranscodeRule::drop("function"),
                                 }),
          // 6. Map `tool_choice` into Anthropic's object-only form. The wrap turns the IR's
          //    bare `"auto"` / `"none"` / `"required"` into `{"type": ...}`; the pinned-tool
          //    object is already an object and passes through the wrap untouched.
          TranscodeRule::ensureObject("tool_choice", "type"),
          TranscodeRule::valueMap("tool_choice.type", {{"required", "any"}, {"function", "tool"}}),
          TranscodeRule::move("tool_choice.function.name", "tool_choice.name"),
          TranscodeRule::drop("tool_choice.function"),
      });
}

// The IR requires every response to name a model. When neither the response nor the request did,
// this placeholder keeps the document valid.
constexpr char kUnknownModel[] = "transcoded-model";

// Anthropic `stop_reason` -> IR `finish_reason`, at `path`.
TranscodeRule anthropicStopReasonToIr(std::string path) {
  return TranscodeRule::valueMap(std::move(path),
                                 {{"end_turn", "stop"},
                                  {"stop_sequence", "stop"},
                                  {"max_tokens", "length"},
                                  {"tool_use", "tool_calls"}},
                                 TranscodeRule::ValueFallback{"stop"});
}

// IR `finish_reason` -> Anthropic `stop_reason`, at `path`.
TranscodeRule irFinishReasonToAnthropic(std::string path) {
  return TranscodeRule::valueMap(
      std::move(path), {{"stop", "end_turn"}, {"length", "max_tokens"}, {"tool_calls", "tool_use"}},
      TranscodeRule::ValueFallback{"end_turn"});
}

// Anthropic Messages response -> IR response.
TranscodeRuleSet anthropicResponseToIr() {
  return TranscodeRuleSet(
      LLMProtocol::AnthropicMessages, TranscodingEngine::kIrProtocol,
      {
          TranscodeRule::require("content", JsonShape::Array),
          // 1. The IR envelope: `id`, `object`, `created` and `model` are all required.
          TranscodeRule::setDefault("id", "chatcmpl-transcoded"),
          TranscodeRule::setConst("object", "chat.completion"),
          TranscodeRule::setFromContext("created", TranscodeRule::ContextField::NowUnixSeconds),
          TranscodeRule::setFromContext("model", TranscodeRule::ContextField::RequestModel),
          TranscodeRule::setDefault("model", kUnknownModel),
          // 2. Anthropic's input count excludes both cache buckets; the IR's includes them.
          TranscodeRule::convertUsage(LLMProtocol::AnthropicMessages,
                                      TranscodingEngine::kIrProtocol),
          // 3. The message becomes the IR's only choice, its text blocks joined into
          //    `message.content`.
          TranscodeRule::collectText("content", "text", "choice.message.content",
                                     TranscodePredicate::fieldEquals("type", "text")),
          TranscodeRule::move("role", "choice.message.role"),
          TranscodeRule::setDefault("choice.message.role", "assistant"),
          TranscodeRule::move("stop_reason", "choice.finish_reason"),
          anthropicStopReasonToIr("choice.finish_reason"),
          TranscodeRule::setDefault("choice.finish_reason", "stop"),
          TranscodeRule::setConst("choice.index", 0),
          TranscodeRule::move("choice", "choices"),
          TranscodeRule::ensureArray("choices"),
          TranscodeRule::retainOnly("", {"id", "object", "created", "model", "choices", "usage"}),
      });
}

// IR response -> Anthropic Messages response.
TranscodeRuleSet anthropicResponseFromIr() {
  return TranscodeRuleSet(
      TranscodingEngine::kIrProtocol, LLMProtocol::AnthropicMessages,
      {
          TranscodeRule::require("choices", JsonShape::NonEmptyArray),
          // 1. The Message envelope.
          TranscodeRule::setDefault("id", "msg_transcoded"),
          TranscodeRule::setConst("type", "message"),
          TranscodeRule::setConst("role", "assistant"),
          TranscodeRule::setFromContext("model", TranscodeRule::ContextField::RequestModel),
          TranscodeRule::setDefault("model", kUnknownModel),
          // 2. The IR's input count includes both cache buckets; Anthropic's excludes them.
          TranscodeRule::convertUsage(TranscodingEngine::kIrProtocol,
                                      LLMProtocol::AnthropicMessages),
          // 3. A message carries one answer: the first choice's content becomes a text block.
          TranscodeRule::takeFirst("choices", "choice"),
          TranscodeRule::setDefault("choice.message.content", ""),
          TranscodeRule::wrapInArrayObject("choice.message.content", "content", "text"),
          TranscodeRule::forEach("content", {TranscodeRule::setConst("type", "text")}),
          TranscodeRule::move("choice.finish_reason", "stop_reason"),
          irFinishReasonToAnthropic("stop_reason"),
          TranscodeRule::setDefault("stop_reason", "end_turn"),
          TranscodeRule::retainOnly(
              "", {"id", "type", "role", "model", "content", "stop_reason", "usage"}),
      });
}

// Completes the rules for one Anthropic stream event with the IR chunk envelope. Only
// `message_start` names the message and its model, so they are remembered from it for the events
// that follow.
TranscodeRuleSet anthropicEventToIr(std::vector<TranscodeRule> rules) {
  rules.push_back(TranscodeRule::setFromState("id", "id"));
  rules.push_back(TranscodeRule::setDefault("id", "chatcmpl-transcoded"));
  rules.push_back(TranscodeRule::setConst("object", "chat.completion.chunk"));
  rules.push_back(
      TranscodeRule::setFromContext("created", TranscodeRule::ContextField::NowUnixSeconds));
  rules.push_back(TranscodeRule::setFromState("model", "model"));
  rules.push_back(
      TranscodeRule::setFromContext("model", TranscodeRule::ContextField::RequestModel));
  rules.push_back(TranscodeRule::setDefault("model", kUnknownModel));
  rules.push_back(
      TranscodeRule::retainOnly("", {"id", "object", "created", "model", "choices", "usage"}));
  return TranscodeRuleSet(LLMProtocol::AnthropicMessages, TranscodingEngine::kIrProtocol,
                          std::move(rules));
}

// Anthropic `message_start` -> the IR's opening chunk, which only announces the assistant's role.
TranscodeRuleSet anthropicMessageStartToIr() {
  return anthropicEventToIr({
      // Anthropic reports the input side of the usage here and the output side in
      // `message_delta`, so it is summed across the two.
      TranscodeRule::accumulateUsage(LLMProtocol::AnthropicMessages),
      TranscodeRule::captureToState("message.id", "id"),
      TranscodeRule::captureToState("message.model", "model"),
      TranscodeRule::setConst("choice.index", 0),
      TranscodeRule::setConst("choice.delta.role", "assistant"),
      TranscodeRule::setConst("choice.delta.content", ""),
      TranscodeRule::setConst("choice.finish_reason", nullptr),
      TranscodeRule::move("choice", "choices"),
      TranscodeRule::ensureArray("choices"),
  });
}

// Anthropic `content_block_delta` -> an IR content chunk. The event's `index` numbers the content
// blocks of the one message, not choices, so the chunk is always choice 0.
TranscodeRuleSet anthropicContentBlockDeltaToIr() {
  return anthropicEventToIr({
      TranscodeRule::move("delta.text", "choice.delta.content"),
      TranscodeRule::setDefault("choice.delta.content", ""),
      TranscodeRule::setConst("choice.index", 0),
      TranscodeRule::setConst("choice.finish_reason", nullptr),
      TranscodeRule::move("choice", "choices"),
      TranscodeRule::ensureArray("choices"),
  });
}

// Anthropic `message_delta` -> the IR's closing chunk, with the finish reason and the usage.
TranscodeRuleSet anthropicMessageDeltaToIr() {
  return anthropicEventToIr({
      TranscodeRule::accumulateUsage(LLMProtocol::AnthropicMessages,
                                     TranscodingEngine::kIrProtocol),
      TranscodeRule::move("delta.stop_reason", "choice.finish_reason"),
      anthropicStopReasonToIr("choice.finish_reason"),
      TranscodeRule::setDefault("choice.finish_reason", "stop"),
      TranscodeRule::setConst("choice.index", 0),
      TranscodeRule::setConst("choice.delta", nlohmann::json::object()),
      TranscodeRule::move("choice", "choices"),
      TranscodeRule::ensureArray("choices"),
  });
}

// Anthropic Messages stream -> IR chunk stream.
StreamGrammar anthropicStreamToIr() {
  return StreamGrammar{
      .cases =
          {
              {.match = StreamEventMatch::notJson(), .disposition = StreamDisposition::Passthrough},
              {.match = StreamEventMatch::eventType("message_start"),
               .rules = anthropicMessageStartToIr()},
              {.match = StreamEventMatch::eventType("content_block_delta"),
               .rules = anthropicContentBlockDeltaToIr()},
              {.match = StreamEventMatch::eventType("message_delta"),
               .rules = anthropicMessageDeltaToIr()},
              {.match = StreamEventMatch::eventType("message_stop"),
               .disposition = StreamDisposition::Terminate},
              // The IR has no content block boundaries and no keepalives.
              {.match = StreamEventMatch::eventType("content_block_start"),
               .disposition = StreamDisposition::Drop},
              {.match = StreamEventMatch::eventType("content_block_stop"),
               .disposition = StreamDisposition::Drop},
              {.match = StreamEventMatch::eventType("ping"),
               .disposition = StreamDisposition::Drop},
          },
      .on_terminate = {StreamEmit{.raw_data = std::string(kSseDone)}},
  };
}

// An IR content chunk -> Anthropic `content_block_delta`, into the message's one text block.
TranscodeRuleSet anthropicTextDeltaFromIr() {
  return TranscodeRuleSet(TranscodingEngine::kIrProtocol, LLMProtocol::AnthropicMessages,
                          {
                              TranscodeRule::takeFirst("choices", "choice"),
                              TranscodeRule::setConst("type", "content_block_delta"),
                              TranscodeRule::setConst("index", 0),
                              TranscodeRule::setConst("delta.type", "text_delta"),
                              TranscodeRule::move("choice.delta.content", "delta.text"),
                              TranscodeRule::retainOnly("", {"type", "index", "delta"}),
                          });
}

// The IR's closing chunk -> Anthropic `message_delta`.
TranscodeRuleSet anthropicMessageDeltaFromIr() {
  return TranscodeRuleSet(TranscodingEngine::kIrProtocol, LLMProtocol::AnthropicMessages,
                          {
                              TranscodeRule::convertUsage(TranscodingEngine::kIrProtocol,
                                                          LLMProtocol::AnthropicMessages),
                              TranscodeRule::takeFirst("choices", "choice"),
                              TranscodeRule::setConst("type", "message_delta"),
                              TranscodeRule::move("choice.finish_reason", "delta.stop_reason"),
                              irFinishReasonToAnthropic("delta.stop_reason"),
                              TranscodeRule::setConst("delta.stop_sequence", nullptr),
                              TranscodeRule::retainOnly("", {"type", "delta", "usage"}),
                          });
}

// Any other IR chunk -> Anthropic `message_start`.
TranscodeRuleSet anthropicMessageStartFromIr() {
  return TranscodeRuleSet(
      TranscodingEngine::kIrProtocol, LLMProtocol::AnthropicMessages,
      {
          TranscodeRule::require("choices", JsonShape::NonEmptyArray),
          TranscodeRule::setConst("type", "message_start"),
          TranscodeRule::move("id", "message.id"),
          TranscodeRule::setDefault("message.id", "msg_transcoded"),
          TranscodeRule::setConst("message.type", "message"),
          TranscodeRule::setConst("message.role", "assistant"),
          TranscodeRule::move("model", "message.model"),
          TranscodeRule::setFromContext("message.model", TranscodeRule::ContextField::RequestModel),
          TranscodeRule::setDefault("message.model", kUnknownModel),
          TranscodeRule::setConst("message.content", nlohmann::json::array()),
          TranscodeRule::retainOnly("", {"type", "message"}),
      });
}

// IR chunk stream -> Anthropic Messages stream. Each chunk is read by what its first choice
// carries: text, else a finish reason, else nothing but the opening role.
StreamGrammar anthropicStreamFromIr() {
  return StreamGrammar{
      .cases =
          {
              {.match = StreamEventMatch::isDone(), .disposition = StreamDisposition::Terminate},
              {.match = StreamEventMatch::notJson(), .disposition = StreamDisposition::Passthrough},
              {.match = StreamEventMatch::json(
                   TranscodePredicate::fieldIs("choices.0.delta.content", JsonShape::Text)),
               .rules = anthropicTextDeltaFromIr(),
               .output_event = "content_block_delta"},
              {.match = StreamEventMatch::json(
                   TranscodePredicate::fieldIs("choices.0.finish_reason", JsonShape::String)),
               .rules = anthropicMessageDeltaFromIr(),
               .output_event = "message_delta"},
              {.match = StreamEventMatch::json(),
               .rules = anthropicMessageStartFromIr(),
               .output_event = "message_start"},
          },
      .on_terminate = {StreamEmit{.event = "message_stop",
                                  .json = nlohmann::json::object({{"type", "message_stop"}})}},
  };
}

// Builds the declarative transcoding pack for Anthropic Messages <-> IR (OpenAI Chat).
DialectTranscodePack createAnthropicTranscodePack() {
  return DialectTranscodePack{
      .protocol = LLMProtocol::AnthropicMessages,
      .request = {.to_ir = anthropicRequestToIr(), .from_ir = anthropicRequestFromIr()},
      .response = {.to_ir = anthropicResponseToIr(), .from_ir = anthropicResponseFromIr()},
      .stream = {.to_ir = anthropicStreamToIr(), .from_ir = anthropicStreamFromIr()},
  };
}

// Gemini GenerateContent request -> IR request.
TranscodeRuleSet geminiRequestToIr() {
  return TranscodeRuleSet(
      LLMProtocol::GeminiGenerateContent, TranscodingEngine::kIrProtocol,
      {
          // 1. Unwrap `systemInstruction.parts[0].text` -> `system`, then prepend to
          //    `contents` before renaming `contents` -> `messages`
          TranscodeRule::unwrapArrayObject("systemInstruction.parts", "text", "system"),
          TranscodeRule::drop("systemInstruction"),
          TranscodeRule::unwrapArrayObject("system_instruction.parts", "text", "system"),
          TranscodeRule::drop("system_instruction"),
          // 2. Move `contents` -> `messages`, unwrap `parts[0].text` -> `content`, and map
          //    Gemini role `"model"` -> `"assistant"`
          TranscodeRule::move("contents", "messages"),
          TranscodeRule::forEach(
              "messages",
              {
                  // Gemini leaves `role` optional and Vertex defaults it to `user`. Both
                  // other dialects require it, so materialize the source default here
                  // rather than let an otherwise valid request fail their role check.
                  TranscodeRule::setDefault("role", "user"),
                  TranscodeRule::valueMap("role", {{"model", "assistant"}}),
                  TranscodeRule::unwrapArrayObject("parts", "text", "content"),
              }),
          TranscodeRule::prependToArray("system", "messages", "role", "system", "content"),
          // 3. Hoist `generationConfig` / `generation_config` parameters to top-level IR
          //    fields. Gemini renders proto numbers through ProtoJSON, so each of these may
          //    arrive quoted (`"maxOutputTokens": "256"`). The IR and both other dialects
          //    declare them as real numbers, so coerce after the hoist: the destination path
          //    is single, while each source has up to four spellings.
          TranscodeRule::firstOf(
              {"generationConfig.maxOutputTokens", "generationConfig.max_output_tokens",
               "generation_config.maxOutputTokens", "generation_config.max_output_tokens"},
              "max_completion_tokens"),
          TranscodeRule::toInteger("max_completion_tokens"),
          TranscodeRule::firstOf({"generationConfig.temperature", "generation_config.temperature"},
                                 "temperature"),
          TranscodeRule::toNumber("temperature"),
          TranscodeRule::firstOf({"generationConfig.topP", "generationConfig.top_p",
                                  "generation_config.topP", "generation_config.top_p"},
                                 "top_p"),
          TranscodeRule::toNumber("top_p"),
          // `stopSequences` is already an array of strings in both dialects.
          TranscodeRule::firstOf(
              {"generationConfig.stopSequences", "generationConfig.stop_sequences",
               "generation_config.stopSequences", "generation_config.stop_sequences"},
              "stop"),
          // Dropping the rest of `generationConfig` also masks a latent version of the
          // coercion above: `candidateCount`, `topK`, `seed`, `presencePenalty`,
          // `frequencyPenalty`, `logprobs` and `thinkingConfig.thinkingBudget` are all
          // declared number-or-string too. Whoever makes the IR lossless must coerce them
          // on the way through, or they reach the destination quoted.
          TranscodeRule::drop("generationConfig"),
          TranscodeRule::drop("generation_config"),
          // TODO(ginama): Map `toolConfig.functionCallingConfig` -> `tool_choice`.
          // Gemini uses `mode: "ANY"` for both `"required"` (when `allowedFunctionNames` is
          // omitted) and `{"type": "function", "function": {"name": "fn"}}` (when
          // `allowedFunctionNames` is set), which requires conditional mapping support.
      });
}

// IR request -> Gemini GenerateContent request.
TranscodeRuleSet geminiRequestFromIr() {
  return TranscodeRuleSet(
      TranscodingEngine::kIrProtocol, LLMProtocol::GeminiGenerateContent,
      {
          // 1. Extract `system` / `developer` messages from `messages[]` and wrap into
          //    `systemInstruction.parts[{text: ...}]`
          TranscodeRule::extractFromArray("messages", "role", {"system", "developer"}, "content",
                                          "systemInstruction.content"),
          TranscodeRule::wrapInArrayObject("systemInstruction.content", "systemInstruction.parts",
                                           "text"),
          // 2. Transform `messages[]` -> `contents[]`, mapping `"assistant"` -> `"model"`
          //    and wrapping `content` -> `parts: [{text: <moved_node>}]`
          TranscodeRule::forEach(
              "messages",
              {
                  TranscodeRule::valueMap("role", {{"assistant", "model"}, {"tool", "user"}}),
                  TranscodeRule::wrapInArrayObject("content", "parts", "text"),
              }),
          TranscodeRule::move("messages", "contents"),
          // 3. Nest generation parameters under `generationConfig`
          TranscodeRule::firstOf({"max_completion_tokens", "max_tokens"},
                                 "generationConfig.maxOutputTokens"),
          TranscodeRule::move("temperature", "generationConfig.temperature"),
          TranscodeRule::move("top_p", "generationConfig.topP"),
          // `stopSequences` is array-only, while the IR also allows a bare string.
          TranscodeRule::ensureArray("stop"),
          TranscodeRule::move("stop", "generationConfig.stopSequences"),
          // 4. Map `tool_choice` to `toolConfig.functionCallingConfig`. Without this the
          //    field rides through as an unknown member: Gemini's root sets
          //    `allowUnknownFields(true)`, so the request is accepted and the caller's
          //    constraint is silently ignored rather than rejected.
          //    A pinned tool becomes `mode: ANY` plus a single-entry allow-list, which is
          //    how Gemini spells "call exactly this function".
          TranscodeRule::ensureObject("tool_choice", "type"),
          TranscodeRule::move("tool_choice.function.name",
                              "toolConfig.functionCallingConfig.allowedFunctionNames"),
          TranscodeRule::ensureArray("toolConfig.functionCallingConfig.allowedFunctionNames"),
          TranscodeRule::valueMap(
              "tool_choice.type",
              {{"auto", "AUTO"}, {"none", "NONE"}, {"required", "ANY"}, {"function", "ANY"}}),
          TranscodeRule::move("tool_choice.type", "toolConfig.functionCallingConfig.mode"),
          TranscodeRule::drop("tool_choice"),
          // 5. Gemini has no stream options: it reports usage in every stream. `model` and
          //    `stream` go in the request path instead; see the pack's `envelope`.
          TranscodeRule::drop("stream_options"),
      });
}

// Gemini `finishReason` -> IR `finish_reason`, at `path`.
TranscodeRule geminiFinishReasonToIr(std::string path) {
  return TranscodeRule::valueMap(std::move(path),
                                 {{"STOP", "stop"},
                                  {"MAX_TOKENS", "length"},
                                  {"SAFETY", "content_filter"},
                                  {"RECITATION", "content_filter"},
                                  {"BLOCKLIST", "content_filter"}},
                                 TranscodeRule::ValueFallback{"stop"});
}

// IR `finish_reason` -> Gemini `finishReason`, at `path`.
TranscodeRule irFinishReasonToGemini(std::string path) {
  return TranscodeRule::valueMap(
      std::move(path), {{"stop", "STOP"}, {"length", "MAX_TOKENS"}, {"content_filter", "SAFETY"}},
      TranscodeRule::ValueFallback{"STOP"});
}

// The Gemini parts that carry the answer. Thought summaries are the model's reasoning, not its
// answer, so they stay out of the IR.
TranscodePredicate geminiAnswerPart() {
  return TranscodePredicate::negate(TranscodePredicate::fieldEquals("thought", true));
}

// Gemini GenerateContent response -> IR response.
TranscodeRuleSet geminiResponseToIr() {
  return TranscodeRuleSet(
      LLMProtocol::GeminiGenerateContent, TranscodingEngine::kIrProtocol,
      {
          TranscodeRule::require("candidates", JsonShape::Array),
          // 1. The IR envelope: `id`, `object`, `created` and `model` are all required.
          TranscodeRule::move("responseId", "id"),
          TranscodeRule::setDefault("id", "chatcmpl-transcoded"),
          TranscodeRule::setConst("object", "chat.completion"),
          TranscodeRule::setFromContext("created", TranscodeRule::ContextField::NowUnixSeconds),
          TranscodeRule::move("modelVersion", "model"),
          TranscodeRule::setFromContext("model", TranscodeRule::ContextField::RequestModel),
          TranscodeRule::setDefault("model", kUnknownModel),
          // 2. Gemini's prompt and candidates counts exclude tool-use and thought tokens; the
          //    IR's include them.
          TranscodeRule::convertUsage(LLMProtocol::GeminiGenerateContent,
                                      TranscodingEngine::kIrProtocol),
          // 3. Each candidate becomes a choice, its text parts joined into `message.content`.
          TranscodeRule::move("candidates", "choices"),
          TranscodeRule::enumerate("choices", "index"),
          TranscodeRule::forEach(
              "choices",
              {
                  TranscodeRule::collectText("content.parts", "text", "message.content",
                                             geminiAnswerPart()),
                  TranscodeRule::setDefault("message.content", ""),
                  TranscodeRule::setConst("message.role", "assistant"),
                  TranscodeRule::move("finishReason", "finish_reason"),
                  geminiFinishReasonToIr("finish_reason"),
                  TranscodeRule::setDefault("finish_reason", "stop"),
                  TranscodeRule::retainOnly("", {"index", "message", "finish_reason"}),
              }),
          TranscodeRule::retainOnly("", {"id", "object", "created", "model", "choices", "usage"}),
      });
}

// IR response -> Gemini GenerateContent response.
TranscodeRuleSet geminiResponseFromIr() {
  return TranscodeRuleSet(
      TranscodingEngine::kIrProtocol, LLMProtocol::GeminiGenerateContent,
      {
          TranscodeRule::require("choices", JsonShape::NonEmptyArray),
          TranscodeRule::move("model", "modelVersion"),
          // 1. The IR's prompt and completion counts include tool-use and reasoning tokens;
          //    Gemini's exclude them.
          TranscodeRule::convertUsage(TranscodingEngine::kIrProtocol,
                                      LLMProtocol::GeminiGenerateContent),
          // 2. Each choice becomes a candidate, its content a single text part.
          TranscodeRule::move("choices", "candidates"),
          TranscodeRule::enumerate("candidates", "index"),
          TranscodeRule::forEach(
              "candidates",
              {
                  TranscodeRule::setDefault("message.content", ""),
                  TranscodeRule::wrapInArrayObject("message.content", "content.parts", "text"),
                  TranscodeRule::setConst("content.role", "model"),
                  TranscodeRule::move("finish_reason", "finishReason"),
                  irFinishReasonToGemini("finishReason"),
                  TranscodeRule::setDefault("finishReason", "STOP"),
                  TranscodeRule::retainOnly("", {"index", "content", "finishReason"}),
              }),
          TranscodeRule::retainOnly("", {"candidates", "modelVersion", "usageMetadata"}),
      });
}

// One Gemini stream chunk -> one IR chunk. Unlike Anthropic's, every Gemini chunk is a complete
// response in miniature: envelope, candidates and the usage so far.
TranscodeRuleSet geminiChunkToIr() {
  return TranscodeRuleSet(
      LLMProtocol::GeminiGenerateContent, TranscodingEngine::kIrProtocol,
      {
          TranscodeRule::require("candidates", JsonShape::Array),
          // 1. The IR chunk envelope.
          TranscodeRule::move("responseId", "id"),
          TranscodeRule::setDefault("id", "chatcmpl-transcoded"),
          TranscodeRule::setConst("object", "chat.completion.chunk"),
          TranscodeRule::setFromContext("created", TranscodeRule::ContextField::NowUnixSeconds),
          TranscodeRule::move("modelVersion", "model"),
          TranscodeRule::setFromContext("model", TranscodeRule::ContextField::RequestModel),
          TranscodeRule::setDefault("model", kUnknownModel),
          // 2. As in the unary response, the IR's counts include tool-use and thought tokens.
          TranscodeRule::convertUsage(LLMProtocol::GeminiGenerateContent,
                                      TranscodingEngine::kIrProtocol),
          // 3. Each candidate becomes a choice whose delta carries the chunk's answer text. A
          //    candidate without parts (e.g. one that only finishes) has an empty delta.
          TranscodeRule::move("candidates", "choices"),
          TranscodeRule::enumerate("choices", "index"),
          TranscodeRule::forEach(
              "choices",
              {
                  TranscodeRule::when(
                      TranscodePredicate::fieldIs("content.parts", JsonShape::Array),
                      {
                          TranscodeRule::collectText("content.parts", "text", "delta.content",
                                                     geminiAnswerPart()),
                          TranscodeRule::setConst("delta.role", "assistant"),
                      }),
                  TranscodeRule::setDefault("delta", nlohmann::json::object()),
                  TranscodeRule::move("finishReason", "finish_reason"),
                  geminiFinishReasonToIr("finish_reason"),
                  TranscodeRule::setDefault("finish_reason", nullptr),
                  TranscodeRule::retainOnly("", {"index", "delta", "finish_reason"}),
              }),
          TranscodeRule::retainOnly("", {"id", "object", "created", "model", "choices", "usage"}),
      });
}

// Gemini GenerateContent stream -> IR chunk stream.
StreamGrammar geminiStreamToIr() {
  return StreamGrammar{
      .cases =
          {
              {.match = StreamEventMatch::notJson(), .disposition = StreamDisposition::Passthrough},
              {.match = StreamEventMatch::json(), .rules = geminiChunkToIr()},
          },
      // A Gemini stream just ends, while an IR client waits for `[DONE]`.
      .on_source_end = {StreamEmit{.raw_data = std::string(kSseDone)}},
  };
}

// One IR chunk -> one Gemini stream chunk.
TranscodeRuleSet geminiChunkFromIr() {
  return TranscodeRuleSet(
      TranscodingEngine::kIrProtocol, LLMProtocol::GeminiGenerateContent,
      {
          // A chunk without choices (e.g. OpenAI's trailing usage-only chunk) has no candidate
          // to carry.
          TranscodeRule::require("choices", JsonShape::NonEmptyArray),
          TranscodeRule::move("model", "modelVersion"),
          TranscodeRule::convertUsage(TranscodingEngine::kIrProtocol,
                                      LLMProtocol::GeminiGenerateContent),
          // Each choice becomes a candidate, its delta a single text part. Only the last chunk
          // finishes, so `finishReason` is left out until one does.
          TranscodeRule::move("choices", "candidates"),
          TranscodeRule::enumerate("candidates", "index"),
          TranscodeRule::forEach(
              "candidates",
              {
                  TranscodeRule::setDefault("delta.content", ""),
                  TranscodeRule::wrapInArrayObject("delta.content", "content.parts", "text"),
                  TranscodeRule::setConst("content.role", "model"),
                  TranscodeRule::when(
                      TranscodePredicate::fieldIs("finish_reason", JsonShape::String),
                      {
                          TranscodeRule::move("finish_reason", "finishReason"),
                          irFinishReasonToGemini("finishReason"),
                      }),
                  TranscodeRule::retainOnly("", {"index", "content", "finishReason"}),
              }),
          TranscodeRule::retainOnly("", {"candidates", "modelVersion", "usageMetadata"}),
      });
}

// IR chunk stream -> Gemini GenerateContent stream.
StreamGrammar geminiStreamFromIr() {
  return StreamGrammar{
      .cases =
          {
              // A Gemini stream has no terminator of its own: it just ends.
              {.match = StreamEventMatch::isDone(), .disposition = StreamDisposition::Terminate},
              {.match = StreamEventMatch::notJson(), .disposition = StreamDisposition::Passthrough},
              {.match = StreamEventMatch::json(), .rules = geminiChunkFromIr()},
          },
  };
}

// Builds the declarative transcoding pack for Gemini GenerateContent <-> IR (OpenAI Chat).
DialectTranscodePack createGeminiTranscodePack() {
  return DialectTranscodePack{
      .protocol = LLMProtocol::GeminiGenerateContent,
      .request = {.to_ir = geminiRequestToIr(), .from_ir = geminiRequestFromIr()},
      .response = {.to_ir = geminiResponseToIr(), .from_ir = geminiResponseFromIr()},
      .stream = {.to_ir = geminiStreamToIr(), .from_ir = geminiStreamFromIr()},
      // The Gemini API's layout. A route in front of another, such as Vertex AI's, rewrites the
      // `/v1beta/models/` prefix.
      .envelope =
          PathTemplate{
              .prefix = "/v1beta/models/",
              .unary_method = ":generateContent",
              .stream_method = ":streamGenerateContent?alt=sse",
          },
  };
}

// Builds the identity/normalization pack for OpenAI Chat Completions (the IR protocol).
DialectTranscodePack createOpenAiChatTranscodePack() {
  return DialectTranscodePack{
      .protocol = LLMProtocol::OpenAiChatCompletions,
      .request = {.to_ir = TranscodeRuleSet(LLMProtocol::OpenAiChatCompletions,
                                            TranscodingEngine::kIrProtocol, {}),
                  .from_ir = TranscodeRuleSet(TranscodingEngine::kIrProtocol,
                                              LLMProtocol::OpenAiChatCompletions, {})},
  };
}

} // namespace

TranscodePredicate TranscodePredicate::always() { return TranscodePredicate(Kind::Always); }

TranscodePredicate TranscodePredicate::fieldEquals(std::string path, nlohmann::json value) {
  TranscodePredicate predicate(Kind::FieldEquals);
  predicate.segments_ = splitPath(path);
  predicate.path_ = std::move(path);
  predicate.value_ = std::move(value);
  return predicate;
}

TranscodePredicate TranscodePredicate::fieldIs(std::string path, JsonShape shape) {
  TranscodePredicate predicate(Kind::FieldIs);
  predicate.segments_ = splitPath(path);
  predicate.path_ = std::move(path);
  predicate.shape_ = shape;
  return predicate;
}

TranscodePredicate TranscodePredicate::negate(TranscodePredicate predicate) {
  TranscodePredicate negation(Kind::Not);
  negation.operands_.push_back(std::move(predicate));
  return negation;
}

bool TranscodePredicate::matches(const nlohmann::json& json) const {
  switch (kind_) {
  case Kind::Always:
    return true;
  case Kind::FieldEquals: {
    const nlohmann::json* node = peekNodeByPath(json, segments_);
    return node != nullptr && *node == value_;
  }
  case Kind::FieldIs: {
    const nlohmann::json* node = peekNodeByPath(json, segments_);
    return node != nullptr && hasShape(*node, shape_);
  }
  case Kind::Not:
    return !operands_.front().matches(json);
  }
  return false;
}

StreamEventMatch StreamEventMatch::json(TranscodePredicate predicate) {
  StreamEventMatch match(Kind::Json);
  match.predicate_ = std::move(predicate);
  return match;
}

StreamEventMatch StreamEventMatch::eventType(std::string type) {
  StreamEventMatch match(Kind::EventType);
  match.type_ = std::move(type);
  return match;
}

StreamEventMatch StreamEventMatch::isDone() { return StreamEventMatch(Kind::IsDone); }

StreamEventMatch StreamEventMatch::notJson() { return StreamEventMatch(Kind::NotJson); }

bool StreamEventMatch::matches(SseEvent& event) const {
  switch (kind_) {
  case Kind::Json:
    return event.is_json() && predicate_.matches(event.json().json());
  case Kind::EventType: {
    if (!event.is_json()) {
      return false;
    }
    const nlohmann::json& payload = event.json().json();
    if (payload.is_object()) {
      if (const auto type = payload.find("type"); type != payload.end() && type->is_string()) {
        return type->get_ref<const std::string&>() == type_;
      }
    }
    return event.event() == type_;
  }
  case Kind::IsDone:
    // The length check first, so a large raw payload is never linearized just to be compared.
    return !event.is_json() && event.raw_data().length() == kSseDone.size() &&
           event.raw_data_as_string() == kSseDone;
  case Kind::NotJson:
    return !event.is_json();
  }
  return false;
}

TranscodeRule TranscodeRule::move(std::string from_path, std::string to_path) {
  TranscodeRule rule(Op::Move);
  rule.source_segments_ = splitPath(from_path);
  rule.source_path_ = std::move(from_path);
  rule.target_segments_ = splitPath(to_path);
  rule.target_path_ = std::move(to_path);
  return rule;
}

TranscodeRule TranscodeRule::firstOf(std::initializer_list<std::string> from_paths,
                                     std::string to_path) {
  TranscodeRule rule(Op::FirstOf);
  rule.source_paths_.assign(from_paths.begin(), from_paths.end());
  rule.source_paths_segments_.reserve(rule.source_paths_.size());
  for (const std::string& candidate : rule.source_paths_) {
    rule.source_paths_segments_.push_back(splitPath(candidate));
  }
  rule.target_segments_ = splitPath(to_path);
  rule.target_path_ = std::move(to_path);
  return rule;
}

TranscodeRule TranscodeRule::drop(std::string path) {
  TranscodeRule rule(Op::Drop);
  rule.source_segments_ = splitPath(path);
  rule.source_path_ = std::move(path);
  return rule;
}

TranscodeRule TranscodeRule::setDefault(std::string path, nlohmann::json default_value) {
  TranscodeRule rule(Op::SetDefault);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.default_value_ = std::move(default_value);
  return rule;
}

TranscodeRule TranscodeRule::ensureArray(std::string path) {
  TranscodeRule rule(Op::EnsureArray);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  return rule;
}

TranscodeRule TranscodeRule::ensureObject(std::string path, std::string key) {
  TranscodeRule rule(Op::EnsureObject);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.extract_subpath_ = std::move(key);
  return rule;
}

TranscodeRule TranscodeRule::unwrapSingleKeyObject(std::string path, std::string key) {
  TranscodeRule rule(Op::UnwrapSingleKeyObject);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.extract_subpath_ = std::move(key);
  return rule;
}

TranscodeRule TranscodeRule::toNumber(std::string path) {
  TranscodeRule rule(Op::CoerceNumeric);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  return rule;
}

TranscodeRule TranscodeRule::toInteger(std::string path) {
  TranscodeRule rule(Op::CoerceNumeric);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.integral_ = true;
  return rule;
}

TranscodeRule TranscodeRule::valueMap(std::string path,
                                      std::initializer_list<ValueMapping> mappings,
                                      UnknownValuePolicy unknown_policy) {
  TranscodeRule rule(Op::ValueMap);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  for (const auto& m : mappings) {
    rule.value_mappings_.emplace(m.from, m.to);
  }
  rule.unknown_policy_ = unknown_policy;
  return rule;
}

TranscodeRule TranscodeRule::valueMap(std::string path, std::vector<ValueMapping> mappings,
                                      ValueFallback fallback) {
  TranscodeRule rule(Op::ValueMap);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  for (ValueMapping& m : mappings) {
    rule.value_mappings_.emplace(std::move(m.from), std::move(m.to));
  }
  rule.unknown_policy_ = UnknownValuePolicy::Fallback;
  rule.default_value_ = std::move(fallback.value);
  return rule;
}

TranscodeRule TranscodeRule::forEach(std::string array_path,
                                     std::initializer_list<TranscodeRule> rules) {
  return forEach(std::move(array_path), std::vector<TranscodeRule>(rules));
}

TranscodeRule TranscodeRule::forEach(std::string array_path, std::vector<TranscodeRule> rules) {
  TranscodeRule rule(Op::ForEach);
  rule.target_segments_ = splitPath(array_path);
  rule.target_path_ = std::move(array_path);
  rule.sub_rules_ = std::move(rules);
  return rule;
}

TranscodeRule TranscodeRule::extractFromArray(std::string array_path, std::string predicate_field,
                                              std::initializer_list<std::string> match_values,
                                              std::string extract_subpath,
                                              std::string target_path) {
  TranscodeRule rule(Op::ExtractFromArray);
  rule.source_segments_ = splitPath(array_path);
  rule.source_path_ = std::move(array_path);
  rule.predicate_field_ = std::move(predicate_field);
  rule.match_values_.assign(match_values.begin(), match_values.end());
  rule.extract_subpath_segments_ = splitPath(extract_subpath);
  rule.extract_subpath_ = std::move(extract_subpath);
  rule.target_segments_ = splitPath(target_path);
  rule.target_path_ = std::move(target_path);
  return rule;
}

TranscodeRule TranscodeRule::prependToArray(std::string source_path, std::string array_path,
                                            std::string key_field, std::string key_value,
                                            std::string value_subpath) {
  TranscodeRule rule(Op::PrependToArray);
  rule.source_segments_ = splitPath(source_path);
  rule.source_path_ = std::move(source_path);
  rule.target_segments_ = splitPath(array_path);
  rule.target_path_ = std::move(array_path);
  rule.predicate_field_ = std::move(key_field);
  rule.match_values_ = {std::move(key_value)};
  rule.extract_subpath_segments_ = splitPath(value_subpath);
  rule.extract_subpath_ = std::move(value_subpath);
  return rule;
}

TranscodeRule TranscodeRule::wrapInArrayObject(std::string from_path, std::string to_array_path,
                                               std::string element_key) {
  TranscodeRule rule(Op::WrapInArrayObject);
  rule.source_segments_ = splitPath(from_path);
  rule.source_path_ = std::move(from_path);
  rule.target_segments_ = splitPath(to_array_path);
  rule.target_path_ = std::move(to_array_path);
  rule.extract_subpath_ = std::move(element_key);
  return rule;
}

TranscodeRule TranscodeRule::unwrapArrayObject(std::string from_array_path, std::string element_key,
                                               std::string to_path) {
  TranscodeRule rule(Op::UnwrapArrayObject);
  rule.source_segments_ = splitPath(from_array_path);
  rule.source_path_ = std::move(from_array_path);
  rule.extract_subpath_segments_ = splitPath(element_key);
  rule.extract_subpath_ = std::move(element_key);
  rule.target_segments_ = splitPath(to_path);
  rule.target_path_ = std::move(to_path);
  return rule;
}

TranscodeRule TranscodeRule::mergeConsecutiveByKey(std::string array_path, std::string key_field,
                                                   std::string merge_field) {
  TranscodeRule rule(Op::MergeConsecutiveByKey);
  rule.target_segments_ = splitPath(array_path);
  rule.target_path_ = std::move(array_path);
  rule.predicate_field_ = std::move(key_field);
  rule.extract_subpath_ = std::move(merge_field);
  return rule;
}

TranscodeRule TranscodeRule::setConst(std::string path, nlohmann::json value) {
  TranscodeRule rule(Op::SetConst);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.default_value_ = std::move(value);
  return rule;
}

TranscodeRule TranscodeRule::setFromContext(std::string path, ContextField field) {
  TranscodeRule rule(Op::SetFromContext);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.context_field_ = field;
  return rule;
}

TranscodeRule TranscodeRule::enumerate(std::string array_path, std::string key) {
  TranscodeRule rule(Op::Enumerate);
  rule.target_segments_ = splitPath(array_path);
  rule.target_path_ = std::move(array_path);
  rule.extract_subpath_ = std::move(key);
  return rule;
}

TranscodeRule TranscodeRule::retainOnly(std::string path, std::initializer_list<std::string> keys) {
  TranscodeRule rule(Op::RetainOnly);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.match_values_.assign(keys.begin(), keys.end());
  return rule;
}

TranscodeRule TranscodeRule::takeFirst(std::string array_path, std::string to_path) {
  TranscodeRule rule(Op::TakeFirst);
  rule.source_segments_ = splitPath(array_path);
  rule.source_path_ = std::move(array_path);
  rule.target_segments_ = splitPath(to_path);
  rule.target_path_ = std::move(to_path);
  return rule;
}

TranscodeRule TranscodeRule::collectText(std::string array_path, std::string key,
                                         std::string to_path, TranscodePredicate where) {
  TranscodeRule rule(Op::CollectText);
  rule.source_segments_ = splitPath(array_path);
  rule.source_path_ = std::move(array_path);
  rule.extract_subpath_segments_ = splitPath(key);
  rule.extract_subpath_ = std::move(key);
  rule.target_segments_ = splitPath(to_path);
  rule.target_path_ = std::move(to_path);
  rule.predicate_ = std::move(where);
  return rule;
}

TranscodeRule TranscodeRule::when(TranscodePredicate predicate,
                                  std::initializer_list<TranscodeRule> rules) {
  return when(std::move(predicate), std::vector<TranscodeRule>(rules));
}

TranscodeRule TranscodeRule::when(TranscodePredicate predicate, std::vector<TranscodeRule> rules) {
  TranscodeRule rule(Op::When);
  rule.predicate_ = std::move(predicate);
  rule.sub_rules_ = std::move(rules);
  return rule;
}

TranscodeRule TranscodeRule::require(std::string path, JsonShape shape) {
  TranscodeRule rule(Op::Require);
  rule.predicate_ = TranscodePredicate::fieldIs(path, shape);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  return rule;
}

TranscodeRule TranscodeRule::captureToState(std::string path, std::string slot) {
  TranscodeRule rule(Op::CaptureToState);
  rule.source_segments_ = splitPath(path);
  rule.source_path_ = std::move(path);
  rule.slot_ = std::move(slot);
  return rule;
}

TranscodeRule TranscodeRule::setFromState(std::string path, std::string slot) {
  TranscodeRule rule(Op::SetFromState);
  rule.target_segments_ = splitPath(path);
  rule.target_path_ = std::move(path);
  rule.slot_ = std::move(slot);
  return rule;
}

TranscodeRule TranscodeRule::convertUsage(LLMProtocol from, LLMProtocol to) {
  TranscodeRule rule(Op::ConvertUsage);
  rule.usage_from_ = from;
  rule.usage_to_ = to;
  return rule;
}

TranscodeRule TranscodeRule::accumulateUsage(LLMProtocol from, LLMProtocol render_to) {
  TranscodeRule rule(Op::AccumulateUsage);
  rule.usage_from_ = from;
  rule.usage_to_ = render_to;
  return rule;
}

absl::Status TranscodeRule::apply(nlohmann::json& json, TranscodeContext* ctx) const {
  if (!json.is_object()) {
    return absl::InvalidArgumentError("transcoding target must be a JSON object");
  }

  switch (op_) {
  case Op::Move: {
    if (source_path_ == target_path_) {
      return absl::OkStatus();
    }
    if (std::optional<nlohmann::json> val = extractNodeByPath(json, source_segments_);
        val.has_value()) {
      setNodeByPath(json, target_segments_, std::move(*val));
    }
    return absl::OkStatus();
  }

  case Op::FirstOf: {
    std::optional<nlohmann::json> chosen;
    for (const std::vector<std::string>& candidate_segments : source_paths_segments_) {
      std::optional<nlohmann::json> extracted = extractNodeByPath(json, candidate_segments);
      if (!chosen.has_value() && extracted.has_value() && !extracted->is_null()) {
        chosen = std::move(extracted);
      }
    }
    if (chosen.has_value()) {
      setNodeByPath(json, target_segments_, std::move(*chosen));
    }
    return absl::OkStatus();
  }

  case Op::Drop: {
    extractNodeByPath(json, source_segments_);
    return absl::OkStatus();
  }

  case Op::SetDefault: {
    const nlohmann::json* existing = findNodeByPath(json, target_segments_);
    if (existing == nullptr || existing->is_null()) {
      nlohmann::json copy = default_value_;
      setNodeByPath(json, target_segments_, std::move(copy));
    }
    return absl::OkStatus();
  }

  case Op::EnsureArray: {
    nlohmann::json* node = findNodeByPath(json, target_segments_);
    // An absent or null field has nothing to normalize, and wrapping it would invent a value the
    // client never sent. An array is already the shape the destination wants.
    if (node == nullptr || node->is_null() || node->is_array()) {
      return absl::OkStatus();
    }
    nlohmann::json wrapped = nlohmann::json::array();
    // Moved, not copied, so an `ExternalRef` node survives the wrap without materializing.
    wrapped.push_back(std::move(*node));
    *node = std::move(wrapped);
    return absl::OkStatus();
  }

  case Op::EnsureObject: {
    nlohmann::json* node = findNodeByPath(json, target_segments_);
    if (node == nullptr || node->is_null() || node->is_object()) {
      return absl::OkStatus();
    }
    nlohmann::json wrapped = nlohmann::json::object();
    wrapped[extract_subpath_] = std::move(*node);
    *node = std::move(wrapped);
    return absl::OkStatus();
  }

  case Op::UnwrapSingleKeyObject: {
    nlohmann::json* node = findNodeByPath(json, target_segments_);
    // Anything carrying more than `extract_subpath_` is a structured value in its own right, so
    // collapsing it would discard the other members.
    if (node == nullptr || !node->is_object() || node->size() != 1) {
      return absl::OkStatus();
    }
    auto it = node->find(extract_subpath_);
    if (it == node->end()) {
      return absl::OkStatus();
    }
    nlohmann::json inner = std::move(*it);
    *node = std::move(inner);
    return absl::OkStatus();
  }

  case Op::CoerceNumeric: {
    nlohmann::json* node = findNodeByPath(json, target_segments_);
    // Only a genuine inline string needs converting. Anything else is either already a number or
    // an `ExternalRef` binary node, neither of which should be touched here.
    if (node == nullptr || !node->is_string()) {
      return absl::OkStatus();
    }
    const std::string& text = node->get_ref<const std::string&>();
    if (integral_) {
      int64_t parsed = 0;
      if (!absl::SimpleAtoi(text, &parsed)) {
        return absl::OkStatus();
      }
      *node = parsed;
      return absl::OkStatus();
    }
    double parsed = 0;
    if (!absl::SimpleAtod(text, &parsed)) {
      return absl::OkStatus();
    }
    *node = parsed;
    return absl::OkStatus();
  }

  case Op::ValueMap: {
    nlohmann::json* node = findNodeByPath(json, target_segments_);
    if (node == nullptr || node->is_null()) {
      return absl::OkStatus();
    }
    if (JsonWithExtBuf::isExternalRef(*node)) {
      return absl::InvalidArgumentError(
          absl::StrCat("cannot apply value_map to offloaded ExternalRef at '", target_path_, "'"));
    }
    if (!node->is_string()) {
      return absl::OkStatus();
    }
    const std::string& current_val = node->get_ref<const std::string&>();
    if (auto it = value_mappings_.find(current_val); it != value_mappings_.end()) {
      *node = it->second;
      return absl::OkStatus();
    }
    switch (unknown_policy_) {
    case UnknownValuePolicy::Passthrough:
      return absl::OkStatus();
    case UnknownValuePolicy::Drop:
      extractNodeByPath(json, target_segments_);
      return absl::OkStatus();
    case UnknownValuePolicy::Reject:
      return absl::InvalidArgumentError(
          absl::StrCat("unmapped value '", current_val, "' at field '", target_path_, "'"));
    case UnknownValuePolicy::Fallback:
      *node = default_value_;
      return absl::OkStatus();
    }
    return absl::OkStatus();
  }

  case Op::ForEach: {
    nlohmann::json* arr = findNodeByPath(json, target_segments_);
    if (arr == nullptr || !arr->is_array()) {
      return absl::OkStatus();
    }
    for (nlohmann::json& item : *arr) {
      if (!item.is_object()) {
        continue;
      }
      for (const TranscodeRule& sub_rule : sub_rules_) {
        absl::Status status = sub_rule.apply(item, ctx);
        if (!status.ok()) {
          return status;
        }
      }
    }
    return absl::OkStatus();
  }

  case Op::ExtractFromArray: {
    nlohmann::json* arr = findNodeByPath(json, source_segments_);
    if (arr == nullptr || !arr->is_array()) {
      return absl::OkStatus();
    }
    nlohmann::json remaining = nlohmann::json::array();
    // All matches are collected, not just the first. Keeping only the first would silently drop
    // the second and subsequent system prompts, which changes the model's behavior with no signal
    // to the client.
    nlohmann::json extracted = nlohmann::json::array();
    for (nlohmann::json& elem : *arr) {
      bool matched = false;
      if (elem.is_object()) {
        if (auto it = elem.find(predicate_field_); it != elem.end() && it->is_string()) {
          const std::string& val = it->get_ref<const std::string&>();
          matched =
              std::find(match_values_.begin(), match_values_.end(), val) != match_values_.end();
        }
      }
      if (matched) {
        if (std::optional<nlohmann::json> sub = extractNodeByPath(elem, extract_subpath_segments_);
            sub.has_value()) {
          extracted.push_back(std::move(*sub));
        }
      } else {
        remaining.push_back(std::move(elem));
      }
    }
    *arr = std::move(remaining);
    if (extracted.empty()) {
      return absl::OkStatus();
    }
    if (extracted.size() == 1) {
      // A single match keeps its original scalar shape, which every dialect accepts.
      setNodeByPath(json, target_segments_, std::move(extracted[0]));
      return absl::OkStatus();
    }
    // Multiple matches are concatenated as content blocks so that no prompt is lost and no
    // offloadable `ExternalRef` node has to be stringified.
    nlohmann::json blocks = nlohmann::json::array();
    for (nlohmann::json& value : extracted) {
      for (nlohmann::json& block : toContentBlockArray(std::move(value))) {
        blocks.push_back(std::move(block));
      }
    }
    setNodeByPath(json, target_segments_, std::move(blocks));
    return absl::OkStatus();
  }

  case Op::PrependToArray: {
    std::optional<nlohmann::json> val = extractNodeByPath(json, source_segments_);
    if (!val.has_value() || val->is_null()) {
      return absl::OkStatus();
    }
    nlohmann::json elem = nlohmann::json::object();
    elem[predicate_field_] = match_values_.front();
    setNodeByPath(elem, extract_subpath_segments_, std::move(*val));

    nlohmann::json* arr = findNodeByPath(json, target_segments_);
    if (arr == nullptr || !arr->is_array()) {
      nlohmann::json new_arr = nlohmann::json::array();
      new_arr.push_back(std::move(elem));
      setNodeByPath(json, target_segments_, std::move(new_arr));
    } else {
      arr->insert(arr->begin(), std::move(elem));
    }
    return absl::OkStatus();
  }

  case Op::WrapInArrayObject: {
    std::optional<nlohmann::json> val = extractNodeByPath(json, source_segments_);
    if (!val.has_value() || val->is_null()) {
      return absl::OkStatus();
    }
    nlohmann::json arr = nlohmann::json::array();
    if (val->is_array()) {
      // Content that is already a block array fans out into one wrapper per block. Nesting the
      // whole array under a single key would produce e.g. a Gemini `part.text` holding an array,
      // which the dialect schema rejects.
      static const std::vector<std::string> kTextSegment = {"text"};
      for (nlohmann::json& block : *val) {
        nlohmann::json item = nlohmann::json::object();
        if (block.is_object()) {
          std::optional<nlohmann::json> text = extractNodeByPath(block, kTextSegment);
          if (!text.has_value()) {
            return absl::InvalidArgumentError(
                absl::StrCat("cannot transcode content block in '", source_path_,
                             "' without a 'text' field; multi-modal content is not supported yet"));
          }
          item[extract_subpath_] = std::move(*text);
        } else {
          item[extract_subpath_] = std::move(block);
        }
        arr.push_back(std::move(item));
      }
      setNodeByPath(json, target_segments_, std::move(arr));
      return absl::OkStatus();
    }
    nlohmann::json item = nlohmann::json::object();
    item[extract_subpath_] = std::move(*val);
    arr.push_back(std::move(item));
    setNodeByPath(json, target_segments_, std::move(arr));
    return absl::OkStatus();
  }

  case Op::UnwrapArrayObject: {
    std::optional<nlohmann::json> arr = extractNodeByPath(json, source_segments_);
    if (!arr.has_value() || !arr->is_array() || arr->empty()) {
      return absl::OkStatus();
    }
    // Every element must be unwrapped. Reading only the first one would silently delete the
    // remaining parts, which is how attached images disappear from a multi-modal request. An
    // element that does not carry `extract_subpath_` (an inline image blob, a function call, a
    // thought signature) has no text representation, so it is surfaced as an error rather than
    // dropped on the floor.
    nlohmann::json blocks = nlohmann::json::array();
    for (nlohmann::json& elem : *arr) {
      if (!elem.is_object()) {
        return absl::InvalidArgumentError(
            absl::StrCat("cannot transcode non-object element in '", source_path_, "'"));
      }
      std::optional<nlohmann::json> inner = extractNodeByPath(elem, extract_subpath_segments_);
      if (!inner.has_value()) {
        return absl::InvalidArgumentError(
            absl::StrCat("cannot transcode element in '", source_path_, "' without field '",
                         extract_subpath_, "'; multi-modal content is not supported yet"));
      }
      blocks.push_back(std::move(*inner));
    }
    if (blocks.size() == 1) {
      setNodeByPath(json, target_segments_, std::move(blocks[0]));
      return absl::OkStatus();
    }
    // Multiple parts collapse into the IR's array-of-content-blocks representation.
    nlohmann::json content = nlohmann::json::array();
    for (nlohmann::json& block : blocks) {
      nlohmann::json wrapper = nlohmann::json::object();
      wrapper["type"] = "text";
      wrapper["text"] = std::move(block);
      content.push_back(std::move(wrapper));
    }
    setNodeByPath(json, target_segments_, std::move(content));
    return absl::OkStatus();
  }

  case Op::MergeConsecutiveByKey: {
    nlohmann::json* arr = findNodeByPath(json, target_segments_);
    if (arr == nullptr || !arr->is_array() || arr->size() < 2) {
      return absl::OkStatus();
    }
    nlohmann::json merged = nlohmann::json::array();
    for (nlohmann::json& elem : *arr) {
      if (!merged.empty() && merged.back().is_object() && elem.is_object()) {
        auto prev_key_it = merged.back().find(predicate_field_);
        auto curr_key_it = elem.find(predicate_field_);
        if (prev_key_it != merged.back().end() && curr_key_it != elem.end() &&
            prev_key_it->is_string() && curr_key_it->is_string() && *prev_key_it == *curr_key_it) {
          // `operator[]` would insert a null member for an absent key, which would fabricate a
          // `{"type": "text", "text": null}` block. Only merge when both sides actually carry the
          // field; otherwise leave the elements separate so no content is invented or lost.
          auto prev_content_it = merged.back().find(extract_subpath_);
          auto curr_content_it = elem.find(extract_subpath_);
          if (prev_content_it != merged.back().end() && curr_content_it != elem.end()) {
            nlohmann::json prev_blocks = toContentBlockArray(std::move(*prev_content_it));
            nlohmann::json curr_blocks = toContentBlockArray(std::move(*curr_content_it));
            for (nlohmann::json& b : curr_blocks) {
              prev_blocks.push_back(std::move(b));
            }
            *prev_content_it = std::move(prev_blocks);
            continue;
          }
        }
      }
      merged.push_back(std::move(elem));
    }
    *arr = std::move(merged);
    return absl::OkStatus();
  }

  case Op::SetConst: {
    nlohmann::json value = default_value_;
    setNodeByPath(json, target_segments_, std::move(value));
    return absl::OkStatus();
  }

  case Op::SetFromContext: {
    const nlohmann::json* existing = findNodeByPath(json, target_segments_);
    if (ctx == nullptr || (existing != nullptr && !existing->is_null())) {
      return absl::OkStatus();
    }
    switch (context_field_) {
    case ContextField::RequestModel:
      if (!ctx->request_model.empty()) {
        setNodeByPath(json, target_segments_, std::string(ctx->request_model));
      }
      break;
    case ContextField::NowUnixSeconds:
      if (ctx->now_unix_seconds > 0) {
        setNodeByPath(json, target_segments_, ctx->now_unix_seconds);
      }
      break;
    }
    return absl::OkStatus();
  }

  case Op::Enumerate: {
    nlohmann::json* arr = findNodeByPath(json, target_segments_);
    if (arr == nullptr || !arr->is_array()) {
      return absl::OkStatus();
    }
    for (size_t i = 0; i < arr->size(); ++i) {
      nlohmann::json& elem = (*arr)[i];
      if (!elem.is_object()) {
        continue;
      }
      if (auto it = elem.find(extract_subpath_); it == elem.end() || it->is_null()) {
        elem[extract_subpath_] = static_cast<int64_t>(i);
      }
    }
    return absl::OkStatus();
  }

  case Op::RetainOnly: {
    nlohmann::json* node = findNodeByPath(json, target_segments_);
    if (node == nullptr || !node->is_object()) {
      return absl::OkStatus();
    }
    for (auto it = node->begin(); it != node->end();) {
      if (std::find(match_values_.begin(), match_values_.end(), it.key()) == match_values_.end()) {
        it = node->erase(it);
      } else {
        ++it;
      }
    }
    return absl::OkStatus();
  }

  case Op::TakeFirst: {
    nlohmann::json* arr = findNodeByPath(json, source_segments_);
    if (arr == nullptr || !arr->is_array()) {
      return absl::OkStatus();
    }
    std::optional<nlohmann::json> first;
    if (!arr->empty()) {
      first = std::move(arr->front());
    }
    extractNodeByPath(json, source_segments_);
    if (first.has_value()) {
      setNodeByPath(json, target_segments_, std::move(*first));
    }
    return absl::OkStatus();
  }

  case Op::CollectText: {
    nlohmann::json* arr = findNodeByPath(json, source_segments_);
    if (arr == nullptr || !arr->is_array()) {
      return absl::OkStatus();
    }
    std::vector<nlohmann::json*> texts;
    for (nlohmann::json& elem : *arr) {
      if (!elem.is_object() || !predicate_.matches(elem)) {
        continue;
      }
      nlohmann::json* text = findNodeByPath(elem, extract_subpath_segments_);
      if (text != nullptr && hasShape(*text, JsonShape::Text)) {
        texts.push_back(text);
      }
    }
    nlohmann::json collected;
    if (texts.size() == 1) {
      // Moved, not copied, so an `ExternalRef` stays a reference.
      collected = std::move(*texts.front());
    } else {
      std::string joined;
      for (const nlohmann::json* text : texts) {
        if (!text->is_string()) {
          return absl::UnimplementedError(
              absl::StrCat("cannot join the text in '", source_path_,
                           "' because part of it is held by reference (ExternalRef)"));
        }
        joined.append(text->get_ref<const std::string&>());
      }
      collected = std::move(joined);
    }
    extractNodeByPath(json, source_segments_);
    setNodeByPath(json, target_segments_, std::move(collected));
    return absl::OkStatus();
  }

  case Op::When: {
    if (!predicate_.matches(json)) {
      return absl::OkStatus();
    }
    for (const TranscodeRule& sub_rule : sub_rules_) {
      absl::Status status = sub_rule.apply(json, ctx);
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  case Op::Require: {
    if (predicate_.matches(json)) {
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError(absl::StrCat("expected field '", target_path_, "' to be ",
                                                   shapeDescription(predicate_.shape())));
  }

  case Op::CaptureToState: {
    if (ctx == nullptr || ctx->stream_state == nullptr) {
      return missingStreamState("capture_to_state");
    }
    const nlohmann::json* node = findNodeByPath(json, source_segments_);
    if (node == nullptr || node->is_null()) {
      return absl::OkStatus();
    }
    // A reference points into this event's buffer, which is gone by the time a later event reads
    // the slot.
    if (containsExternalRef(*node)) {
      return absl::InvalidArgumentError(
          absl::StrCat("cannot capture offloaded ExternalRef at '", source_path_, "'"));
    }
    ctx->stream_state->slots.insert_or_assign(slot_, *node);
    return absl::OkStatus();
  }

  case Op::SetFromState: {
    if (ctx == nullptr || ctx->stream_state == nullptr) {
      return missingStreamState("set_from_state");
    }
    const nlohmann::json* existing = findNodeByPath(json, target_segments_);
    if (existing != nullptr && !existing->is_null()) {
      return absl::OkStatus();
    }
    const auto slot = ctx->stream_state->slots.find(slot_);
    if (slot == ctx->stream_state->slots.end()) {
      return absl::OkStatus();
    }
    nlohmann::json value = slot->second;
    setNodeByPath(json, target_segments_, std::move(value));
    return absl::OkStatus();
  }

  case Op::ConvertUsage: {
    TokenUsage usage = takeUsage(json, usage_from_);
    if (!usage.hasAny()) {
      return absl::OkStatus();
    }
    finalizeUsage(usage);
    writeUsage(json, usage, usage_to_);
    return absl::OkStatus();
  }

  case Op::AccumulateUsage: {
    if (ctx == nullptr || ctx->stream_state == nullptr) {
      return missingStreamState("accumulate_usage");
    }
    TokenUsage& total = ctx->stream_state->usage;
    total.merge(takeUsage(json, usage_from_));
    if (usage_to_ == LLMProtocol::Unspecified || !total.hasAny()) {
      return absl::OkStatus();
    }
    // Finalizing is one-shot and the stream is not over, so render a finalized copy.
    TokenUsage snapshot = total;
    finalizeUsage(snapshot);
    writeUsage(json, snapshot, usage_to_);
    return absl::OkStatus();
  }
  }
  return absl::OkStatus();
}

absl::Status TranscodeRuleSet::execute(nlohmann::json& json, TranscodeContext* ctx) const {
  for (const TranscodeRule& rule : rules_) {
    absl::Status status = rule.apply(json, ctx);
    if (!status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status TranscodingEngine::validateRulesAgainstSchema(const TranscodeRuleSet& plan,
                                                           const PayloadSchema* source_schema) {
  if (source_schema == nullptr) {
    return absl::OkStatus();
  }
  const std::vector<std::string> offloadable_paths = source_schema->requestOffloadableFieldPaths();
  absl::flat_hash_set<std::string> offloadable_set(offloadable_paths.begin(),
                                                   offloadable_paths.end());
  return verifyRulesTrackProvenance(plan.rules(), "", offloadable_set);
}

absl::Status TranscodingEngine::registerPack(DialectTranscodePack pack,
                                             const PayloadSchema* dialect_schema,
                                             const PayloadSchema* ir_schema) {
  // Only override the pack's own schema pointers when the caller supplied one. A pack constructed
  // with pre-populated schemas keeps them if `nullptr` is passed here.
  if (dialect_schema != nullptr) {
    pack.dialect_schema = dialect_schema;
  }
  if (ir_schema != nullptr) {
    pack.ir_schema = ir_schema;
  }
  absl::Status to_ir_status = validateRulesAgainstSchema(pack.request.to_ir, pack.dialect_schema);
  if (!to_ir_status.ok()) {
    return to_ir_status;
  }
  absl::Status from_ir_status = validateRulesAgainstSchema(pack.request.from_ir, pack.ir_schema);
  if (!from_ir_status.ok()) {
    return from_ir_status;
  }
  const LLMProtocol protocol = pack.protocol;
  packs_.insert_or_assign(protocol, std::move(pack));
  return absl::OkStatus();
}

absl::StatusOr<TranscodingEngine> TranscodingEngine::createDefault() {
  TranscodingEngine engine;
  const PayloadSchema* ir_schema = AdapterRegistry::get(kIrProtocol).schema();

  for (DialectTranscodePack pack : {createOpenAiChatTranscodePack(), createAnthropicTranscodePack(),
                                    createGeminiTranscodePack()}) {
    const PayloadSchema* dialect_schema = AdapterRegistry::get(pack.protocol).schema();
    absl::Status status = engine.registerPack(std::move(pack), dialect_schema, ir_schema);
    if (!status.ok()) {
      return status;
    }
  }
  return engine;
}

absl::StatusOr<const DialectTranscodePack*> TranscodingEngine::findPack(LLMProtocol dialect) const {
  auto it = packs_.find(dialect);
  if (it == packs_.end()) {
    return absl::InvalidArgumentError(
        absl::StrCat("no transcoding pack registered for ", llmProtocolName(dialect)));
  }
  return &it->second;
}

namespace {

// What a request path names, per a dialect's `PathTemplate`.
struct PathTarget {
  std::string model;
  bool stream{false};
};

// A method as a parsed path names it: without the query a rendered path adds.
absl::string_view methodPath(absl::string_view method) {
  return method.substr(0, method.find('?'));
}

// Parses `path` as one of `envelope`'s model methods: `.../{collection}/{model}{method}`, where
// the collection is the last segment of the envelope's prefix.
//
// TODO(ginama): `readGeminiTarget()` in the request_info AI filter's extractor.cc parses the same
// paths for the same reason. Have it read them through the engine rather than keep its own copy.
std::optional<PathTarget> parseRequestPath(const PathTemplate& envelope, absl::string_view path) {
  path = path.substr(0, path.find('?'));
  const absl::string_view prefix = envelope.prefix;
  const size_t collection = absl::StripSuffix(prefix, "/").rfind('/');
  const size_t last_slash = path.rfind('/');
  if (last_slash == absl::string_view::npos ||
      !absl::EndsWith(path.substr(0, last_slash + 1),
                      collection == absl::string_view::npos ? prefix : prefix.substr(collection))) {
    return std::nullopt;
  }
  // The model is the segment's resource name, up to the colon that starts its custom method.
  const absl::string_view segment = path.substr(last_slash + 1);
  const size_t colon = segment.find(':');
  if (colon == 0 || colon == absl::string_view::npos) {
    return std::nullopt;
  }
  const absl::string_view method = segment.substr(colon);
  const bool stream = method == methodPath(envelope.stream_method);
  if (!stream && method != methodPath(envelope.unary_method)) {
    return std::nullopt;
  }
  return PathTarget{std::string(segment.substr(0, colon)), stream};
}

// The model becomes a path segment, so anything that could escape one is refused.
bool isModelId(absl::string_view model) {
  return !model.empty() && std::all_of(model.begin(), model.end(), [](char c) {
    return absl::ascii_isalnum(c) || c == '-' || c == '.' || c == '_';
  });
}

// Request `ToIr`: writes what `request_path` names into the IR body, where the body does not name
// it itself.
void liftFromRequestPath(const PathTemplate& envelope, absl::string_view request_path,
                         nlohmann::json& json) {
  const std::optional<PathTarget> target = parseRequestPath(envelope, request_path);
  if (!target.has_value() || !json.is_object()) {
    return;
  }
  if (!json.contains(envelope.model_field)) {
    json[envelope.model_field] = target->model;
  }
  if (target->stream && !json.contains(envelope.stream_field)) {
    json[envelope.stream_field] = true;
  }
}

// Request `FromIr`: moves what `envelope` names in the path out of the IR body, and returns the
// path that names it.
absl::StatusOr<std::string> renderRequestPath(const PathTemplate& envelope, LLMProtocol dialect,
                                              nlohmann::json& json) {
  const auto model = json.find(envelope.model_field);
  if (model == json.end() || !model->is_string() ||
      !isModelId(model->get_ref<const std::string&>())) {
    return absl::InvalidArgumentError(absl::StrCat(llmProtocolName(dialect),
                                                   " names the model in the request path, so `",
                                                   envelope.model_field, "` must be a model id"));
  }
  const auto stream = json.find(envelope.stream_field);
  const bool streaming = stream != json.end() && stream->is_boolean() && stream->get<bool>();
  std::string path = absl::StrCat(envelope.prefix, model->get_ref<const std::string&>(),
                                  streaming ? envelope.stream_method : envelope.unary_method);
  json.erase(envelope.model_field);
  json.erase(envelope.stream_field);
  return path;
}

// The IR's `model`, which a request leg reports back through `TranscodeContext::ir_model`.
std::string irModel(const nlohmann::json& json) {
  if (!json.is_object()) {
    return "";
  }
  const auto model = json.find("model");
  return model != json.end() && model->is_string() ? model->get<std::string>() : "";
}

// Runs a request leg. `envelope` is what the dialect names in the request path, or none to convert
// only the body.
absl::Status transcodeRequest(const DialectTranscodePack& pack, TranscodeDirection direction,
                              const std::optional<PathTemplate>& envelope, TranscodeContext& ctx,
                              nlohmann::json& json) {
  const bool is_ir = pack.protocol == TranscodingEngine::kIrProtocol;
  ctx.rewritten_path.reset();
  if (direction == TranscodeDirection::ToIr) {
    // The rules then see what the path names as if the body had named it.
    if (envelope.has_value()) {
      liftFromRequestPath(*envelope, ctx.request_path, json);
    }
    if (!is_ir) {
      absl::Status status = pack.request.to_ir.execute(json, &ctx);
      if (!status.ok()) {
        return status;
      }
    }
    ctx.ir_model = irModel(json);
    return absl::OkStatus();
  }

  ctx.ir_model = irModel(json);
  if (!is_ir) {
    absl::Status status = pack.request.from_ir.execute(json, &ctx);
    if (!status.ok()) {
      return status;
    }
  }
  // Rendered before validation, so what is validated is the body the upstream gets.
  std::optional<std::string> path;
  if (envelope.has_value()) {
    absl::StatusOr<std::string> rendered = renderRequestPath(*envelope, pack.protocol, json);
    if (!rendered.ok()) {
      return rendered.status();
    }
    path = *std::move(rendered);
  }
  if (pack.dialect_schema != nullptr) {
    absl::Status status = pack.dialect_schema->validateRequest(json);
    if (!status.ok()) {
      return status;
    }
  }
  ctx.rewritten_path = std::move(path);
  return absl::OkStatus();
}

absl::Status transcodeResponse(const DialectTranscodePack& pack, TranscodeDirection direction,
                               TranscodeContext& ctx, nlohmann::json& json) {
  if (pack.protocol == TranscodingEngine::kIrProtocol) {
    return absl::OkStatus();
  }
  const TranscodeRuleSet& rules =
      direction == TranscodeDirection::ToIr ? pack.response.to_ir : pack.response.from_ir;
  // Rules rewrite in place, so they run on a copy: a failure part way through then leaves the
  // caller's document intact. `ExternalRef` nodes are small handles, so the copy never touches
  // offloaded bytes.
  nlohmann::json working = json;
  absl::Status status = rules.execute(working, &ctx);
  if (!status.ok()) {
    return status;
  }
  json = std::move(working);
  return absl::OkStatus();
}

// Builds the events a stream grammar writes itself.
absl::StatusOr<std::vector<SseEventPtr>> makeStreamEvents(const std::vector<StreamEmit>& emits) {
  std::vector<SseEventPtr> events;
  events.reserve(emits.size());
  for (const StreamEmit& emit : emits) {
    auto event = std::make_unique<SseEvent>();
    if (emit.json.is_null()) {
      event->set_raw_data(std::make_unique<Buffer::OwnedImpl>(emit.raw_data));
    } else {
      JsonWithExtBuf payload;
      payload.setJson(emit.json);
      event->set_json(std::move(payload));
    }
    if (absl::Status status = event->set_event(emit.event); !status.ok()) {
      return status;
    }
    events.push_back(std::move(event));
  }
  return events;
}

} // namespace

absl::Status TranscodingEngine::transcode(const TranscodeLeg& leg, TranscodeContext& ctx,
                                          nlohmann::json& json) const {
  absl::StatusOr<const DialectTranscodePack*> pack = findPack(leg.dialect);
  if (!pack.ok()) {
    return pack.status();
  }
  switch (leg.kind) {
  case PayloadKind::Request:
    return transcodeRequest(**pack, leg.direction, (*pack)->envelope, ctx, json);
  case PayloadKind::Response:
    return transcodeResponse(**pack, leg.direction, ctx, json);
  case PayloadKind::StreamEvent:
    break;
  }
  return absl::InvalidArgumentError("stream events are transcoded one at a time, as SSE events");
}

absl::StatusOr<const StreamGrammar*>
TranscodingEngine::findStreamGrammar(const TranscodeLeg& leg, const TranscodeContext& ctx) const {
  if (leg.kind != PayloadKind::StreamEvent) {
    return absl::InvalidArgumentError("only a stream event leg transcodes SSE events");
  }
  if (ctx.stream_state == nullptr) {
    return absl::FailedPreconditionError("a stream event leg needs per-stream state");
  }
  absl::StatusOr<const DialectTranscodePack*> pack = findPack(leg.dialect);
  if (!pack.ok()) {
    return pack.status();
  }
  if ((*pack)->protocol == kIrProtocol) {
    return static_cast<const StreamGrammar*>(nullptr);
  }
  const StreamGrammar& grammar =
      leg.direction == TranscodeDirection::ToIr ? (*pack)->stream.to_ir : (*pack)->stream.from_ir;
  return grammar.cases.empty() ? nullptr : &grammar;
}

absl::StatusOr<std::vector<SseEventPtr>>
TranscodingEngine::transcodeStreamEvent(const TranscodeLeg& leg, TranscodeContext& ctx,
                                        SseEventPtr& event) const {
  absl::StatusOr<const StreamGrammar*> grammar = findStreamGrammar(leg, ctx);
  if (!grammar.ok()) {
    return grammar.status();
  }
  if (event == nullptr) {
    return absl::InvalidArgumentError("no SSE event to transcode");
  }
  std::vector<SseEventPtr> out;
  if (*grammar == nullptr) {
    out.push_back(std::move(event));
    return out;
  }

  const std::vector<StreamEventCase>& cases = (*grammar)->cases;
  const auto matched = std::find_if(cases.begin(), cases.end(), [&event](const StreamEventCase& c) {
    return c.match.matches(*event);
  });
  if (matched == cases.end()) {
    return absl::InvalidArgumentError(absl::StrCat("the ", llmProtocolName(leg.dialect),
                                                   " stream grammar has no case for SSE event '",
                                                   event->event(), "'"));
  }

  switch (matched->disposition) {
  case StreamDisposition::Passthrough:
    out.push_back(std::move(event));
    return out;
  case StreamDisposition::Drop:
    event.reset();
    return out;
  case StreamDisposition::Terminate: {
    absl::StatusOr<std::vector<SseEventPtr>> emitted = makeStreamEvents((*grammar)->on_terminate);
    if (!emitted.ok()) {
      return emitted.status();
    }
    ctx.stream_state->terminated = true;
    event.reset();
    return emitted;
  }
  case StreamDisposition::Transcode:
    break;
  }

  if (!event->is_json()) {
    return absl::InvalidArgumentError("only an SSE event with a JSON payload can be transcoded");
  }
  // Rules rewrite in place and may write the stream state, so they run on copies of both: a
  // failure part way through then leaves the event and the stream as they were. `ExternalRef`
  // nodes are small handles into the event's own payload store, so the copy never touches
  // offloaded bytes.
  nlohmann::json working = event->json().json();
  TranscodeStreamState saved_state = *ctx.stream_state;
  absl::Status status = matched->rules.execute(working, &ctx);
  if (status.ok()) {
    status = event->set_event(matched->output_event);
  }
  if (!status.ok()) {
    *ctx.stream_state = std::move(saved_state);
    return status;
  }
  event->json().json() = std::move(working);
  out.push_back(std::move(event));
  return out;
}

absl::StatusOr<std::vector<SseEventPtr>>
TranscodingEngine::finishStream(const TranscodeLeg& leg, TranscodeContext& ctx) const {
  absl::StatusOr<const StreamGrammar*> grammar = findStreamGrammar(leg, ctx);
  if (!grammar.ok()) {
    return grammar.status();
  }
  if (*grammar == nullptr || ctx.stream_state->terminated) {
    return std::vector<SseEventPtr>();
  }
  absl::StatusOr<std::vector<SseEventPtr>> emitted = makeStreamEvents((*grammar)->on_source_end);
  if (!emitted.ok()) {
    return emitted.status();
  }
  ctx.stream_state->terminated = true;
  return emitted;
}

std::string TranscodingEngine::modelFromRequestPath(LLMProtocol dialect,
                                                    absl::string_view path) const {
  const auto pack = packs_.find(dialect);
  if (pack == packs_.end() || !pack->second.envelope.has_value()) {
    return "";
  }
  std::optional<PathTarget> target = parseRequestPath(*pack->second.envelope, path);
  return target.has_value() ? std::move(target->model) : "";
}

// TODO(ginama): Address the IR data-loss problem where dialect-specific fields not modeled by
// `OpenAiChatCompletions` are dropped when converting to the IR.
absl::Status TranscodingEngine::transcodeToIr(LLMProtocol source_protocol,
                                              nlohmann::json& json) const {
  if (source_protocol == LLMProtocol::Unspecified) {
    return absl::OkStatus();
  }
  absl::StatusOr<const DialectTranscodePack*> pack = findPack(source_protocol);
  if (!pack.ok()) {
    return pack.status();
  }
  TranscodeContext ctx;
  return transcodeRequest(**pack, TranscodeDirection::ToIr, /*envelope=*/std::nullopt, ctx, json);
}

absl::Status TranscodingEngine::transcodeFromIr(LLMProtocol target_protocol,
                                                nlohmann::json& json) const {
  if (target_protocol == LLMProtocol::Unspecified) {
    return absl::OkStatus();
  }
  absl::StatusOr<const DialectTranscodePack*> pack = findPack(target_protocol);
  if (!pack.ok()) {
    return pack.status();
  }
  TranscodeContext ctx;
  return transcodeRequest(**pack, TranscodeDirection::FromIr, /*envelope=*/std::nullopt, ctx, json);
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
