#include "source/extensions/http/ai_filters/dynamic_modules/json_pointer.h"

#include <optional>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace DynamicModules {
namespace {

using HttpFilters::AiProtocolManager::JsonWithExtBuf;

constexpr absl::string_view AppendToken = "-";

// The unescaped reference tokens of `pointer`.
absl::StatusOr<std::vector<std::string>> parsePointer(absl::string_view pointer) {
  std::vector<std::string> tokens;
  if (pointer.empty()) {
    return tokens;
  }
  if (pointer.front() != '/') {
    return absl::InvalidArgumentError("JSON pointer must be empty or start with '/'");
  }
  for (absl::string_view escaped : absl::StrSplit(pointer.substr(1), '/')) {
    std::string& token = tokens.emplace_back();
    token.reserve(escaped.size());
    for (size_t i = 0; i < escaped.size(); ++i) {
      if (escaped[i] != '~') {
        token.push_back(escaped[i]);
        continue;
      }
      if (i + 1 == escaped.size() || (escaped[i + 1] != '0' && escaped[i + 1] != '1')) {
        return absl::InvalidArgumentError("JSON pointer has an invalid '~' escape");
      }
      token.push_back(escaped[++i] == '0' ? '~' : '/');
    }
  }
  return tokens;
}

// An array index token is "0" or digits without a leading zero.
std::optional<size_t> arrayIndex(absl::string_view token) {
  size_t index = 0;
  if (token.empty() || (token.size() > 1 && token.front() == '0') ||
      !absl::c_all_of(token, [](char c) { return absl::ascii_isdigit(c); }) ||
      !absl::SimpleAtoi(token, &index)) {
    return std::nullopt;
  }
  return index;
}

template <class Json> Json* child(Json& parent, absl::string_view token) {
  if (parent.is_object()) {
    auto it = parent.find(token);
    return it == parent.end() ? nullptr : &*it;
  }
  if (parent.is_array()) {
    const std::optional<size_t> index = arrayIndex(token);
    return index.has_value() && *index < parent.size() ? &parent[*index] : nullptr;
  }
  return nullptr;
}

template <class Json> Json* resolve(Json& document, absl::Span<const std::string> tokens) {
  Json* value = &document;
  for (const std::string& token : tokens) {
    value = child(*value, token);
    if (value == nullptr) {
      return nullptr;
    }
  }
  return value;
}

bool holdsExternalRef(const nlohmann::json& value) {
  return JsonWithExtBuf::isExternalRef(value) ||
         (value.is_structured() && absl::c_any_of(value, holdsExternalRef));
}

} // namespace

absl::StatusOr<std::string> getJson(const nlohmann::json& document, absl::string_view pointer) {
  absl::StatusOr<std::vector<std::string>> tokens = parsePointer(pointer);
  if (!tokens.ok()) {
    return tokens.status();
  }
  const nlohmann::json* value = resolve(document, *tokens);
  if (value == nullptr) {
    return absl::NotFoundError("JSON pointer names no value");
  }
  if (holdsExternalRef(*value)) {
    return absl::FailedPreconditionError("value holds a string offloaded to the external buffer");
  }
  return value->dump(-1, ' ', /*ensure_ascii=*/false, nlohmann::json::error_handler_t::replace);
}

absl::Status setJson(nlohmann::json& document, absl::string_view pointer, absl::string_view value) {
  absl::StatusOr<std::vector<std::string>> tokens = parsePointer(pointer);
  if (!tokens.ok()) {
    return tokens.status();
  }
  nlohmann::json parsed =
      nlohmann::json::parse(value.begin(), value.end(), nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded()) {
    return absl::InvalidArgumentError("value is not valid JSON");
  }
  if (tokens->empty()) {
    if (!parsed.is_object()) {
      return absl::InvalidArgumentError("the document root must be an object");
    }
    document = std::move(parsed);
    return absl::OkStatus();
  }

  const std::string last = std::move(tokens->back());
  tokens->pop_back();
  nlohmann::json* parent = resolve(document, *tokens);
  if (parent != nullptr && parent->is_object()) {
    (*parent)[last] = std::move(parsed);
    return absl::OkStatus();
  }
  if (parent != nullptr && parent->is_array()) {
    if (last == AppendToken) {
      parent->push_back(std::move(parsed));
      return absl::OkStatus();
    }
    if (nlohmann::json* element = child(*parent, last); element != nullptr) {
      *element = std::move(parsed);
      return absl::OkStatus();
    }
  }
  return absl::NotFoundError("JSON pointer names no location");
}

absl::Status removeJson(nlohmann::json& document, absl::string_view pointer) {
  absl::StatusOr<std::vector<std::string>> tokens = parsePointer(pointer);
  if (!tokens.ok()) {
    return tokens.status();
  }
  if (tokens->empty()) {
    return absl::InvalidArgumentError("the document root cannot be removed");
  }

  const std::string last = std::move(tokens->back());
  tokens->pop_back();
  nlohmann::json* parent = resolve(document, *tokens);
  if (parent != nullptr && parent->is_object() && parent->erase(last) == 1) {
    return absl::OkStatus();
  }
  if (parent != nullptr && parent->is_array()) {
    if (const std::optional<size_t> index = arrayIndex(last);
        index.has_value() && *index < parent->size()) {
      parent->erase(*index);
      return absl::OkStatus();
    }
  }
  return absl::NotFoundError("JSON pointer names no value");
}

} // namespace DynamicModules
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
