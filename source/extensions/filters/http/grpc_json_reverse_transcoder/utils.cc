#include "source/extensions/filters/http/grpc_json_reverse_transcoder/utils.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/http/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

namespace {

absl::Status BuildReplacementVector(nlohmann::json& request, std::string http_rule_path,
                                    std::vector<std::pair<std::string, std::string>>& replacements,
                                    absl::flat_hash_set<std::string>& param_set) {
  size_t end = 0;
  // Iterate through the path and replace the placeholders with the values from
  // the request message.
  while (true) {
    // Start by searching for the next opening and the closing brace after the
    // given index. The sub-string between the two braces is the key we need to
    // search for in the request message.
    size_t start = http_rule_path.find('{', end);
    if (start == std::string::npos) {
      break;
    }
    end = http_rule_path.find('}', start);
    if (end == std::string::npos) {
      return absl::InvalidArgumentError(absl::StrCat("Invalid HTTP path: ", http_rule_path));
    }
    std::string key = http_rule_path.substr(start + 1, end - start - 1);
    bool has_one_path_segment = true;
    // The sub-string in the braces can be of the form {key = /resource/*}, if
    // that's the case, just extract the part before the `=` to get the key to
    // search for in the request object.
    size_t eq_index = http_rule_path.find('=', start);
    if (eq_index != std::string::npos && eq_index < end) {
      has_one_path_segment = false;
      key = http_rule_path.substr(start + 1, eq_index - start - 1);
    }

    // Keep track of all the keys extracted from the path so that we don't add
    // then in the query param string again.
    param_set.insert(key);
    // A key can be a multi-word string. For example, the key can be
    // "parent.id". In this case, we need to extract the value of the "parent"
    // field from the request object and then extract the value of the "id"
    // field from the "parent" object.
    absl::optional<std::string> param_value =
        GetNestedJsonValueAsString(request, key, has_one_path_segment);
    if (!param_value.has_value()) {
      return absl::InvalidArgumentError(
          absl::StrCat("Key, ", key, ", not found in the request message"));
    }
    replacements.push_back(
        {std::string(http_rule_path.substr(start, end - start + 1)), param_value.value()});
  }
  return absl::OkStatus();
}

} // namespace

std::string BuildGrpcMessage(Envoy::Buffer::Instance& body_data) {
  const uint64_t message_length = body_data.length();
  std::string message;
  message.reserve(message_length);
  message.resize(message_length);
  body_data.copyOut(0, message_length, message.data());

  return Envoy::Http::Utility::PercentEncoding::encode(message);
}

absl::optional<std::string> GetNestedJsonValueAsString(const nlohmann::json& object,
                                                       const std::string& key,
                                                       bool has_one_path_segment) {
  nlohmann::json::json_pointer key_pointer(
      absl::StrCat("/", absl::StrReplaceAll(key, {{".", "/"}})));
  if (!object.contains(key_pointer)) {
    return std::nullopt;
  }
  std::string value;
  if (object[key_pointer].is_string()) {
    value = object[key_pointer].get<std::string>();
  } else {
    value = object[key_pointer].dump();
  }
  return Envoy::Http::Utility::PercentEncoding::encode(
      value, has_one_path_segment ? absl::StrCat(reserved_chars, "/") : reserved_chars);
}

void BuildQueryParamString(const nlohmann::json& object,
                           const absl::flat_hash_set<std::string>& ignore_list,
                           std::string* query_string, std::string prefix) {
  if (ignore_list.contains(prefix)) {
    return;
  }

  if (object.is_primitive()) {
    std::string value;
    if (object.is_string()) {
      value = object.get<std::string>();
    } else {
      value = object.dump();
    }
    absl::StrAppend(query_string, query_string->empty() ? "" : "&", prefix, "=",
                    Envoy::Http::Utility::PercentEncoding::urlEncodeQueryParameter(value));
    return;
  }

  if (object.is_array()) {
    for (auto& array_item : object) {
      BuildQueryParamString(array_item, ignore_list, query_string, prefix);
    }
    return;
  }

  for (auto& it : object.items()) {
    std::string key = absl::StrCat(prefix, prefix.empty() ? "" : ".", it.key());
    BuildQueryParamString(it.value(), ignore_list, query_string, key);
  }
}

absl::StatusOr<std::string> BuildPath(nlohmann::json& request, std::string http_rule_path,
                                      std::string http_body_field) {
  std::vector<std::pair<std::string, std::string>> replacements;
  absl::flat_hash_set<std::string> param_set;
  absl::Status status = BuildReplacementVector(request, http_rule_path, replacements, param_set);
  if (!status.ok()) {
    return status;
  }
  // Replace all the placeholders in the path with their values from the request
  // message.
  http_rule_path = absl::StrReplaceAll(http_rule_path, replacements);

  // If the body field is not the entire request message and there are still
  // some fields left in the request message after populating the path, add them
  // as query parameters to the path.
  if (http_body_field != "*") {
    if (!http_body_field.empty()) {
      param_set.insert(http_body_field);
    }
    std::string query_string;
    BuildQueryParamString(request, param_set, &query_string);
    if (!query_string.empty()) {
      absl::StrAppend(&http_rule_path, "?", query_string);
    }
  }
  return http_rule_path;
}

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
