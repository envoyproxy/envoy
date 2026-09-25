#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "source/extensions/filters/http/ai_protocol_manager/llm_protocol_adapter.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
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
    case TranscodeRule::Op::SetDefault:
    case TranscodeRule::Op::CoerceNumeric:
      break;
    }
  }
  return absl::OkStatus();
}

// Anthropic requires `max_tokens`, but it is optional for every other dialect. When a client omits
// it entirely the engine has to synthesize a value or the upstream rejects the request outright.
// TODO(ginama): make this configurable per route rather than a compiled-in default.
constexpr int kDefaultAnthropicMaxTokens = 4096;

// Builds the declarative transcoding pack for Anthropic Messages <-> IR (OpenAI Chat).
DialectTranscodePack createAnthropicTranscodePack() {
  return DialectTranscodePack{
      /*protocol=*/LLMProtocol::AnthropicMessages,
      /*to_IR=*/
      TranscodeRuleSet(
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
              TranscodeRule::valueMap("tool_choice.type",
                                      {{"any", "required"}, {"tool", "function"}}),
              TranscodeRule::move("tool_choice.name", "tool_choice.function.name"),
              TranscodeRule::unwrapSingleKeyObject("tool_choice", "type"),
          }),
      /*from_IR=*/
      TranscodeRuleSet(
          TranscodingEngine::kIrProtocol, LLMProtocol::AnthropicMessages,
          {
              // 1. Extract `system` / `developer` messages from `messages[]` into top-level
              // `system`
              TranscodeRule::extractFromArray("messages", "role", {"system", "developer"},
                                              "content", "system"),
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
              TranscodeRule::valueMap("tool_choice.type",
                                      {{"required", "any"}, {"function", "tool"}}),
              TranscodeRule::move("tool_choice.function.name", "tool_choice.name"),
              TranscodeRule::drop("tool_choice.function"),
          }),
  };
}

// Builds the declarative transcoding pack for Gemini GenerateContent <-> IR (OpenAI Chat).
DialectTranscodePack createGeminiTranscodePack() {
  return DialectTranscodePack{
      /*protocol=*/LLMProtocol::GeminiGenerateContent,
      /*to_IR=*/
      TranscodeRuleSet(
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
              TranscodeRule::firstOf(
                  {"generationConfig.temperature", "generation_config.temperature"}, "temperature"),
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
          }),
      /*from_IR=*/
      TranscodeRuleSet(
          TranscodingEngine::kIrProtocol, LLMProtocol::GeminiGenerateContent,
          {
              // 1. Extract `system` / `developer` messages from `messages[]` and wrap into
              //    `systemInstruction.parts[{text: ...}]`
              TranscodeRule::extractFromArray("messages", "role", {"system", "developer"},
                                              "content", "systemInstruction.content"),
              TranscodeRule::wrapInArrayObject("systemInstruction.content",
                                               "systemInstruction.parts", "text"),
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
              // 5. Keep `model` and `stream` in the JSON body. Gemini encodes these in the URL
              //    path (`/v1beta/models/{model}:generateContent` or `:streamGenerateContent`),
              //    and the transcoding filter moves them into `:path` (Gemini's schema allows
              //    unknown root fields).
          }),
  };
}

// Builds the identity/normalization pack for OpenAI Chat Completions (the IR protocol).
DialectTranscodePack createOpenAiChatTranscodePack() {
  return DialectTranscodePack{
      /*protocol=*/LLMProtocol::OpenAiChatCompletions,
      /*to_IR=*/
      TranscodeRuleSet(LLMProtocol::OpenAiChatCompletions, TranscodingEngine::kIrProtocol, {}),
      /*from_IR=*/
      TranscodeRuleSet(TranscodingEngine::kIrProtocol, LLMProtocol::OpenAiChatCompletions, {}),
  };
}

} // namespace

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

TranscodeRule TranscodeRule::forEach(std::string array_path,
                                     std::initializer_list<TranscodeRule> rules) {
  TranscodeRule rule(Op::ForEach);
  rule.target_segments_ = splitPath(array_path);
  rule.target_path_ = std::move(array_path);
  rule.sub_rules_.assign(rules.begin(), rules.end());
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

absl::Status TranscodeRule::apply(nlohmann::json& json) const {
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
        absl::Status status = sub_rule.apply(item);
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
  }
  return absl::OkStatus();
}

absl::Status TranscodeRuleSet::execute(nlohmann::json& json) const {
  for (const TranscodeRule& rule : rules_) {
    absl::Status status = rule.apply(json);
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
  absl::Status to_ir_status = validateRulesAgainstSchema(pack.to_ir, pack.dialect_schema);
  if (!to_ir_status.ok()) {
    return to_ir_status;
  }
  absl::Status from_ir_status = validateRulesAgainstSchema(pack.from_ir, pack.ir_schema);
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

// TODO(ginama): Address the IR data-loss problem where dialect-specific fields not modeled by
// `OpenAiChatCompletions` are dropped when converting to the IR.
absl::Status TranscodingEngine::transcodeToIr(LLMProtocol source_protocol,
                                              nlohmann::json& json) const {
  if (source_protocol == LLMProtocol::Unspecified) {
    return absl::OkStatus();
  }
  auto it = packs_.find(source_protocol);
  if (it == packs_.end()) {
    return absl::InvalidArgumentError(absl::StrCat("no transcoding pack registered for source ",
                                                   llmProtocolName(source_protocol)));
  }
  if (source_protocol == kIrProtocol) {
    return absl::OkStatus();
  }
  return it->second.to_ir.execute(json);
}

absl::Status TranscodingEngine::transcodeFromIr(LLMProtocol target_protocol,
                                                nlohmann::json& json) const {
  if (target_protocol == LLMProtocol::Unspecified) {
    return absl::OkStatus();
  }
  auto it = packs_.find(target_protocol);
  if (it == packs_.end()) {
    return absl::InvalidArgumentError(absl::StrCat("no transcoding pack registered for target ",
                                                   llmProtocolName(target_protocol)));
  }
  if (target_protocol != kIrProtocol) {
    absl::Status status = it->second.from_ir.execute(json);
    if (!status.ok()) {
      return status;
    }
  }
  if (it->second.dialect_schema != nullptr) {
    return it->second.dialect_schema->validateRequest(json);
  }
  return absl::OkStatus();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
