#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"

#include <iterator>
#include <utility>

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

constexpr absl::string_view ModelField = "model";
constexpr absl::string_view StreamField = "stream";

// Headers a binding may not write: the pseudo-headers are the connection's and the path is the
// pattern's; the rest describe the body Envoy frames itself.
bool isReservedHeader(absl::string_view name) {
  return absl::StartsWith(name, ":") || name == "host" || name == "content-length" ||
         name == "transfer-encoding";
}

// Checks a header mutation as `create()` builds it: the name is one Envoy will send, lower-case
// so it matches what the request carries, and the value, when set, is text.
absl::Status validateHeaderMutation(const LlmBinding::Mutation& mutation) {
  if (isReservedHeader(mutation.header)) {
    return absl::InvalidArgumentError(
        absl::StrCat("a binding mutation cannot write the `", mutation.header, "` header"));
  }
  for (const char c : mutation.header) {
    if (absl::ascii_isupper(c) || absl::ascii_isspace(c)) {
      return absl::InvalidArgumentError(absl::StrCat(
          "a binding mutation names the header `", mutation.header, "`, which must be lower-case"));
    }
  }
  if (mutation.value.has_value() && !mutation.value->is_string()) {
    return absl::InvalidArgumentError(absl::StrCat("a binding mutation writes the `",
                                                   mutation.header,
                                                   "` header with a value that is not a string"));
  }
  return absl::OkStatus();
}

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
    absl::StrAppend(&filled, text.substr(0, open), *variable);
    text.remove_prefix(close + 1);
  }
}

// The pattern `binding` reads and renders: its own, or `api_binding`'s when it has none. Null
// when neither has one.
const UriPattern* patternOf(const LlmBinding& binding, const LlmBinding* api_binding) {
  if (binding.pattern().has_value()) {
    return &*binding.pattern();
  }
  return api_binding != nullptr && api_binding->pattern().has_value() ? &*api_binding->pattern()
                                                                      : nullptr;
}

// The binding whose mutations apply: `binding`, or `api_binding` when `binding` has none.
const LlmBinding& editsOf(const LlmBinding& binding, const LlmBinding* api_binding) {
  return binding.mutations().empty() && api_binding != nullptr ? *api_binding : binding;
}

std::optional<LlmBindingRead> readRequest(const LlmBindingConstSharedPtr& binding,
                                          const LlmBinding* api_binding, absl::string_view path,
                                          nlohmann::json& body, const HeaderLookup& header) {
  LlmBindingRead read{binding};
  const LlmBinding& edits = editsOf(*binding, api_binding);
  if (const UriPattern* pattern = patternOf(*binding, api_binding); pattern != nullptr) {
    std::optional<UriPattern::Match> match = pattern->match(path);
    if (!match.has_value()) {
      return std::nullopt;
    }
    // A field the binding removes has the path as its only home, so the path's value wins. A path
    // that does not name the streaming mode leaves the body's.
    if (body.is_object()) {
      if (!match->model.empty() && (edits.removesModel() || !body.contains(ModelField))) {
        body[ModelField] = match->model;
      }
      if (match->stream.has_value()) {
        if (edits.removesStream()) {
          body[StreamField] = *match->stream;
        } else if (*match->stream && !body.contains(StreamField)) {
          body[StreamField] = true;
        }
      }
    }
    read.model = std::move(match->model);
    read.path_variables = std::move(match->variables);
  }
  // What the binding writes is the provider's, not the API's: taken out of the body here, and
  // recorded for the caller to take out of the headers, so neither reaches another provider.
  for (const LlmBinding::Mutation& mutation : edits.mutations()) {
    if (!mutation.value.has_value()) {
      continue;
    }
    if (mutation.isHeader()) {
      if (std::optional<std::string> sent = header(mutation.header); sent.has_value()) {
        read.written_headers.emplace(mutation.header, *std::move(sent));
      }
      continue;
    }
    if (!body.is_object()) {
      continue;
    }
    const auto field = body.find(mutation.body_field);
    if (field == body.end()) {
      continue;
    }
    read.written_fields.emplace(mutation.body_field, std::move(*field));
    body.erase(field);
  }
  return read;
}

} // namespace

LlmBinding::LlmBinding(std::optional<UriPattern>&& pattern, std::vector<Mutation>&& mutations)
    : pattern_(std::move(pattern)), mutations_(std::move(mutations)) {
  for (const Mutation& mutation : mutations_) {
    if (!mutation.value.has_value() && !mutation.isHeader()) {
      removes_model_ |= mutation.body_field == ModelField;
      removes_stream_ |= mutation.body_field == StreamField;
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
    Mutation converted;
    switch (mutation.target_case()) {
    case envoy::type::ai::v3::LLMBinding::Mutation::kBodyField:
      converted.body_field = mutation.body_field();
      break;
    case envoy::type::ai::v3::LLMBinding::Mutation::kHeader:
      converted.header = mutation.header();
      break;
    case envoy::type::ai::v3::LLMBinding::Mutation::TARGET_NOT_SET:
      break;
    }
    if (converted.body_field.empty() && converted.header.empty()) {
      return absl::InvalidArgumentError("a binding mutation names no body field or header");
    }
    if (mutation.has_value()) {
      absl::StatusOr<std::optional<nlohmann::json>> value = toJson(mutation.value());
      if (!value.ok()) {
        return value.status();
      }
      converted.value = *std::move(value);
    }
    converted.keep_existing = mutation.keep_existing();
    if (converted.isHeader()) {
      if (const absl::Status status = validateHeaderMutation(converted); !status.ok()) {
        return status;
      }
    }
    mutations.push_back(std::move(converted));
  }
  return std::make_shared<const LlmBinding>(std::move(pattern), std::move(mutations));
}

absl::StatusOr<LlmBindingConstSharedPtr> LlmBinding::parse(absl::string_view text) {
  text = absl::StripAsciiWhitespace(text);
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

std::optional<LlmBindingRead>
readClientPath(const LlmBindingConstSharedPtr& binding, const LlmBindingConstSharedPtr& api_binding,
               absl::Span<const LlmBindingConstSharedPtr> client_bindings, absl::string_view path,
               nlohmann::json& body, const HeaderLookup& header) {
  if (binding != nullptr) {
    return readRequest(binding, api_binding.get(), path, body, header);
  }
  if (api_binding != nullptr) {
    if (std::optional<LlmBindingRead> read = readRequest(api_binding, nullptr, path, body, header);
        read.has_value()) {
      return read;
    }
  }
  for (const LlmBindingConstSharedPtr& client_binding : client_bindings) {
    if (std::optional<LlmBindingRead> read =
            readRequest(client_binding, nullptr, path, body, header);
        read.has_value()) {
      return read;
    }
  }
  return std::nullopt;
}

absl::StatusOr<std::optional<std::string>>
writeRequest(const LlmBinding& binding, const LlmBinding* api_binding,
             const LlmBindingRead* arrived, nlohmann::json& body, const HeaderLookup& header,
             std::vector<HeaderMutation>& headers, bool& unresolved) {
  unresolved = false;
  // `{model}` reads the body as rendered, before any mutation removes it.
  std::optional<std::string> model;
  if (const auto field = body.find(ModelField); field != body.end() && field->is_string()) {
    model = field->get<std::string>();
  }
  const auto value = [&model, arrived, &unresolved](absl::string_view name) {
    if (name == ModelField) {
      return model;
    }
    if (arrived != nullptr) {
      if (const auto variable = arrived->path_variables.find(name);
          variable != arrived->path_variables.end()) {
        return std::make_optional(variable->second);
      }
    }
    unresolved = true;
    return std::optional<std::string>();
  };

  std::optional<std::string> path;
  if (const UriPattern* pattern = patternOf(binding, api_binding); pattern != nullptr) {
    if (!pattern->renderable()) {
      unresolved = true;
      return absl::FailedPreconditionError(absl::StrCat(
          "uri pattern `", pattern->source(), "` has a wildcard, which cannot be rendered"));
    }
    if (pattern->hasModel() && (!model.has_value() || !isPathToken(*model))) {
      return absl::InvalidArgumentError(absl::StrCat("the request path names the model, so `",
                                                     ModelField, "` must be a model id"));
    }
    const auto stream = body.find(StreamField);
    absl::StatusOr<std::string> rendered =
        pattern->render(stream != body.end() && stream->is_boolean() && stream->get<bool>(), value);
    if (!rendered.ok()) {
      return rendered.status();
    }
    path = *std::move(rendered);
  }

  // The filter hands back the arrived binding itself when the request leaves in it.
  const bool same_binding = arrived != nullptr && arrived->binding.get() == &binding;
  // Each mutation is decided before its placeholders are filled: one that keeps an existing field
  // or restores the client's value needs none. Header edits are collected for the caller, which
  // owns the headers; they are complete only once every mutation has been decided, so a failure
  // part way through leaves `headers` untouched.
  std::vector<HeaderMutation> header_edits;
  for (const LlmBinding::Mutation& mutation : editsOf(binding, api_binding).mutations()) {
    if (mutation.isHeader()) {
      if (!mutation.value.has_value()) {
        header_edits.push_back({mutation.header, std::nullopt});
        continue;
      }
      if (mutation.keep_existing && header(mutation.header).has_value()) {
        continue;
      }
      if (same_binding) {
        if (const auto written = arrived->written_headers.find(mutation.header);
            written != arrived->written_headers.end()) {
          header_edits.push_back({mutation.header, written->second});
          continue;
        }
      }
      absl::StatusOr<std::string> filled =
          fillPlaceholders(mutation.value->get_ref<const std::string&>(), value);
      if (!filled.ok()) {
        return filled.status();
      }
      header_edits.push_back({mutation.header, *std::move(filled)});
      continue;
    }
    if (!body.is_object()) {
      continue;
    }
    if (!mutation.value.has_value()) {
      body.erase(mutation.body_field);
      continue;
    }
    if (mutation.keep_existing && body.contains(mutation.body_field)) {
      continue;
    }
    if (same_binding) {
      if (const auto written = arrived->written_fields.find(mutation.body_field);
          written != arrived->written_fields.end()) {
        body[mutation.body_field] = written->second;
        continue;
      }
    }
    if (!mutation.value->is_string()) {
      body[mutation.body_field] = *mutation.value;
      continue;
    }
    absl::StatusOr<std::string> filled =
        fillPlaceholders(mutation.value->get_ref<const std::string&>(), value);
    if (!filled.ok()) {
      return filled.status();
    }
    body[mutation.body_field] = *std::move(filled);
  }
  headers.insert(headers.end(), std::make_move_iterator(header_edits.begin()),
                 std::make_move_iterator(header_edits.end()));
  return path;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
