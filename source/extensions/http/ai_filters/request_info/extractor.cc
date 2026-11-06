#include "source/extensions/http/ai_filters/request_info/extractor.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

#include "source/extensions/filters/http/ai_protocol_manager/json_readers.h"
#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace RequestInfo {

using HttpFilters::AiProtocolManager::JsonWithExtBuf;
using HttpFilters::AiProtocolManager::LLMProtocol;
using HttpFilters::AiProtocolManager::MaxSafeCount;
using HttpFilters::AiProtocolManager::MaxStringValueSize;
using HttpFilters::AiProtocolManager::NullPolicy;
using HttpFilters::AiProtocolManager::readArrayLength;
using HttpFilters::AiProtocolManager::readBool;
using HttpFilters::AiProtocolManager::readCount;
using HttpFilters::AiProtocolManager::readObject;
using HttpFilters::AiProtocolManager::readString;

namespace Keys = HttpFilters::AiProtocolManager::Keys;

namespace {

// These APIs document null as "unset", so a null limit is absent, not malformed.
std::optional<uint64_t> readLimit(const nlohmann::json& json, absl::string_view key,
                                  bool& malformed) {
  return readCount(json, key, malformed, NullPolicy::AllowNullAsAbsent);
}

void readCommon(const nlohmann::json& json, RequestAttributes& attrs) {
  if (auto model = readString(json, Keys::Model, attrs.malformed); model.has_value()) {
    attrs.model = std::move(model).value();
  }
  attrs.stream = readBool(json, Keys::Stream, attrs.malformed);
}

void readOpenAiChatCompletions(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::MaxCompletionTokens, attrs.malformed);
  if (!attrs.max_output_tokens.has_value()) {
    attrs.max_output_tokens = readLimit(json, Keys::MaxTokens, attrs.malformed);
  }
  attrs.message_count = readArrayLength(json, Keys::Messages, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
}

void readOpenAiResponses(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::MaxOutputTokens, attrs.malformed);
  // A string `input`, inline or offloaded, is the shorthand for one user message.
  if (const auto input = json.find(Keys::Input);
      input != json.end() && (input->is_string() || JsonWithExtBuf::isExternalRef(*input))) {
    attrs.message_count = 1;
  } else {
    attrs.message_count = readArrayLength(json, Keys::Input, attrs.malformed);
  }
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
}

void readAnthropicMessages(const nlohmann::json& json, RequestAttributes& attrs) {
  readCommon(json, attrs);
  attrs.max_output_tokens = readLimit(json, Keys::MaxTokens, attrs.malformed);
  attrs.message_count = readArrayLength(json, Keys::Messages, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
}

// `.../models/{model}:generateContent` or `:streamGenerateContent`, which Vertex nests too.
void readGeminiTarget(absl::string_view path, RequestAttributes& attrs) {
  path = path.substr(0, path.find('?'));
  const size_t last_slash = path.rfind('/');
  if (last_slash == absl::string_view::npos ||
      !absl::EndsWith(path.substr(0, last_slash), "/models")) {
    return;
  }
  const absl::string_view segment = path.substr(last_slash + 1);
  const size_t colon = segment.find(':');
  if (colon == absl::string_view::npos) {
    return;
  }
  const absl::string_view model = segment.substr(0, colon);
  const absl::string_view operation = segment.substr(colon + 1);
  if (model.empty()) {
    return;
  }
  if (operation == "generateContent") {
    attrs.stream = false;
  } else if (operation == "streamGenerateContent") {
    attrs.stream = true;
  } else {
    return;
  }
  if (model.size() > MaxStringValueSize) {
    attrs.malformed = true;
    return;
  }
  attrs.model = std::string(model);
}

void readGeminiGenerateContent(const nlohmann::json& json, absl::string_view path,
                               RequestAttributes& attrs) {
  readGeminiTarget(path, attrs);
  // Gemini's proto3 JSON accepts snake_case field names alongside camelCase.
  const nlohmann::json* config =
      readObject(json, Keys::GenerationConfig, attrs.malformed, NullPolicy::AllowNullAsAbsent);
  if (config == nullptr) {
    config = readObject(json, Keys::GenerationConfigSnake, attrs.malformed,
                        NullPolicy::AllowNullAsAbsent);
  }
  if (config != nullptr) {
    attrs.max_output_tokens = readLimit(*config, Keys::MaxOutputTokensCamel, attrs.malformed);
    if (!attrs.max_output_tokens.has_value()) {
      attrs.max_output_tokens = readLimit(*config, Keys::MaxOutputTokens, attrs.malformed);
    }
  }
  attrs.message_count = readArrayLength(json, Keys::Contents, attrs.malformed);
  attrs.tool_count = readArrayLength(json, Keys::Tools, attrs.malformed);
}

} // namespace

RequestAttributes extractRequestAttributes(LLMProtocol protocol, const nlohmann::json& json,
                                           absl::string_view path) {
  RequestAttributes attrs;
  attrs.llm_protocol = protocol;
  switch (protocol) {
  case LLMProtocol::OpenAiChatCompletions:
    readOpenAiChatCompletions(json, attrs);
    break;
  case LLMProtocol::OpenAiResponses:
    readOpenAiResponses(json, attrs);
    break;
  case LLMProtocol::AnthropicMessages:
    readAnthropicMessages(json, attrs);
    break;
  case LLMProtocol::GeminiGenerateContent:
    readGeminiGenerateContent(json, path, attrs);
    break;
  case LLMProtocol::Unspecified:
    readCommon(json, attrs);
    break;
  }
  return attrs;
}

namespace {

// Charged per node so a long array of tiny values is not free.
constexpr uint64_t NodeOverheadBytes = 8;

enum class CopyResult { Ok, OverLimit, Unusable };

// Bytes one rule has spent. The per-request total is committed only after the whole value
// copied, so a dropped value does not eat the budget of the rules after it.
class Budget {
public:
  Budget(const ExtractionLimits& limits, uint64_t total_used)
      : limits_(limits), total_used_(total_used) {}

  bool charge(uint64_t bytes) {
    if (bytes > limits_.max_value_bytes - value_used_ ||
        bytes > limits_.max_total_bytes - total_used_ - value_used_) {
      return false;
    }
    value_used_ += bytes;
    return true;
  }

  uint64_t valueUsed() const { return value_used_; }

private:
  const ExtractionLimits& limits_;
  const uint64_t total_used_;
  uint64_t value_used_{0};
};

// Copies `in` into `out`, stopping at the first byte or depth over the limit so an oversized
// value costs no more than the limit.
CopyResult copyValue(const nlohmann::json& in, uint32_t depth, const ExtractionLimits& limits,
                     Budget& budget, Protobuf::Value& out) {
  if (!budget.charge(NodeOverheadBytes)) {
    return CopyResult::OverLimit;
  }
  switch (in.type()) {
  case nlohmann::json::value_t::null:
    out.set_null_value(Protobuf::NULL_VALUE);
    return CopyResult::Ok;
  case nlohmann::json::value_t::boolean:
    out.set_bool_value(in.get<bool>());
    return CopyResult::Ok;
  case nlohmann::json::value_t::number_integer: {
    // ``protobuf.Value`` stores numbers as doubles; a larger integer would reach the consumer
    // changed.
    const int64_t n = in.get<int64_t>();
    if (n > static_cast<int64_t>(MaxSafeCount) || n < -static_cast<int64_t>(MaxSafeCount)) {
      return CopyResult::Unusable;
    }
    out.set_number_value(static_cast<double>(n));
    return CopyResult::Ok;
  }
  case nlohmann::json::value_t::number_unsigned: {
    const uint64_t n = in.get<uint64_t>();
    if (n > MaxSafeCount) {
      return CopyResult::Unusable;
    }
    out.set_number_value(static_cast<double>(n));
    return CopyResult::Ok;
  }
  case nlohmann::json::value_t::number_float: {
    const double d = in.get<double>();
    if (!std::isfinite(d)) {
      return CopyResult::Unusable;
    }
    out.set_number_value(d);
    return CopyResult::Ok;
  }
  case nlohmann::json::value_t::string: {
    const auto& s = in.get_ref<const std::string&>();
    if (s.size() > limits.max_string_bytes || !budget.charge(s.size())) {
      return CopyResult::OverLimit;
    }
    out.set_string_value(s);
    return CopyResult::Ok;
  }
  case nlohmann::json::value_t::array: {
    if (depth >= limits.max_depth) {
      return CopyResult::OverLimit;
    }
    Protobuf::ListValue& list = *out.mutable_list_value();
    for (const nlohmann::json& element : in) {
      const CopyResult result = copyValue(element, depth + 1, limits, budget, *list.add_values());
      if (result != CopyResult::Ok) {
        return result;
      }
    }
    return CopyResult::Ok;
  }
  case nlohmann::json::value_t::object: {
    if (depth >= limits.max_depth) {
      return CopyResult::OverLimit;
    }
    auto& fields = *out.mutable_struct_value()->mutable_fields();
    for (const auto& [key, element] : in.items()) {
      if (key.size() > limits.max_string_bytes || !budget.charge(key.size())) {
        return CopyResult::OverLimit;
      }
      const CopyResult result = copyValue(element, depth + 1, limits, budget, fields[key]);
      if (result != CopyResult::Ok) {
        return result;
      }
    }
    return CopyResult::Ok;
  }
  case nlohmann::json::value_t::binary:
    // An offloaded string: its bytes are not in memory.
    return CopyResult::Unusable;
  case nlohmann::json::value_t::discarded:
    return CopyResult::Unusable;
  }
  return CopyResult::Unusable;
}

// The value at `path`, or nullptr when absent. Null counts as absent, like the rest of the filter.
const nlohmann::json* locate(const nlohmann::json& json, absl::Span<const std::string> path) {
  const nlohmann::json* node = &json;
  for (const std::string& key : path) {
    if (!node->is_object()) {
      return nullptr;
    }
    const auto it = node->find(key);
    if (it == node->end()) {
      return nullptr;
    }
    node = &*it;
  }
  return node->is_null() ? nullptr : node;
}

bool taken(const std::string& key, const Protobuf::Struct* existing, const Protobuf::Struct& out) {
  return out.fields().contains(key) || (existing != nullptr && existing->fields().contains(key));
}

} // namespace

ExtractionOutcome extractConfiguredValues(const nlohmann::json& json,
                                          absl::Span<const ExtractionRule> rules,
                                          const ExtractionLimits& limits,
                                          const Protobuf::Struct* existing, Protobuf::Struct& out) {
  ExtractionOutcome outcome;
  uint64_t total_used = 0;
  // Defaults wait until all rules ran, so a later rule can still supply the real value.
  absl::flat_hash_map<std::string, const Protobuf::Value*> defaults;

  for (const ExtractionRule& rule : rules) {
    if (taken(rule.key, existing, out)) {
      continue;
    }
    const nlohmann::json* value = locate(json, rule.path);
    if (value == nullptr) {
      if (rule.on_missing.has_value()) {
        defaults.try_emplace(rule.key, &*rule.on_missing);
      }
      continue;
    }
    Budget budget(limits, total_used);
    Protobuf::Value copied;
    switch (copyValue(*value, 0, limits, budget, copied)) {
    case CopyResult::Ok:
      total_used += budget.valueUsed();
      (*out.mutable_fields())[rule.key] = std::move(copied);
      ++outcome.written;
      break;
    case CopyResult::OverLimit:
      ++outcome.dropped;
      break;
    case CopyResult::Unusable:
      ++outcome.unusable;
      break;
    }
  }

  for (const auto& [key, value] : defaults) {
    if (!taken(key, existing, out)) {
      (*out.mutable_fields())[key] = *value;
      ++outcome.written;
    }
  }
  return outcome;
}

} // namespace RequestInfo
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
