#include "source/extensions/filters/http/mcp_json_rest_bridge/http_request_builder.h"

#include "source/common/http/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "nlohmann/json.hpp"
#include "re2/re2.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule;
using ::nlohmann::json;

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

absl::Status constructQueryParams(
    std::vector<QueryParam>& query_params, absl::string_view body_rule, const json& arguments,
    const absl::flat_hash_set<std::string>& templates, absl::string_view path,
    absl::Span<const HttpRule::ParameterBinding* const> header_parameter_bindings,
    absl::Span<const HttpRule::ParameterBinding* const> cookie_parameter_bindings) {
  // Skip if it's a URL path template
  if (templates.contains(path)) {
    return absl::OkStatus();
  }

  // Skip if it's part of the body
  if (!body_rule.empty()) {
    if (path == body_rule || (absl::StartsWith(path, body_rule) && path[body_rule.size()] == '.')) {
      return absl::OkStatus();
    }
  }

  // Skip if it's part of header parameter bindings.
  for (const auto& binding : header_parameter_bindings) {
    const absl::string_view arg_path = binding->argument_path();
    if (path == arg_path || (absl::StartsWith(path, arg_path) && path[arg_path.size()] == '.')) {
      return absl::OkStatus();
    }
  }
  // Skip if it's part of cookie parameter bindings.
  for (const auto& binding : cookie_parameter_bindings) {
    const absl::string_view arg_path = binding->argument_path();
    if (path == arg_path || (absl::StartsWith(path, arg_path) && path[arg_path.size()] == '.')) {
      return absl::OkStatus();
    }
  }

  if (arguments.is_object()) {
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
      absl::Status status =
          constructQueryParams(query_params, body_rule, it.value(), templates,
                               path.empty() ? it.key() : absl::StrCat(path, ".", it.key()),
                               header_parameter_bindings, cookie_parameter_bindings);
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }
  if (arguments.is_array()) {
    for (auto& array_item : arguments) {
      absl::Status status =
          constructQueryParams(query_params, body_rule, array_item, templates, path,
                               header_parameter_bindings, cookie_parameter_bindings);
      if (!status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  const std::string value = jsonValueToString(arguments);
  // Uses Http::Utility::PercentEncoding::urlEncode to escape the value.
  query_params.push_back({std::string(path), Http::Utility::PercentEncoding::urlEncode(value)});
  return absl::OkStatus();
}

void appendQueryParamsToBaseUrl(std::string& url, absl::Span<const QueryParam> query_params) {
  if (query_params.empty()) {
    return;
  }
  absl::StrAppend(
      &url, "?",
      absl::StrJoin(query_params, "&", [](std::string* out, const QueryParam& query_param) {
        absl::StrAppend(out, Http::Utility::PercentEncoding::urlEncode(query_param.key), "=",
                        query_param.value);
      }));
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

absl::StatusOr<json> constructRequestBody(
    absl::string_view body_rule, const absl::flat_hash_set<std::string>& templates,
    const json& arguments,
    absl::Span<const HttpRule::ParameterBinding* const> header_parameter_bindings,
    absl::Span<const HttpRule::ParameterBinding* const> cookie_parameter_bindings) {
  if (body_rule.empty()) {
    return nullptr;
  }
  if (body_rule == "*") {
    json body = arguments;
    for (const auto& path : templates) {
      removeJsonPath(body, path);
    }
    for (const auto& binding : header_parameter_bindings) {
      removeJsonPath(body, binding->argument_path());
    }
    for (const auto& binding : cookie_parameter_bindings) {
      removeJsonPath(body, binding->argument_path());
    }
    return body;
  }
  return getJsonValue(arguments, body_rule);
}

void populateParamsMap(absl::Span<const HttpRule::ParameterBinding* const> bindings,
                       const json& arguments,
                       absl::flat_hash_map<std::string, std::string>& params_map) {
  for (const auto& binding : bindings) {
    absl::StatusOr<json> value = getJsonValue(arguments, binding->argument_path());
    if (!value.ok()) {
      // This is expected when the parameter is optional.
      continue;
    }
    params_map[binding->name()] =
        Http::Utility::PercentEncoding::urlEncode(jsonValueToString(*std::move(value)));
  }
}

} // namespace

absl::StatusOr<std::string> constructBaseUrl(absl::string_view pattern,
                                             const absl::flat_hash_set<std::string>& templates,
                                             const nlohmann::json& arguments) {
  std::string base_url = std::string(pattern);
  for (const auto& element : templates) {
    absl::StatusOr<nlohmann::json> template_value_json = getJsonValue(arguments, element);
    if (!template_value_json.ok()) {
      return template_value_json.status();
    }
    // Non-visible ASCII characters are always escaped by Http::Utility::PercentEncoding::encode,
    // in addition to the specified reserved characters.
    std::string value_str = Http::Utility::PercentEncoding::encode(
        jsonValueToString(*template_value_json), ReservedChars);
    std::string var_pattern = absl::StrCat("\\{", RE2::QuoteMeta(element), "(?:=[^}]+)?\\}");
    RE2::GlobalReplace(&base_url, var_pattern, value_str);
  }
  return base_url;
}

absl::StatusOr<HttpRequest> buildHttpRequest(
    const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule& http_rule,
    const nlohmann::json& arguments,
    absl::Span<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule::
                   ParameterBinding* const>
        header_parameter_bindings,
    absl::Span<const envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule::
                   ParameterBinding* const>
        cookie_parameter_bindings) {
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
    return absl::InvalidArgumentError("Unsupported HTTP method in HttpRule");
  }
  absl::string_view url_template = pattern;
  absl::flat_hash_set<std::string> templates;
  std::string template_capture;
  static const LazyRE2 template_regex = {R"(\{([a-zA-Z0-9_.]+)(?:=.*?)?\})"};
  while (RE2::FindAndConsume(&url_template, *template_regex, &template_capture)) {
    templates.insert(template_capture);
  }
  absl::StatusOr<std::string> url = constructBaseUrl(pattern, templates, arguments);
  if (!url.ok()) {
    return url.status();
  }

  std::vector<QueryParam> query_params;
  if (http_rule.body() != "*") {
    std::string base_path;
    if (auto status =
            constructQueryParams(query_params, http_rule.body(), arguments, templates, base_path,
                                 header_parameter_bindings, cookie_parameter_bindings);
        !status.ok()) {
      return status;
    }
  }
  appendQueryParamsToBaseUrl(*url, query_params);

  absl::StatusOr<json> http_body = constructRequestBody(
      http_rule.body(), templates, arguments, header_parameter_bindings, cookie_parameter_bindings);
  if (!http_body.ok()) {
    return http_body.status();
  }

  absl::flat_hash_map<std::string, std::string> headers_params;
  populateParamsMap(header_parameter_bindings, arguments, headers_params);
  absl::flat_hash_map<std::string, std::string> cookies_params;
  populateParamsMap(cookie_parameter_bindings, arguments, cookies_params);

  return HttpRequest{
      .url = *std::move(url),
      .method = std::move(method),
      .body = *std::move(http_body),
      .headers_params = std::move(headers_params),
      .cookies_params = std::move(cookies_params),
  };
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
