#include "source/extensions/filters/http/mcp_json_rest_bridge/http_request_builder.h"

#include <cstdint>

#include "source/common/http/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "nlohmann/json.hpp"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::nlohmann::json;

// Maximum nesting depth limit for parsing JSON arguments into query parameters in
// `constructQueryParams`. Legitimate REST API query parameter schemas rarely exceed 5–10
// nesting levels, and 100 aligns with standard Protocol Buffers / gRPC-JSON transcoding
// recursion limits (e.g. `CodedInputStream::default_recursion_limit_`).
constexpr uint32_t MaxNestingDepth = 100;

absl::StatusOr<json> getJsonValue(const json& data, absl::string_view path) {
  std::vector<absl::string_view> parts = absl::StrSplit(path, '.');
  json current = data;
  for (const auto& part : parts) {
    if (!current.contains(part)) {
      return absl::InvalidArgumentError(absl::StrCat("Could not find value for path: ", path));
    }
    current = current[part];
  }
  return current;
}

std::string jsonValueToString(const json& j) {
  if (j.is_string()) {
    return j.get<std::string>();
  }
  return j.dump();
}

// Key and value for HTTP query parameter.
struct QueryParam {
  std::string key;
  std::string value;
};

absl::Status constructQueryParams(std::vector<QueryParam>& query_params,
                                  absl::string_view body_rule, const json& arguments,
                                  const absl::flat_hash_set<std::string>& templates,
                                  const std::string& path, uint32_t depth = 0) {
  if (depth > MaxNestingDepth) {
    return absl::InvalidArgumentError("JSON payload exceeds maximum nesting depth limit");
  }
  // Skip if it's a URL path template
  if (templates.contains(path)) {
    return absl::OkStatus();
  }

  // Skip if it's part of the body
  if (!body_rule.empty()) {
    if (path == body_rule || absl::StartsWith(path, std::string(body_rule) + ".")) {
      return absl::OkStatus();
    }
  }

  if (arguments.is_object()) {
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
      absl::Status status =
          constructQueryParams(query_params, body_rule, it.value(), templates,
                               path.empty() ? it.key() : path + "." + it.key(), depth + 1);
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }
  if (arguments.is_array()) {
    for (auto& array_item : arguments) {
      absl::Status status =
          constructQueryParams(query_params, body_rule, array_item, templates, path, depth + 1);
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  const std::string value = jsonValueToString(arguments);
  // Uses Http::Utility::PercentEncoding::urlEncode to escape the value.
  query_params.push_back({path, Http::Utility::PercentEncoding::urlEncode(value)});
  return absl::OkStatus();
}

void appendQueryParamsToBaseUrl(std::string& url, absl::Span<const QueryParam> query_params) {
  if (query_params.empty()) {
    return;
  }
  url += "?";
  url += absl::StrJoin(query_params, "&", [](std::string* out, const QueryParam& query_param) {
    absl::StrAppend(out, Http::Utility::PercentEncoding::urlEncode(query_param.key), "=",
                    query_param.value);
  });
}

// Recursively removes a path from a JSON object.
// Returns true if `data` becomes empty after removal, false otherwise.
bool recursiveRemoveJsonPath(json& data, absl::Span<const absl::string_view> parts) {
  if (parts.empty()) {
    return false;
  }
  absl::string_view key = parts[0];
  if (!data.is_object() || !data.contains(key)) {
    return false;
  }

  if (parts.size() == 1) {
    data.erase(key);
  } else {
    if (recursiveRemoveJsonPath(data[key], parts.subspan(1)) && data[key].empty()) {
      data.erase(key);
    }
  }
  return data.empty();
}

void removeJsonPath(json& data, absl::string_view path) {
  if (path.empty()) {
    return;
  }
  std::vector<absl::string_view> parts = absl::StrSplit(path, '.');
  recursiveRemoveJsonPath(data, parts);
}

absl::StatusOr<json> constructRequestBody(absl::string_view body_rule,
                                          const absl::flat_hash_set<std::string>& templates,
                                          const json& arguments) {
  if (body_rule.empty()) {
    return nullptr;
  }
  if (body_rule == "*") {
    json body = arguments;
    for (const auto& path : templates) {
      removeJsonPath(body, path);
    }
    return body;
  }
  return getJsonValue(arguments, body_rule);
}

// Substitutes `value` for every `{element}` and `{element=pattern}` occurrence in `url`.
//
// This is the literal equivalent of the RE2 pattern `\{element(?:=[^}]+)?\}`, which used to be
// compiled on every request. `element` is only ever a literal name (`template_regex` restricts it
// to `[a-zA-Z0-9_.]+`), so no regex is needed to find it.
void substitutePathTemplateVariable(std::string& url, absl::string_view element,
                                    absl::string_view value) {
  const std::string opening = absl::StrCat("{", element);
  size_t pos = 0;
  while ((pos = url.find(opening, pos)) != std::string::npos) {
    // Index just past `{element`. A match closes either immediately, or after an `=pattern` of
    // one or more non-'}' characters.
    const size_t after_name = pos + opening.size();
    size_t closing = std::string::npos;
    if (after_name < url.size() && url[after_name] == '}') {
      closing = after_name;
    } else if (after_name < url.size() && url[after_name] == '=') {
      const size_t candidate = url.find('}', after_name + 1);
      // `[^}]+` requires at least one character between '=' and '}'.
      if (candidate != std::string::npos && candidate > after_name + 1) {
        closing = candidate;
      }
    }
    if (closing == std::string::npos) {
      // Not a match: a longer name that merely starts with `element` (`{elementary}`), or an
      // unterminated `{element`. Advance one character, as a regex scan would.
      ++pos;
      continue;
    }
    url.replace(pos, closing - pos + 1, value.data(), value.size());
    // Resume past the substituted value so it is never scanned again, as RE2::GlobalReplace did.
    pos += value.size();
  }
}

} // namespace

absl::StatusOr<std::string> constructBaseUrl(absl::string_view pattern,
                                             const absl::flat_hash_set<std::string>& templates,
                                             const nlohmann::json& arguments,
                                             BridgeStatus& bridge_status) {
  std::string base_url = std::string(pattern);
  for (const auto& element : templates) {
    absl::StatusOr<nlohmann::json> template_value_json = getJsonValue(arguments, element);
    if (!template_value_json.ok()) {
      bridge_status = BridgeStatus::RequestToolsCallMissingRequiredArg;
      return template_value_json.status();
    }
    const std::string raw_value = jsonValueToString(*template_value_json);

    // The constructed URL is installed verbatim as the upstream `:path` and is not re-normalized by
    // Envoy, so no attacker-controlled value may contain a '.'/'..' traversal segment (#45931).
    // '\' is percent-encoded below, but an upstream that decodes it and folds it to '/' would see a
    // traversal, so treat it as a separator here too.
    for (const absl::string_view segment : absl::StrSplit(raw_value, absl::ByAnyChar("\\/"))) {
      if (segment == "." || segment == "..") {
        bridge_status = BridgeStatus::RequestToolsCallPathTraversalRejected;
        return absl::InvalidArgumentError(absl::StrCat(
            "path template variable '", element, "' must not contain path traversal segments"));
      }
    }
    // A simple variable (`{id}`) fills one segment, so its '/' is encoded; a variable with an
    // explicit pattern (`{name=projects/*}`) may span segments, so its '/' is preserved.
    const absl::string_view reserved_chars =
        absl::StrContains(pattern, absl::StrCat("{", element, "=")) ? ReservedChars
                                                                    : ReservedCharsWithSlash;

    // Non-visible ASCII characters are always escaped by Http::Utility::PercentEncoding::encode,
    // in addition to the specified reserved characters.
    const std::string value_str = Http::Utility::PercentEncoding::encode(raw_value, reserved_chars);
    substitutePathTemplateVariable(base_url, element, value_str);
  }
  return base_url;
}

absl::StatusOr<HttpRequest> buildHttpRequest(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule& http_rule,
    const nlohmann::json& arguments, BridgeStatus& bridge_status) {
  std::string pattern;
  std::string method;
  // TODO(guoyilin42): Add validation to ensure exactly one HTTP method is specified.
  if (!http_rule.get().empty()) {
    method = "GET";
    pattern = http_rule.get();
  } else if (!http_rule.put().empty()) {
    method = "PUT";
    pattern = http_rule.put();
  } else if (!http_rule.post().empty()) {
    method = "POST";
    pattern = http_rule.post();
  } else if (!http_rule.delete_().empty()) {
    method = "DELETE";
    pattern = http_rule.delete_();
  } else if (!http_rule.patch().empty()) {
    method = "PATCH";
    pattern = http_rule.patch();
  } else {
    bridge_status = BridgeStatus::InternalToolsCallInvalidHttpRule;
    return absl::InvalidArgumentError("HttpRule is malformed");
  }
  absl::string_view url_template = pattern;
  absl::flat_hash_set<std::string> templates;
  std::string template_capture;
  static const LazyRE2 template_regex = {R"(\{([a-zA-Z0-9_.]+)(?:=.*?)?\})"};
  while (RE2::FindAndConsume(&url_template, *template_regex, &template_capture)) {
    templates.insert(template_capture);
  }
  absl::StatusOr<std::string> url = constructBaseUrl(pattern, templates, arguments, bridge_status);
  if (!url.ok()) {
    return url.status();
  }

  std::vector<QueryParam> query_params;
  if (http_rule.body() != "*") {
    std::string base_path;
    if (auto status =
            constructQueryParams(query_params, http_rule.body(), arguments, templates, base_path);
        !status.ok()) {
      bridge_status = BridgeStatus::RequestToolsCallMissingRequiredArg;
      return status;
    }
  }
  appendQueryParamsToBaseUrl(*url, query_params);

  absl::StatusOr<json> http_body = constructRequestBody(http_rule.body(), templates, arguments);
  if (!http_body.ok()) {
    bridge_status = BridgeStatus::RequestToolsCallMissingRequiredArg;
    return http_body.status();
  }

  return HttpRequest{
      .url = *std::move(url),
      .method = std::move(method),
      .body = *std::move(http_body),
  };
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
