#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"

#include <cmath>
#include <cstdint>
#include <utility>

#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/status/status.h"
#include "absl/strings/ascii.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

using MutationProto = envoy::type::ai::v3::LLMBinding::Mutation;

constexpr absl::string_view kModelField = "model";
constexpr absl::string_view kStreamField = "stream";

nlohmann::json toJson(const Protobuf::Value& value) {
  switch (value.kind_case()) {
  case Protobuf::Value::kNumberValue: {
    // JSON numbers arrive as doubles; integral ones stay integers, as `max_tokens` must.
    const double number = value.number_value();
    if (std::trunc(number) == number && std::abs(number) < 9007199254740992.0) {
      return static_cast<int64_t>(number);
    }
    return number;
  }
  case Protobuf::Value::kStringValue:
    return value.string_value();
  case Protobuf::Value::kBoolValue:
    return value.bool_value();
  case Protobuf::Value::kStructValue: {
    nlohmann::json object = nlohmann::json::object();
    for (const auto& [name, field] : value.struct_value().fields()) {
      object[name] = toJson(field);
    }
    return object;
  }
  case Protobuf::Value::kListValue: {
    nlohmann::json array = nlohmann::json::array();
    for (const Protobuf::Value& element : value.list_value().values()) {
      array.push_back(toJson(element));
    }
    return array;
  }
  case Protobuf::Value::kNullValue:
  case Protobuf::Value::KIND_NOT_SET:
    break;
  }
  return nullptr;
}

absl::StatusOr<LlmBinding::Mutation> toMutation(const MutationProto& proto) {
  LlmBinding::Mutation mutation;
  switch (proto.target_case()) {
  case MutationProto::kBodyField:
    if (proto.body_field().empty()) {
      return absl::InvalidArgumentError("a binding mutation names an empty body field");
    }
    mutation.name = proto.body_field();
    break;
  case MutationProto::kHeader:
    // `:path` and the other pseudo-headers belong to the URI pattern and the route.
    if (absl::StartsWith(proto.header(), ":") ||
        !Http::HeaderUtility::headerNameIsValid(proto.header())) {
      return absl::InvalidArgumentError(
          absl::StrCat("a binding mutation names an invalid header `", proto.header(), "`"));
    }
    if (proto.has_value() && proto.value().kind_case() != Protobuf::Value::kStringValue) {
      return absl::InvalidArgumentError(
          absl::StrCat("binding header `", proto.header(), "` takes a string value"));
    }
    mutation.target = LlmBinding::Mutation::Target::Header;
    mutation.name = absl::AsciiStrToLower(proto.header());
    break;
  case MutationProto::TARGET_NOT_SET:
    return absl::InvalidArgumentError("a binding mutation names neither a body field nor a header");
  }
  if (proto.has_value()) {
    mutation.value = toJson(proto.value());
  }
  mutation.keep_existing = proto.keep_existing();
  return mutation;
}

bool removesBodyField(absl::Span<const LlmBinding::Mutation> mutations, absl::string_view field) {
  for (const LlmBinding::Mutation& mutation : mutations) {
    if (mutation.target == LlmBinding::Mutation::Target::BodyField && mutation.name == field &&
        !mutation.value.has_value()) {
      return true;
    }
  }
  return false;
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
    text = text.substr(close + 1);
  }
}

} // namespace

absl::StatusOr<LlmBindingConstSharedPtr>
LlmBinding::create(const envoy::type::ai::v3::LLMBinding& proto) {
  auto binding = std::make_shared<LlmBinding>();
  if (!proto.uri_pattern().empty()) {
    absl::StatusOr<UriPattern> pattern = UriPattern::parse(proto.uri_pattern());
    if (!pattern.ok()) {
      return pattern.status();
    }
    binding->pattern = *std::move(pattern);
  }
  binding->mutations.reserve(proto.mutations_size());
  for (const MutationProto& mutation : proto.mutations()) {
    absl::StatusOr<Mutation> converted = toMutation(mutation);
    if (!converted.ok()) {
      return converted.status();
    }
    binding->mutations.push_back(*std::move(converted));
  }
  return LlmBindingConstSharedPtr(std::move(binding));
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

const UriPattern* LlmBinding::patternOver(const LlmBinding* api_binding) const {
  if (pattern.has_value()) {
    return &*pattern;
  }
  return api_binding != nullptr && api_binding->pattern.has_value() ? &*api_binding->pattern
                                                                    : nullptr;
}

absl::Span<const LlmBinding::Mutation>
LlmBinding::mutationsOver(const LlmBinding* api_binding) const {
  return mutations.empty() && api_binding != nullptr ? api_binding->mutations : mutations;
}

std::optional<LlmBindingRead> readRequest(const LlmBindingConstSharedPtr& binding,
                                          const LlmBinding* api_binding, absl::string_view path,
                                          nlohmann::json& body) {
  LlmBindingRead read{binding};
  const absl::Span<const LlmBinding::Mutation> mutations = binding->mutationsOver(api_binding);
  if (const UriPattern* pattern = binding->patternOver(api_binding); pattern != nullptr) {
    std::optional<UriPattern::Match> match = pattern->match(path);
    if (!match.has_value()) {
      return std::nullopt;
    }
    // A field the binding removes has the path as its only home, so the path's value wins.
    if (body.is_object()) {
      if (!match->model.empty() &&
          (removesBodyField(mutations, kModelField) || !body.contains(kModelField))) {
        body[kModelField] = match->model;
      }
      if (removesBodyField(mutations, kStreamField)) {
        body[kStreamField] = match->stream;
      } else if (match->stream && !body.contains(kStreamField)) {
        body[kStreamField] = true;
      }
    }
    read.model = std::move(match->model);
    read.path_variables = std::move(match->variables);
  }
  if (body.is_object()) {
    for (const LlmBinding::Mutation& mutation : mutations) {
      if (mutation.target != LlmBinding::Mutation::Target::BodyField ||
          !mutation.value.has_value()) {
        continue;
      }
      const auto field = body.find(mutation.name);
      if (field == body.end()) {
        continue;
      }
      read.written_fields.emplace(mutation.name, std::move(*field));
      body.erase(field);
    }
  }
  return read;
}

absl::StatusOr<LlmBindingWrite> writeRequest(const LlmBinding& binding,
                                             const LlmBinding* api_binding,
                                             const LlmBindingRead* arrived, nlohmann::json& body) {
  const auto value = [&body, arrived](absl::string_view name) -> std::optional<std::string> {
    if (const auto field = body.find(name); field != body.end() && field->is_string()) {
      return field->get<std::string>();
    }
    if (arrived != nullptr) {
      if (const auto variable = arrived->path_variables.find(name);
          variable != arrived->path_variables.end()) {
        return variable->second;
      }
    }
    return std::nullopt;
  };

  LlmBindingWrite write;
  if (const UriPattern* pattern = binding.patternOver(api_binding); pattern != nullptr) {
    if (pattern->hasModel()) {
      const auto model = body.find(kModelField);
      if (model == body.end() || !model->is_string() ||
          !isPathToken(model->get_ref<const std::string&>())) {
        return absl::InvalidArgumentError(absl::StrCat("the request path names the model, so `",
                                                       kModelField, "` must be a model id"));
      }
    }
    const auto stream = body.find(kStreamField);
    absl::StatusOr<std::string> path =
        pattern->render(stream != body.end() && stream->is_boolean() && stream->get<bool>(), value);
    if (!path.ok()) {
      return path.status();
    }
    write.path = *std::move(path);
  }

  // Every value is filled before any mutation applies, so all of them read the rendered body.
  const absl::Span<const LlmBinding::Mutation> mutations = binding.mutationsOver(api_binding);
  const bool same_binding = arrived != nullptr && arrived->binding.get() == &binding;
  std::vector<std::optional<nlohmann::json>> values;
  values.reserve(mutations.size());
  for (const LlmBinding::Mutation& mutation : mutations) {
    if (!mutation.value.has_value()) {
      values.emplace_back();
      continue;
    }
    if (same_binding && mutation.target == LlmBinding::Mutation::Target::BodyField) {
      if (const auto written = arrived->written_fields.find(mutation.name);
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

  for (size_t i = 0; i < mutations.size(); ++i) {
    const LlmBinding::Mutation& mutation = mutations[i];
    std::optional<nlohmann::json>& filled = values[i];
    if (mutation.target == LlmBinding::Mutation::Target::Header) {
      write.headers.push_back(
          {mutation.name,
           filled.has_value() ? std::make_optional(filled->get<std::string>()) : std::nullopt,
           mutation.keep_existing});
      continue;
    }
    if (!body.is_object()) {
      continue;
    }
    if (!filled.has_value()) {
      body.erase(mutation.name);
    } else if (!mutation.keep_existing || !body.contains(mutation.name)) {
      body[mutation.name] = *std::move(filled);
    }
  }
  return write;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
