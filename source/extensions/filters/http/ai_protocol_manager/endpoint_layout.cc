#include "source/extensions/filters/http/ai_protocol_manager/endpoint_layout.h"

#include <string>
#include <vector>

#include "source/common/common/macros.h"
#include "source/common/http/header_utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/status/statusor.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

using envoy::type::ai::v3::Endpoint;
using envoy::type::ai::v3::EndpointTemplate;
using ProtoLLMProtocol = envoy::type::ai::v3::LLMProtocol;

constexpr size_t MaxValueLength = 256;

EndpointTemplate modelInBodyLayout(absl::string_view path) {
  EndpointTemplate layout;
  layout.set_path_template(path);
  return layout;
}

EndpointTemplate anthropicLayout() {
  EndpointTemplate layout = modelInBodyLayout("/v1/messages");
  (*layout.mutable_request_headers_to_add())["anthropic-version"] = "2023-06-01";
  // A Vertex or Bedrock envelope field, which the native API rejects.
  layout.add_remove_body_fields("anthropic_version");
  return layout;
}

EndpointTemplate geminiLayout(absl::string_view models_path) {
  EndpointTemplate layout;
  layout.set_path_template(absl::StrCat(models_path, "/{model}:{method}"));
  layout.set_unary_method("generateContent");
  layout.set_stream_method("streamGenerateContent");
  (*layout.mutable_stream_query_params())["alt"] = "sse";
  layout.set_model_placement(EndpointTemplate::MODEL_IN_PATH);
  layout.set_stream_placement(EndpointTemplate::STREAM_IN_METHOD);
  for (const char* field : {"model", "stream", "stream_options"}) {
    layout.add_remove_body_fields(field);
  }
  return layout;
}

EndpointTemplate vertexAnthropicLayout() {
  EndpointTemplate layout;
  layout.set_path_template(
      "/v1/projects/{project}/locations/{location}/publishers/anthropic/models/{model}:{method}");
  layout.set_unary_method("rawPredict");
  layout.set_stream_method("streamRawPredict");
  layout.set_model_placement(EndpointTemplate::MODEL_IN_PATH);
  layout.set_stream_placement(EndpointTemplate::STREAM_IN_BODY_AND_METHOD);
  (*layout.mutable_set_body_fields()->mutable_fields())["anthropic_version"].set_string_value(
      "vertex-2023-10-16");
  layout.add_remove_body_fields("model");
  return layout;
}

EndpointTemplate bedrockAnthropicLayout() {
  EndpointTemplate layout;
  layout.set_path_template("/model/{model}/{method}");
  layout.set_unary_method("invoke");
  layout.set_stream_method("invoke-with-response-stream");
  layout.set_model_placement(EndpointTemplate::MODEL_IN_PATH);
  layout.set_stream_placement(EndpointTemplate::STREAM_IN_METHOD);
  layout.set_response_framing(EndpointTemplate::AWS_EVENT_STREAM);
  (*layout.mutable_set_body_fields()->mutable_fields())["anthropic_version"].set_string_value(
      "bedrock-2023-05-31");
  layout.add_remove_body_fields("model");
  layout.add_remove_body_fields("stream");
  return layout;
}

EndpointTemplate azureOpenAiLayout() {
  EndpointTemplate layout;
  layout.set_path_template("/openai/deployments/{model}/chat/completions");
  layout.set_model_placement(EndpointTemplate::MODEL_IN_PATH);
  (*layout.mutable_query_params())["api-version"] = "{api_version}";
  return layout;
}

std::vector<EndpointPreset> buildPresets() {
  std::vector<EndpointPreset> presets;
  presets.push_back(
      {"openai",
       {{envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS, modelInBodyLayout("/v1/chat/completions")},
        {envoy::type::ai::v3::OPENAI_RESPONSES, modelInBodyLayout("/v1/responses")}},
       {},
       {}});
  presets.push_back(
      {"anthropic", {{envoy::type::ai::v3::ANTHROPIC_MESSAGES, anthropicLayout()}}, {}, {}});
  presets.push_back(
      {"gemini_api",
       {{envoy::type::ai::v3::GEMINI_GENERATE_CONTENT, geminiLayout("/v1beta/models")}},
       {},
       {}});
  presets.push_back(
      {"gcp_vertex_ai",
       {{envoy::type::ai::v3::GEMINI_GENERATE_CONTENT,
         geminiLayout("/v1/projects/{project}/locations/{location}/publishers/google/models")},
        {envoy::type::ai::v3::ANTHROPIC_MESSAGES, vertexAnthropicLayout()},
        {envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS,
         modelInBodyLayout(
             "/v1/projects/{project}/locations/{location}/endpoints/openapi/chat/completions")}},
       {"project", "location"},
       {}});
  // Express mode authenticates with an API key, which only Gemini models accept there.
  presets.push_back({"gcp_vertex_ai_express",
                     {{envoy::type::ai::v3::GEMINI_GENERATE_CONTENT,
                       geminiLayout("/v1/publishers/google/models")}},
                     {},
                     {}});
  // The region is for SigV4 signing and the authority, not the path.
  presets.push_back({"aws_bedrock",
                     {{envoy::type::ai::v3::ANTHROPIC_MESSAGES, bedrockAnthropicLayout()}},
                     {},
                     {"region"}});
  presets.push_back({"azure_openai",
                     {{envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS, azureOpenAiLayout()}},
                     {"api_version"},
                     {}});
  return presets;
}

absl::Status invalid(absl::string_view message) { return absl::InvalidArgumentError(message); }

// Non-empty, bounded, and safe as a path segment or query value without encoding.
absl::Status validateValue(absl::string_view what, absl::string_view value) {
  if (value.empty() || value.size() > MaxValueLength) {
    return invalid(absl::StrCat(what, " must be 1 to ", MaxValueLength, " bytes"));
  }
  if (value == ".") {
    return invalid(absl::StrCat(what, " must not be '.'"));
  }
  if (absl::StrContains(value, "..")) {
    return invalid(absl::StrCat(what, " must not contain '..'"));
  }
  for (const char c : value) {
    if (c == '/' || c == '?' || c == '#' || c == '%' || absl::ascii_isspace(c) ||
        absl::ascii_iscntrl(c) || !absl::ascii_isascii(c)) {
      return invalid(absl::StrCat(what, " must not contain '/', '?', '#', '%', whitespace, control "
                                        "or non-ASCII characters"));
    }
  }
  return absl::OkStatus();
}

absl::Status validateVariables(const Endpoint& endpoint) {
  for (const auto& [name, value] : endpoint.variables()) {
    if (absl::Status status = validateValue(absl::StrCat("variable '", name, "'"), value);
        !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

bool isReservedPlaceholder(absl::string_view name) { return name == "model" || name == "method"; }

bool isPlaceholderName(absl::string_view name) {
  if (name.empty() || !absl::ascii_islower(name[0])) {
    return false;
  }
  for (const char c : name) {
    if (!absl::ascii_islower(c) && !absl::ascii_isdigit(c) && c != '_') {
      return false;
    }
  }
  return true;
}

// Splits `text` into literal pieces and `{name}` placeholders; `on_placeholder` sees each name and
// `on_literal` each piece of literal text.
template <class OnLiteral, class OnPlaceholder>
absl::Status forEachPart(absl::string_view what, absl::string_view text, OnLiteral on_literal,
                         OnPlaceholder on_placeholder) {
  while (!text.empty()) {
    const size_t open = text.find_first_of("{}");
    if (open == absl::string_view::npos) {
      on_literal(text);
      return absl::OkStatus();
    }
    if (text[open] == '}') {
      return invalid(absl::StrCat(what, " has a '}' with no '{'"));
    }
    on_literal(text.substr(0, open));
    const size_t close = text.find('}', open);
    if (close == absl::string_view::npos) {
      return invalid(absl::StrCat(what, " has a '{' with no '}'"));
    }
    const absl::string_view name = text.substr(open + 1, close - open - 1);
    if (!isPlaceholderName(name)) {
      return invalid(absl::StrCat(what, " has a malformed placeholder '{", name,
                                  "}': names are [a-z][a-z0-9_]*"));
    }
    if (absl::Status status = on_placeholder(name); !status.ok()) {
      return status;
    }
    text.remove_prefix(close + 1);
  }
  return absl::OkStatus();
}

absl::Status validatePathTemplate(const EndpointTemplate& layout,
                                  absl::flat_hash_set<std::string>& placeholders) {
  const absl::string_view path = layout.path_template();
  if (!absl::StartsWith(path, "/")) {
    return invalid("path_template must start with '/'");
  }
  bool literal_ok = true;
  absl::Status status = forEachPart(
      "path_template", path,
      [&literal_ok](absl::string_view literal) {
        for (const char c : literal) {
          if (c == '?' || c == '#' || absl::ascii_isspace(c) || absl::ascii_iscntrl(c) ||
              !absl::ascii_isascii(c)) {
            literal_ok = false;
          }
        }
      },
      [&placeholders](absl::string_view name) {
        placeholders.emplace(name);
        return absl::OkStatus();
      });
  if (!status.ok()) {
    return status;
  }
  if (!literal_ok) {
    return invalid(
        "path_template must not contain '?', '#', whitespace, control or non-ASCII characters");
  }
  return absl::OkStatus();
}

absl::Status validatePlacements(const EndpointTemplate& layout,
                                const absl::flat_hash_set<std::string>& placeholders) {
  const bool model_in_path = layout.model_placement() == EndpointTemplate::MODEL_IN_PATH;
  if (placeholders.contains("model") != model_in_path) {
    return invalid("path_template has a {model} placeholder if and only if model_placement is "
                   "MODEL_IN_PATH");
  }
  const bool method_in_path =
      layout.stream_placement() == EndpointTemplate::STREAM_IN_METHOD ||
      layout.stream_placement() == EndpointTemplate::STREAM_IN_BODY_AND_METHOD;
  if (placeholders.contains("method") != method_in_path) {
    return invalid("path_template has a {method} placeholder if and only if stream_placement is "
                   "STREAM_IN_METHOD or STREAM_IN_BODY_AND_METHOD");
  }
  if (method_in_path && (layout.unary_method().empty() || layout.stream_method().empty())) {
    return invalid("stream_placement in the method requires unary_method and stream_method");
  }
  for (const auto& [what, method] : {std::make_pair("unary_method", &layout.unary_method()),
                                     std::make_pair("stream_method", &layout.stream_method())}) {
    if (!method->empty()) {
      if (absl::Status status = validateValue(what, *method); !status.ok()) {
        return status;
      }
    }
  }
  return absl::OkStatus();
}

// Substitutes `{name}` placeholders in `value` from the endpoint's variables, recording each used.
absl::StatusOr<std::string> substitute(absl::string_view what, absl::string_view value,
                                       const Endpoint& endpoint,
                                       absl::flat_hash_set<std::string>& used) {
  std::string result;
  absl::Status status = forEachPart(
      what, value, [&result](absl::string_view literal) { absl::StrAppend(&result, literal); },
      [&](absl::string_view name) -> absl::Status {
        if (isReservedPlaceholder(name)) {
          return invalid(
              absl::StrCat(what, " must not use the reserved placeholder '{", name, "}'"));
        }
        const auto it = endpoint.variables().find(std::string(name));
        if (it == endpoint.variables().end()) {
          return invalid(absl::StrCat(what, " names no variable '", name, "'"));
        }
        used.emplace(name);
        absl::StrAppend(&result, it->second);
        return absl::OkStatus();
      });
  if (!status.ok()) {
    return status;
  }
  return result;
}

absl::Status validateQueryParams(const Endpoint& endpoint, absl::flat_hash_set<std::string>& used) {
  const EndpointTemplate& layout = endpoint.custom();
  for (const auto* params : {&layout.query_params(), &layout.stream_query_params()}) {
    for (const auto& [name, value] : *params) {
      if (absl::Status status = validateValue("query parameter name", name); !status.ok()) {
        return status;
      }
      const std::string what = absl::StrCat("query parameter '", name, "'");
      absl::StatusOr<std::string> substituted = substitute(what, value, endpoint, used);
      if (!substituted.ok()) {
        return substituted.status();
      }
      if (absl::Status status = validateValue(what, *substituted); !status.ok()) {
        return status;
      }
    }
  }
  return absl::OkStatus();
}

bool isCredentialHeader(absl::string_view name) {
  static constexpr absl::string_view CredentialHeaders[] = {
      "authorization", "proxy-authorization", "x-api-key", "api-key", "x-goog-api-key", "cookie"};
  for (const absl::string_view header : CredentialHeaders) {
    if (name == header) {
      return true;
    }
  }
  return false;
}

absl::Status validateHeaderName(absl::string_view name) {
  if (name.empty() || !Http::HeaderUtility::headerNameIsValid(name) ||
      absl::AsciiStrToLower(name) != name) {
    return invalid(absl::StrCat("header name '", name, "' must be a valid lowercase name"));
  }
  if (name[0] == ':' || name == "host") {
    return invalid(absl::StrCat("header '", name, "' must not be a pseudo-header or host"));
  }
  return absl::OkStatus();
}

absl::Status validateHeaders(const EndpointTemplate& layout) {
  for (const auto& [name, value] : layout.request_headers_to_add()) {
    if (absl::Status status = validateHeaderName(name); !status.ok()) {
      return status;
    }
    if (isCredentialHeader(name)) {
      return invalid(absl::StrCat("header '", name, "' carries a credential and must not be set"));
    }
    if (!Http::HeaderUtility::headerValueIsValid(value)) {
      return invalid(absl::StrCat("header '", name, "' has an invalid value"));
    }
  }
  for (const std::string& name : layout.request_headers_to_remove()) {
    if (absl::Status status = validateHeaderName(name); !status.ok()) {
      return status;
    }
  }
  return absl::OkStatus();
}

absl::Status validateBodyFields(const EndpointTemplate& layout) {
  for (const auto& [name, value] : layout.set_body_fields().fields()) {
    if (name.empty()) {
      return invalid("set_body_fields must not have an empty field name");
    }
  }
  for (const std::string& name : layout.remove_body_fields()) {
    if (name.empty()) {
      return invalid("remove_body_fields must not have an empty field name");
    }
  }
  return absl::OkStatus();
}

absl::Status validateTemplate(const Endpoint& endpoint) {
  const EndpointTemplate& layout = endpoint.custom();
  absl::flat_hash_set<std::string> placeholders;
  if (absl::Status status = validatePathTemplate(layout, placeholders); !status.ok()) {
    return status;
  }
  if (absl::Status status = validatePlacements(layout, placeholders); !status.ok()) {
    return status;
  }
  absl::flat_hash_set<std::string> used;
  for (const std::string& name : placeholders) {
    if (isReservedPlaceholder(name)) {
      continue;
    }
    if (!endpoint.variables().contains(name)) {
      return invalid(absl::StrCat("path_template names no variable '", name, "'"));
    }
    used.insert(name);
  }
  if (absl::Status status = validateQueryParams(endpoint, used); !status.ok()) {
    return status;
  }
  for (const auto& [name, value] : endpoint.variables()) {
    if (!used.contains(name)) {
      return invalid(absl::StrCat("variable '", name, "' is not used by the template"));
    }
  }
  if (absl::Status status = validateHeaders(layout); !status.ok()) {
    return status;
  }
  return validateBodyFields(layout);
}

bool contains(const std::vector<std::string>& names, absl::string_view name) {
  for (const std::string& candidate : names) {
    if (candidate == name) {
      return true;
    }
  }
  return false;
}

absl::Status validatePreset(const Endpoint& endpoint, ProtoLLMProtocol protocol) {
  const EndpointPreset* preset = findEndpointPreset(endpoint.preset());
  if (preset == nullptr) {
    return invalid(absl::StrCat("unknown endpoint preset '", endpoint.preset(), "'"));
  }
  if (preset->layout(protocol) == nullptr) {
    return invalid(absl::StrCat("endpoint preset '", preset->name, "' does not serve ",
                                envoy::type::ai::v3::LLMProtocol_Name(protocol)));
  }
  for (const std::string& name : preset->required_variables) {
    if (!endpoint.variables().contains(name)) {
      return invalid(
          absl::StrCat("endpoint preset '", preset->name, "' requires variable '", name, "'"));
    }
  }
  for (const auto& [name, value] : endpoint.variables()) {
    if (!contains(preset->required_variables, name) &&
        !contains(preset->optional_variables, name)) {
      return invalid(
          absl::StrCat("endpoint preset '", preset->name, "' takes no variable '", name, "'"));
    }
  }
  return absl::OkStatus();
}

} // namespace

const envoy::type::ai::v3::EndpointTemplate*
EndpointPreset::layout(envoy::type::ai::v3::LLMProtocol protocol) const {
  for (const auto& [served, layout] : layouts) {
    if (served == protocol) {
      return &layout;
    }
  }
  return nullptr;
}

const std::vector<EndpointPreset>& endpointPresets() {
  CONSTRUCT_ON_FIRST_USE(std::vector<EndpointPreset>, buildPresets());
}

const EndpointPreset* findEndpointPreset(absl::string_view name) {
  for (const EndpointPreset& preset : endpointPresets()) {
    if (preset.name == name) {
      return &preset;
    }
  }
  return nullptr;
}

absl::Status validateEndpoint(const Endpoint& endpoint, ProtoLLMProtocol protocol) {
  if (absl::Status status = validateVariables(endpoint); !status.ok()) {
    return status;
  }
  switch (endpoint.layout_case()) {
  case Endpoint::kPreset:
    return validatePreset(endpoint, protocol);
  case Endpoint::kCustom:
    return validateTemplate(endpoint);
  case Endpoint::LAYOUT_NOT_SET:
    break;
  }
  return invalid("endpoint needs a preset or a template");
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
