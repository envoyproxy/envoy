#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"

#include <algorithm>

#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_adapter.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

// Splits a dot-delimited JSON path (e.g. "generationConfig.maxOutputTokens") into segments.
std::vector<absl::string_view> splitPath(absl::string_view path) {
  if (path.empty()) {
    return {};
  }
  return absl::StrSplit(path, '.');
}

// Extracts and removes the node at `path` from `root`, returning `std::nullopt` if any
// segment is absent or not an object. Cleans up empty parent objects created along `path`.
std::optional<nlohmann::json> extractNodeByPath(nlohmann::json& root, absl::string_view path) {
  const std::vector<absl::string_view> parts = splitPath(path);
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
    const absl::string_view child_key = parts[i - 1];
    auto child_it = parent->find(child_key);
    if (child_it != parent->end() && child_it->is_object() && child_it->empty()) {
      parent->erase(child_it);
    } else {
      break;
    }
  }

  return extracted;
}

// Navigates to `path` inside `root` (read/write), returning `nullptr` if absent.
nlohmann::json* findNodeByPath(nlohmann::json& root, absl::string_view path) {
  const std::vector<absl::string_view> parts = splitPath(path);
  if (parts.empty()) {
    return &root;
  }
  nlohmann::json* curr = &root;
  for (const absl::string_view part : parts) {
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

// Writes `value` into `root` at `path`, creating intermediate objects as needed.
void setNodeByPath(nlohmann::json& root, absl::string_view path, nlohmann::json&& value) {
  const std::vector<absl::string_view> parts = splitPath(path);
  if (parts.empty()) {
    root = std::move(value);
    return;
  }
  if (!root.is_object()) {
    root = nlohmann::json::object();
  }
  nlohmann::json* curr = &root;
  for (size_t i = 0; i + 1 < parts.size(); ++i) {
    const std::string key(parts[i]);
    auto it = curr->find(key);
    if (it == curr->end() || !it->is_object()) {
      (*curr)[key] = nlohmann::json::object();
    }
    curr = &((*curr)[key]);
  }
  (*curr)[std::string(parts.back())] = std::move(value);
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

// Recursively collects the field paths targeted by `ValueMap` rules, for static verification
// against `PayloadSchema`. Descends into `ForEach` sub-rules to build `messages[].role` style
// paths, matching the format `requestOffloadableFieldPaths()` emits.
void collectValueMapPaths(const std::vector<TranscodeRule>& rules, absl::string_view prefix,
                          std::vector<std::string>& out_paths) {
  for (const TranscodeRule& rule : rules) {
    if (rule.op() == TranscodeRule::Op::ValueMap) {
      const std::string full_path =
          prefix.empty() ? rule.targetPath() : absl::StrCat(prefix, ".", rule.targetPath());
      out_paths.push_back(full_path);
    } else if (rule.op() == TranscodeRule::Op::ForEach) {
      const std::string array_prefix = prefix.empty()
                                           ? absl::StrCat(rule.targetPath(), "[]")
                                           : absl::StrCat(prefix, ".", rule.targetPath(), "[]");
      collectValueMapPaths(rule.subRules(), array_prefix, out_paths);
    }
  }
}

// Anthropic requires `max_tokens`, but it is optional for every other dialect. When a client omits
// it entirely the engine has to synthesize a value or the upstream rejects the request outright.
// TODO(ginama): make this configurable per route rather than a compiled-in default.
constexpr int kDefaultAnthropicMaxTokens = 4096;

// Builds the declarative transcoding pack for Anthropic Messages <-> Hub (OpenAI Chat).
DialectTranscodePack createAnthropicTranscodePack() {
  return DialectTranscodePack{
      /*protocol=*/ApiProtocol::AnthropicMessages,
      /*inbound=*/
      TranscodeRuleSet(
          ApiProtocol::AnthropicMessages, TranscodingEngine::kHubProtocol,
          {
              // 1. Prepend top-level `system` prompt into `messages[]` as `{role: "system", ...}`
              TranscodeRule::prependToArray("system", "messages", "role", "system", "content"),
              // 2. Map Anthropic `max_tokens` and `stop_sequences` to Hub (OpenAI Chat) names
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
          }),
      /*outbound=*/
      TranscodeRuleSet(
          TranscodingEngine::kHubProtocol, ApiProtocol::AnthropicMessages,
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
              // 4. Map `stop` -> `stop_sequences`
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
          }),
      /*model_prefixes=*/{"claude-"},
  };
}

// Builds the declarative transcoding pack for Gemini GenerateContent <-> Hub (OpenAI Chat).
DialectTranscodePack createGeminiTranscodePack() {
  return DialectTranscodePack{
      /*protocol=*/ApiProtocol::GeminiGenerateContent,
      /*inbound=*/
      TranscodeRuleSet(
          ApiProtocol::GeminiGenerateContent, TranscodingEngine::kHubProtocol,
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
                      TranscodeRule::valueMap("role", {{"model", "assistant"}}),
                      TranscodeRule::unwrapArrayObject("parts", "text", "content"),
                  }),
              TranscodeRule::prependToArray("system", "messages", "role", "system", "content"),
              // 3. Hoist `generationConfig` / `generation_config` parameters to top-level Hub
              // fields
              TranscodeRule::firstOf(
                  {"generationConfig.maxOutputTokens", "generationConfig.max_output_tokens",
                   "generation_config.maxOutputTokens", "generation_config.max_output_tokens"},
                  "max_completion_tokens"),
              TranscodeRule::firstOf(
                  {"generationConfig.temperature", "generation_config.temperature"}, "temperature"),
              TranscodeRule::firstOf({"generationConfig.topP", "generationConfig.top_p",
                                      "generation_config.topP", "generation_config.top_p"},
                                     "top_p"),
              TranscodeRule::firstOf(
                  {"generationConfig.stopSequences", "generationConfig.stop_sequences",
                   "generation_config.stopSequences", "generation_config.stop_sequences"},
                  "stop"),
              TranscodeRule::drop("generationConfig"),
              TranscodeRule::drop("generation_config"),
          }),
      /*outbound=*/
      TranscodeRuleSet(
          TranscodingEngine::kHubProtocol, ApiProtocol::GeminiGenerateContent,
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
              TranscodeRule::move("stop", "generationConfig.stopSequences"),
              // 4. `model` and `stream` belong in the Gemini URL (`/v1beta/models/{model}:
              //    generateContent` vs `:streamGenerateContent`), not the body. They are
              //    deliberately left in the payload rather than dropped: dropping them destroys
              //    the only copy of the routing information, and Gemini's root schema sets
              //    `allowUnknownFields(true)` so the extra fields still validate.
              //    TODO(ginama): relocate these into the `:path` header once
              //    `AiFilterContext::request_headers` is non-const and the transcoder filter can
              //    rewrite the request line.
          }),
      /*model_prefixes=*/{"gemini-"},
  };
}

// Builds the identity/normalization pack for OpenAI Chat Completions (the Hub protocol).
DialectTranscodePack createOpenAiChatTranscodePack() {
  return DialectTranscodePack{
      /*protocol=*/ApiProtocol::OpenAiChatCompletions,
      /*inbound=*/
      TranscodeRuleSet(ApiProtocol::OpenAiChatCompletions, TranscodingEngine::kHubProtocol, {}),
      /*outbound=*/
      TranscodeRuleSet(TranscodingEngine::kHubProtocol, ApiProtocol::OpenAiChatCompletions, {}),
      /*model_prefixes=*/{"gpt-", "o1-", "o3-", "o4-"},
  };
}

} // namespace

TranscodeRule TranscodeRule::move(std::string from_path, std::string to_path) {
  TranscodeRule rule(Op::Move);
  rule.source_path_ = std::move(from_path);
  rule.target_path_ = std::move(to_path);
  return rule;
}

TranscodeRule TranscodeRule::firstOf(std::initializer_list<std::string> from_paths,
                                     std::string to_path) {
  TranscodeRule rule(Op::FirstOf);
  rule.source_paths_.assign(from_paths.begin(), from_paths.end());
  rule.target_path_ = std::move(to_path);
  return rule;
}

TranscodeRule TranscodeRule::drop(std::string path) {
  TranscodeRule rule(Op::Drop);
  rule.source_path_ = std::move(path);
  return rule;
}

TranscodeRule TranscodeRule::setDefault(std::string path, nlohmann::json default_value) {
  TranscodeRule rule(Op::SetDefault);
  rule.target_path_ = std::move(path);
  rule.default_value_ = std::move(default_value);
  return rule;
}

TranscodeRule TranscodeRule::valueMap(std::string path,
                                      std::initializer_list<ValueMapping> mappings,
                                      UnknownValuePolicy unknown_policy) {
  TranscodeRule rule(Op::ValueMap);
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
  rule.target_path_ = std::move(array_path);
  rule.sub_rules_.assign(rules.begin(), rules.end());
  return rule;
}

TranscodeRule TranscodeRule::extractFromArray(std::string array_path, std::string predicate_field,
                                              std::initializer_list<std::string> match_values,
                                              std::string extract_subpath,
                                              std::string target_path) {
  TranscodeRule rule(Op::ExtractFromArray);
  rule.source_path_ = std::move(array_path);
  rule.predicate_field_ = std::move(predicate_field);
  rule.match_values_.assign(match_values.begin(), match_values.end());
  rule.extract_subpath_ = std::move(extract_subpath);
  rule.target_path_ = std::move(target_path);
  return rule;
}

TranscodeRule TranscodeRule::prependToArray(std::string source_path, std::string array_path,
                                            std::string key_field, std::string key_value,
                                            std::string value_subpath) {
  TranscodeRule rule(Op::PrependToArray);
  rule.source_path_ = std::move(source_path);
  rule.target_path_ = std::move(array_path);
  rule.predicate_field_ = std::move(key_field);
  rule.match_values_ = {std::move(key_value)};
  rule.extract_subpath_ = std::move(value_subpath);
  return rule;
}

TranscodeRule TranscodeRule::wrapInArrayObject(std::string from_path, std::string to_array_path,
                                               std::string element_key) {
  TranscodeRule rule(Op::WrapInArrayObject);
  rule.source_path_ = std::move(from_path);
  rule.target_path_ = std::move(to_array_path);
  rule.extract_subpath_ = std::move(element_key);
  return rule;
}

TranscodeRule TranscodeRule::unwrapArrayObject(std::string from_array_path, std::string element_key,
                                               std::string to_path) {
  TranscodeRule rule(Op::UnwrapArrayObject);
  rule.source_path_ = std::move(from_array_path);
  rule.extract_subpath_ = std::move(element_key);
  rule.target_path_ = std::move(to_path);
  return rule;
}

TranscodeRule TranscodeRule::mergeConsecutiveByKey(std::string array_path, std::string key_field,
                                                   std::string merge_field) {
  TranscodeRule rule(Op::MergeConsecutiveByKey);
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
    if (std::optional<nlohmann::json> val = extractNodeByPath(json, source_path_);
        val.has_value()) {
      setNodeByPath(json, target_path_, std::move(*val));
    }
    return absl::OkStatus();
  }

  case Op::FirstOf: {
    std::optional<nlohmann::json> chosen;
    for (const std::string& candidate : source_paths_) {
      std::optional<nlohmann::json> extracted = extractNodeByPath(json, candidate);
      if (!chosen.has_value() && extracted.has_value() && !extracted->is_null()) {
        chosen = std::move(extracted);
      }
    }
    if (chosen.has_value()) {
      setNodeByPath(json, target_path_, std::move(*chosen));
    }
    return absl::OkStatus();
  }

  case Op::Drop: {
    extractNodeByPath(json, source_path_);
    return absl::OkStatus();
  }

  case Op::SetDefault: {
    const nlohmann::json* existing = findNodeByPath(json, target_path_);
    if (existing == nullptr || existing->is_null()) {
      nlohmann::json copy = default_value_;
      setNodeByPath(json, target_path_, std::move(copy));
    }
    return absl::OkStatus();
  }

  case Op::ValueMap: {
    nlohmann::json* node = findNodeByPath(json, target_path_);
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
      extractNodeByPath(json, target_path_);
      return absl::OkStatus();
    case UnknownValuePolicy::Reject:
      return absl::InvalidArgumentError(
          absl::StrCat("unmapped value '", current_val, "' at field '", target_path_, "'"));
    }
    return absl::OkStatus();
  }

  case Op::ForEach: {
    nlohmann::json* arr = findNodeByPath(json, target_path_);
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
    nlohmann::json* arr = findNodeByPath(json, source_path_);
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
        if (std::optional<nlohmann::json> sub = extractNodeByPath(elem, extract_subpath_);
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
      setNodeByPath(json, target_path_, std::move(extracted[0]));
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
    setNodeByPath(json, target_path_, std::move(blocks));
    return absl::OkStatus();
  }

  case Op::PrependToArray: {
    std::optional<nlohmann::json> val = extractNodeByPath(json, source_path_);
    if (!val.has_value() || val->is_null()) {
      return absl::OkStatus();
    }
    nlohmann::json elem = nlohmann::json::object();
    elem[predicate_field_] = match_values_.front();
    setNodeByPath(elem, extract_subpath_, std::move(*val));

    nlohmann::json* arr = findNodeByPath(json, target_path_);
    if (arr == nullptr || !arr->is_array()) {
      nlohmann::json new_arr = nlohmann::json::array();
      new_arr.push_back(std::move(elem));
      setNodeByPath(json, target_path_, std::move(new_arr));
    } else {
      arr->insert(arr->begin(), std::move(elem));
    }
    return absl::OkStatus();
  }

  case Op::WrapInArrayObject: {
    std::optional<nlohmann::json> val = extractNodeByPath(json, source_path_);
    if (!val.has_value() || val->is_null()) {
      return absl::OkStatus();
    }
    nlohmann::json arr = nlohmann::json::array();
    if (val->is_array()) {
      // Content that is already a block array fans out into one wrapper per block. Nesting the
      // whole array under a single key would produce e.g. a Gemini `part.text` holding an array,
      // which the dialect schema rejects.
      for (nlohmann::json& block : *val) {
        nlohmann::json item = nlohmann::json::object();
        if (block.is_object()) {
          std::optional<nlohmann::json> text = extractNodeByPath(block, "text");
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
      setNodeByPath(json, target_path_, std::move(arr));
      return absl::OkStatus();
    }
    nlohmann::json item = nlohmann::json::object();
    item[extract_subpath_] = std::move(*val);
    arr.push_back(std::move(item));
    setNodeByPath(json, target_path_, std::move(arr));
    return absl::OkStatus();
  }

  case Op::UnwrapArrayObject: {
    std::optional<nlohmann::json> arr = extractNodeByPath(json, source_path_);
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
      std::optional<nlohmann::json> inner = extractNodeByPath(elem, extract_subpath_);
      if (!inner.has_value()) {
        return absl::InvalidArgumentError(
            absl::StrCat("cannot transcode element in '", source_path_, "' without field '",
                         extract_subpath_, "'; multi-modal content is not supported yet"));
      }
      blocks.push_back(std::move(*inner));
    }
    if (blocks.size() == 1) {
      setNodeByPath(json, target_path_, std::move(blocks[0]));
      return absl::OkStatus();
    }
    // Multiple parts collapse into the hub's array-of-content-blocks representation.
    nlohmann::json content = nlohmann::json::array();
    for (nlohmann::json& block : blocks) {
      nlohmann::json wrapper = nlohmann::json::object();
      wrapper["type"] = "text";
      wrapper["text"] = std::move(block);
      content.push_back(std::move(wrapper));
    }
    setNodeByPath(json, target_path_, std::move(content));
    return absl::OkStatus();
  }

  case Op::MergeConsecutiveByKey: {
    nlohmann::json* arr = findNodeByPath(json, target_path_);
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
  const absl::flat_hash_set<std::string> offloadable_set(offloadable_paths.begin(),
                                                         offloadable_paths.end());

  std::vector<std::string> value_map_paths;
  collectValueMapPaths(plan.rules(), "", value_map_paths);
  for (const std::string& path : value_map_paths) {
    if (offloadable_set.contains(path)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "transcoding verifier error: value_map rule cannot target offloadable field '", path,
          "' because large values are represented as ExternalRef nodes"));
    }
  }
  return absl::OkStatus();
}

absl::Status TranscodingEngine::registerPack(DialectTranscodePack pack,
                                             const PayloadSchema* dialect_schema,
                                             const PayloadSchema* hub_schema) {
  absl::Status inbound_status = validateRulesAgainstSchema(pack.inbound, dialect_schema);
  if (!inbound_status.ok()) {
    return inbound_status;
  }
  absl::Status outbound_status = validateRulesAgainstSchema(pack.outbound, hub_schema);
  if (!outbound_status.ok()) {
    return outbound_status;
  }
  // Only override the pack's own schema pointers when the caller supplied one. A pack constructed
  // with pre-populated schemas keeps them if `nullptr` is passed here.
  if (dialect_schema != nullptr) {
    pack.dialect_schema = dialect_schema;
  }
  if (hub_schema != nullptr) {
    pack.hub_schema = hub_schema;
  }
  for (const std::string& prefix : pack.model_prefixes) {
    model_prefixes_.push_back({prefix, pack.protocol});
  }
  const ApiProtocol protocol = pack.protocol;
  packs_.insert_or_assign(protocol, std::move(pack));
  return absl::OkStatus();
}

absl::StatusOr<TranscodingEngine> TranscodingEngine::createDefault() {
  TranscodingEngine engine;
  const PayloadSchema* hub_schema = AdapterRegistry::get(kHubProtocol).schema();

  for (DialectTranscodePack pack : {createOpenAiChatTranscodePack(), createAnthropicTranscodePack(),
                                    createGeminiTranscodePack()}) {
    const PayloadSchema* dialect_schema = AdapterRegistry::get(pack.protocol).schema();
    absl::Status status = engine.registerPack(std::move(pack), dialect_schema, hub_schema);
    if (!status.ok()) {
      return status;
    }
  }
  return engine;
}

ApiProtocol TranscodingEngine::resolveTargetProtocol(absl::string_view model) const {
  for (const ModelPrefixEntry& entry : model_prefixes_) {
    if (absl::StartsWith(model, entry.prefix)) {
      return entry.protocol;
    }
  }
  return ApiProtocol::Unspecified;
}

absl::Status TranscodingEngine::transcodeInbound(ApiProtocol source_protocol,
                                                 nlohmann::json& json) const {
  if (source_protocol == ApiProtocol::Unspecified) {
    return absl::OkStatus();
  }
  auto it = packs_.find(source_protocol);
  if (it == packs_.end()) {
    return absl::InvalidArgumentError(absl::StrCat("no transcoding pack registered for source ",
                                                   apiProtocolName(source_protocol)));
  }
  if (source_protocol == kHubProtocol) {
    return absl::OkStatus();
  }
  return it->second.inbound.execute(json);
}

absl::Status TranscodingEngine::transcodeOutbound(ApiProtocol target_protocol,
                                                  nlohmann::json& json) const {
  if (target_protocol == ApiProtocol::Unspecified) {
    return absl::OkStatus();
  }
  auto it = packs_.find(target_protocol);
  if (it == packs_.end()) {
    return absl::InvalidArgumentError(absl::StrCat("no transcoding pack registered for target ",
                                                   apiProtocolName(target_protocol)));
  }
  if (target_protocol != kHubProtocol) {
    absl::Status status = it->second.outbound.execute(json);
    if (!status.ok()) {
      return status;
    }
  }
  if (it->second.dialect_schema != nullptr) {
    return it->second.dialect_schema->validateRequest(json);
  }
  return absl::OkStatus();
}

absl::Status TranscodingEngine::transcode(ApiProtocol source_protocol, ApiProtocol target_protocol,
                                          nlohmann::json& json) const {
  // Both legs always run, even when `source_protocol == target_protocol`. The inbound leg is a
  // no-op for a same-protocol pair, but the outbound leg still validates the payload against the
  // target dialect schema, which is the check that protects the upstream cluster.
  absl::Status status = transcodeInbound(source_protocol, json);
  if (!status.ok()) {
    return status;
  }
  return transcodeOutbound(target_protocol, json);
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
