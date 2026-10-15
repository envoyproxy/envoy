#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"

#include <utility>

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

constexpr absl::string_view kModelField = "model";
constexpr absl::string_view kStreamField = "stream";

// `value` as JSON; nullopt for a null, which removes the field as an unset value does.
absl::StatusOr<std::optional<nlohmann::json>> toJson(const Protobuf::Value& value) {
  if (value.kind_case() == Protobuf::Value::kNullValue ||
      value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
    return std::nullopt;
  }
  const absl::StatusOr<std::string> text = MessageUtil::getJsonStringFromMessage(value);
  if (!text.ok()) {
    return text.status();
  }
  return nlohmann::json::parse(*text);
}

// The names of the `{name}` placeholders in `text`.
std::vector<absl::string_view> placeholderNames(absl::string_view text) {
  std::vector<absl::string_view> names;
  while (true) {
    const size_t open = text.find('{');
    const size_t close = open == absl::string_view::npos ? open : text.find('}', open);
    if (close == absl::string_view::npos) {
      return names;
    }
    names.push_back(text.substr(open + 1, close - open - 1));
    text.remove_prefix(close + 1);
  }
}

// `text` with each `{name}` replaced by its value.
absl::StatusOr<std::string> fillPlaceholders(absl::string_view text,
                                             UriPattern::ValueLookup value) {
  std::string filled;
  while (true) {
    const size_t open = text.find('{');
    const size_t close = open == absl::string_view::npos ? open : text.find('}', open);
    if (close == absl::string_view::npos) {
      absl::StrAppend(&filled, text);
      return filled;
    }
    const absl::string_view name = text.substr(open + 1, close - open - 1);
    const std::optional<std::string> variable = value(name);
    if (!variable.has_value()) {
      return absl::FailedPreconditionError(
          absl::StrCat("a binding mutation has no value for `{", name, "}`"));
    }
    if (!isPathToken(*variable)) {
      return absl::InvalidArgumentError(
          absl::StrCat("`", *variable, "` cannot fill `{", name, "}` in a binding mutation"));
    }
    absl::StrAppend(&filled, text.substr(0, open), *variable);
    text.remove_prefix(close + 1);
  }
}

} // namespace

LlmBinding::LlmBinding(std::optional<UriPattern> pattern, std::vector<Mutation> mutations)
    : pattern_(std::move(pattern)), mutations_(std::move(mutations)) {
  for (const Mutation& mutation : mutations_) {
    if (!mutation.value.has_value()) {
      removes_model_ |= mutation.body_field == kModelField;
      removes_stream_ |= mutation.body_field == kStreamField;
    } else if (mutation.value->is_string()) {
      for (absl::string_view name :
           placeholderNames(mutation.value->get_ref<const std::string&>())) {
        if (name != kModelField) {
          placeholders_.emplace_back(name);
        }
      }
    }
  }
}

absl::StatusOr<LlmBindingConstSharedPtr>
LlmBinding::create(const envoy::type::ai::v3::LLMBinding& proto) {
  std::optional<UriPattern> pattern;
  if (!proto.uri_pattern().empty()) {
    absl::StatusOr<UriPattern> parsed = UriPattern::parse(proto.uri_pattern());
    if (!parsed.ok()) {
      return parsed.status();
    }
    pattern = *std::move(parsed);
  }
  std::vector<Mutation> mutations;
  mutations.reserve(proto.mutations_size());
  for (const envoy::type::ai::v3::LLMBinding::Mutation& mutation : proto.mutations()) {
    if (mutation.body_field().empty()) {
      return absl::InvalidArgumentError("a binding mutation names no body field");
    }
    Mutation converted{mutation.body_field()};
    if (mutation.has_value()) {
      absl::StatusOr<std::optional<nlohmann::json>> value = toJson(mutation.value());
      if (!value.ok()) {
        return value.status();
      }
      converted.value = *std::move(value);
    }
    converted.keep_existing = mutation.keep_existing();
    mutations.push_back(std::move(converted));
  }
  return std::make_shared<const LlmBinding>(std::move(pattern), std::move(mutations));
}

absl::StatusOr<LlmBindingConstSharedPtr> LlmBinding::parse(absl::string_view text) {
  envoy::type::ai::v3::LLMBinding proto;
  if (absl::StartsWith(text, "/")) {
    proto.set_uri_pattern(std::string(text));
  } else if (absl::StartsWith(text, "{")) {
    bool has_unknown_field;
    if (const absl::Status status =
            MessageUtil::loadFromJsonNoThrow(text, proto, has_unknown_field);
        !status.ok()) {
      return absl::InvalidArgumentError(absl::StrCat("binding JSON: ", status.message()));
    }
  } else {
    return absl::InvalidArgumentError(
        "a binding is a URI pattern, starting with `/`, or an LLMBinding in JSON");
  }
  return create(proto);
}

const UriPattern* patternOf(const LlmBinding& binding, const LlmBinding* api_binding) {
  if (binding.pattern().has_value()) {
    return &*binding.pattern();
  }
  return api_binding != nullptr && api_binding->pattern().has_value() ? &*api_binding->pattern()
                                                                      : nullptr;
}

const LlmBinding& editsOf(const LlmBinding& binding, const LlmBinding* api_binding) {
  return binding.mutations().empty() && api_binding != nullptr ? *api_binding : binding;
}

std::optional<LlmBindingRead> readRequest(const LlmBindingConstSharedPtr& binding,
                                          const LlmBinding* api_binding, absl::string_view path,
                                          nlohmann::json& body) {
  LlmBindingRead read{binding};
  const LlmBinding& edits = editsOf(*binding, api_binding);
  if (const UriPattern* pattern = patternOf(*binding, api_binding); pattern != nullptr) {
    std::optional<UriPattern::Match> match = pattern->match(path);
    if (!match.has_value()) {
      return std::nullopt;
    }
    // A field the binding removes has the path as its only home, so the path's value wins.
    if (body.is_object()) {
      if (!match->model.empty() && (edits.removesModel() || !body.contains(kModelField))) {
        body[kModelField] = match->model;
      }
      if (edits.removesStream()) {
        body[kStreamField] = match->stream;
      } else if (match->stream && !body.contains(kStreamField)) {
        body[kStreamField] = true;
      }
    }
    read.model = std::move(match->model);
    read.path_variables = std::move(match->variables);
  }
  if (body.is_object()) {
    for (const LlmBinding::Mutation& mutation : edits.mutations()) {
      if (!mutation.value.has_value()) {
        continue;
      }
      const auto field = body.find(mutation.body_field);
      if (field == body.end()) {
        continue;
      }
      read.written_fields.emplace(mutation.body_field, std::move(*field));
      body.erase(field);
    }
  }
  return read;
}

std::optional<LlmBindingRead>
readClientPath(const LlmBindingConstSharedPtr& binding, const LlmBindingConstSharedPtr& api_binding,
               absl::Span<const LlmBindingConstSharedPtr> client_bindings, absl::string_view path,
               nlohmann::json& body) {
  if (binding != nullptr) {
    return readRequest(binding, api_binding.get(), path, body);
  }
  if (api_binding != nullptr) {
    if (std::optional<LlmBindingRead> read = readRequest(api_binding, nullptr, path, body);
        read.has_value()) {
      return read;
    }
  }
  for (const LlmBindingConstSharedPtr& client_binding : client_bindings) {
    if (std::optional<LlmBindingRead> read = readRequest(client_binding, nullptr, path, body);
        read.has_value()) {
      return read;
    }
  }
  return std::nullopt;
}

absl::Status checkResolvable(const LlmBinding& binding, const LlmBinding* api_binding,
                             const LlmBindingRead* arrived) {
  const auto supplied = [arrived](absl::string_view name) {
    return name == kModelField || (arrived != nullptr && arrived->path_variables.contains(name));
  };
  if (const UriPattern* pattern = patternOf(binding, api_binding); pattern != nullptr) {
    if (!pattern->renderable()) {
      return absl::FailedPreconditionError(absl::StrCat(
          "uri pattern `", pattern->source(), "` has a wildcard, which cannot be rendered"));
    }
    for (const std::string& name : pattern->renderVariables()) {
      if (!supplied(name)) {
        return absl::FailedPreconditionError(absl::StrCat("uri pattern `", pattern->source(),
                                                          "` has no value for `{", name,
                                                          "}`: the client's path names none"));
      }
    }
  }
  for (const std::string& name : editsOf(binding, api_binding).placeholders()) {
    if (!supplied(name)) {
      return absl::FailedPreconditionError(absl::StrCat("a binding mutation has no value for `{",
                                                        name, "}`: the client's path names none"));
    }
  }
  return absl::OkStatus();
}

absl::StatusOr<std::optional<std::string>> writeRequest(const LlmBinding& binding,
                                                        const LlmBinding* api_binding,
                                                        const LlmBindingRead* arrived,
                                                        nlohmann::json& body) {
  const auto value = [&body, arrived](absl::string_view name) -> std::optional<std::string> {
    if (name == kModelField) {
      const auto model = body.find(kModelField);
      if (model != body.end() && model->is_string()) {
        return model->get<std::string>();
      }
      return std::nullopt;
    }
    if (arrived != nullptr) {
      if (const auto variable = arrived->path_variables.find(name);
          variable != arrived->path_variables.end()) {
        return variable->second;
      }
    }
    return std::nullopt;
  };

  std::optional<std::string> path;
  if (const UriPattern* pattern = patternOf(binding, api_binding); pattern != nullptr) {
    if (pattern->hasModel()) {
      const auto model = body.find(kModelField);
      if (model == body.end() || !model->is_string() ||
          !isPathToken(model->get_ref<const std::string&>())) {
        return absl::InvalidArgumentError(absl::StrCat("the request path names the model, so `",
                                                       kModelField, "` must be a model id"));
      }
    }
    const auto stream = body.find(kStreamField);
    absl::StatusOr<std::string> rendered =
        pattern->render(stream != body.end() && stream->is_boolean() && stream->get<bool>(), value);
    if (!rendered.ok()) {
      return rendered.status();
    }
    path = *std::move(rendered);
  }

  // The filter hands back the arrived binding itself when the request leaves in it.
  const bool same_binding = arrived != nullptr && arrived->binding.get() == &binding;
  const std::vector<LlmBinding::Mutation>& mutations = editsOf(binding, api_binding).mutations();
  // Every value is filled before any mutation applies, so all of them read the rendered body.
  std::vector<std::optional<nlohmann::json>> values;
  values.reserve(mutations.size());
  for (const LlmBinding::Mutation& mutation : mutations) {
    if (!mutation.value.has_value()) {
      values.emplace_back();
      continue;
    }
    if (same_binding) {
      if (const auto written = arrived->written_fields.find(mutation.body_field);
          written != arrived->written_fields.end()) {
        values.emplace_back(written->second);
        continue;
      }
    }
    if (!mutation.value->is_string()) {
      values.emplace_back(*mutation.value);
      continue;
    }
    absl::StatusOr<std::string> filled =
        fillPlaceholders(mutation.value->get_ref<const std::string&>(), value);
    if (!filled.ok()) {
      return filled.status();
    }
    values.emplace_back(*std::move(filled));
  }

  if (!body.is_object()) {
    return path;
  }
  for (size_t i = 0; i < mutations.size(); ++i) {
    const LlmBinding::Mutation& mutation = mutations[i];
    if (!values[i].has_value()) {
      body.erase(mutation.body_field);
    } else if (!mutation.keep_existing || !body.contains(mutation.body_field)) {
      body[mutation.body_field] = *std::move(values[i]);
    }
  }
  return path;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
