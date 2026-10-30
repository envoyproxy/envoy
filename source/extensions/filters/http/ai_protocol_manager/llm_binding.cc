#include "source/extensions/filters/http/ai_protocol_manager/llm_binding.h"

#include <iterator>
#include <utility>

#include "source/common/http/header_utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

#include "absl/algorithm/container.h"
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

// Checks whether an HTTP header name is a protected protocol/framing header that binding mutations
// are not allowed to modify or remove.
bool isReservedHeader(absl::string_view name) {
  return absl::StartsWith(name, ":") || name == "host" || name == "content-length" ||
         name == "transfer-encoding";
}

// Validates that a header mutation is well-formed by checking three rules:
// - Not a reserved header: Rejects pseudo-headers (`:*`), `host`, `content-length`, and
//   `transfer-encoding` via `isReservedHeader`.
// - A lower-case RFC 7230 token: the header map keys by lower-case name, and a binding can
//   arrive as JSON in filter state, which PGV never sees, so the RFC check happens here.
// - String value (if set): If `value` is provided (rather than unset/null to remove the header),
//   verifies that the value is a string. Its bytes are checked once placeholders are filled.
absl::Status validateHeaderMutation(const LlmBinding::Mutation& mutation) {
  if (isReservedHeader(mutation.header)) {
    return absl::InvalidArgumentError(
        absl::StrCat("a binding mutation cannot write the `", mutation.header, "` header"));
  }
  if (!Http::HeaderUtility::headerNameIsValid(mutation.header) ||
      absl::c_any_of(mutation.header, absl::ascii_isupper)) {
    return absl::InvalidArgumentError(
        absl::StrCat("a binding mutation names the header `", mutation.header,
                     "`, which must be a lower-case HTTP header name"));
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

// Only these header mutations are accepted: a removal, or a write with `ADD_IF_ABSENT` or
// `OVERWRITE_IF_EXISTS_OR_ADD`. The enum's default, `APPEND_IF_EXISTS_OR_ADD`, is rejected too, so
// the action is always named: a binding that spells out what it means keeps meaning it if more
// is accepted later.
absl::StatusOr<LlmBinding::Mutation>
headerMutation(const envoy::config::common::mutation_rules::v3::HeaderMutation& proto) {
  using envoy::config::core::v3::HeaderValueOption;
  LlmBinding::Mutation mutation;
  switch (proto.action_case()) {
  case envoy::config::common::mutation_rules::v3::HeaderMutation::kRemove:
    mutation.header = proto.remove();
    break;
  case envoy::config::common::mutation_rules::v3::HeaderMutation::kAppend: {
    const HeaderValueOption& option = proto.append();
    mutation.header = option.header().key();
    if (option.has_append() || !option.header().raw_value().empty()) {
      return absl::InvalidArgumentError(
          absl::StrCat("a binding writes the `", mutation.header,
                       "` header with `value` and `append_action`, not `raw_value` or `append`"));
    }
    switch (option.append_action()) {
    case HeaderValueOption::ADD_IF_ABSENT:
      mutation.keep_existing = true;
      break;
    case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
      break;
    default:
      return absl::InvalidArgumentError(
          absl::StrCat("a binding cannot write the `", mutation.header, "` header with ",
                       HeaderValueOption::HeaderAppendAction_Name(option.append_action())));
    }
    mutation.value = nlohmann::json(option.header().value());
    break;
  }
  default:
    return absl::InvalidArgumentError("a binding's header mutation is a `remove` or an `append`");
  }
  if (mutation.header.empty()) {
    return absl::InvalidArgumentError("a binding mutation names no body field or header");
  }
  if (const absl::Status status = validateHeaderMutation(mutation); !status.ok()) {
    return status;
  }
  return mutation;
}

// Only these body mutations are accepted: a removal, or a write of any JSON value with
// `ADD_IF_ABSENT` or `OVERWRITE_IF_EXISTS_OR_ADD`. The enum's default, `APPEND_IF_EXISTS_OR_ADD`,
// is rejected too, as for a header.
absl::StatusOr<LlmBinding::Mutation>
bodyMutation(const envoy::config::core::v3::KeyValueMutation& proto) {
  using envoy::config::core::v3::KeyValueAppend;
  LlmBinding::Mutation mutation;
  if (proto.has_append() == !proto.remove().empty()) {
    return absl::InvalidArgumentError(
        "a binding's body mutation sets exactly one of `append` and `remove`");
  }
  if (!proto.has_append()) {
    mutation.body_field = proto.remove();
    return mutation;
  }
  const KeyValueAppend& option = proto.append();
  if (option.has_entry()) {
    return absl::InvalidArgumentError("a binding's body mutation writes `record`, not `entry`");
  }
  mutation.body_field = option.record().key();
  if (mutation.body_field.empty()) {
    return absl::InvalidArgumentError("a binding mutation names no body field or header");
  }
  switch (option.action()) {
  case KeyValueAppend::ADD_IF_ABSENT:
    mutation.keep_existing = true;
    break;
  case KeyValueAppend::OVERWRITE_IF_EXISTS_OR_ADD:
    break;
  default:
    return absl::InvalidArgumentError(
        absl::StrCat("a binding cannot write the `", mutation.body_field, "` field with ",
                     KeyValueAppend::KeyValueAppendAction_Name(option.action())));
  }
  absl::StatusOr<std::optional<nlohmann::json>> value = toJson(option.record().value());
  if (!value.ok()) {
    return value.status();
  }
  if (!value->has_value()) {
    return absl::InvalidArgumentError(absl::StrCat("a binding writes the `", mutation.body_field,
                                                   "` field with no value; `remove` it instead"));
  }
  mutation.value = **std::move(value);
  return mutation;
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

// Whether two mutations edit the same field or header.
bool sameTarget(const LlmBinding::Mutation& a, const LlmBinding::Mutation& b) {
  return a.isHeader() == b.isHeader() &&
         (a.isHeader() ? a.header == b.header : a.body_field == b.body_field);
}

// The mutations that apply, in order: the API's own, then `binding`'s. A mutation of `binding`
// that names a field or header the API's also names stands in for it, so a binding adds to the
// API's edits -- a header on top of Gemini's does not undo its `model` and `stream` removals --
// and overrides one only by naming it.
std::vector<const LlmBinding::Mutation*> editsOf(const LlmBinding& binding,
                                                 const LlmBinding* api_binding) {
  std::vector<const LlmBinding::Mutation*> edits;
  if (api_binding != nullptr && api_binding != &binding) {
    for (const LlmBinding::Mutation& api : api_binding->mutations()) {
      if (!absl::c_any_of(binding.mutations(), [&api](const LlmBinding::Mutation& own) {
            return sameTarget(own, api);
          })) {
        edits.push_back(&api);
      }
    }
  }
  for (const LlmBinding::Mutation& own : binding.mutations()) {
    edits.push_back(&own);
  }
  return edits;
}

// Whether some edit removes the top-level body member `field`.
bool removes(const std::vector<const LlmBinding::Mutation*>& edits, absl::string_view field) {
  return absl::c_any_of(edits, [field](const LlmBinding::Mutation* mutation) {
    return !mutation->isHeader() && !mutation->value.has_value() && mutation->body_field == field;
  });
}

std::optional<LlmBindingRead> readRequest(const LlmBindingConstSharedPtr& binding,
                                          const LlmBinding* api_binding, absl::string_view path,
                                          nlohmann::json& body, const HeaderLookup& header) {
  LlmBindingRead read{binding};
  const std::vector<const LlmBinding::Mutation*> edits = editsOf(*binding, api_binding);
  if (const UriPattern* pattern = patternOf(*binding, api_binding); pattern != nullptr) {
    std::optional<UriPattern::Match> match = pattern->match(path);
    if (!match.has_value()) {
      return std::nullopt;
    }
    // A field the binding removes has the path as its only home, so the path's value wins. A path
    // that does not name the streaming mode leaves the body's.
    if (body.is_object()) {
      if (!match->model.empty() && (removes(edits, ModelField) || !body.contains(ModelField))) {
        body[ModelField] = match->model;
      }
      if (match->stream.has_value()) {
        if (removes(edits, StreamField)) {
          body[StreamField] = *match->stream;
        } else if (*match->stream && !body.contains(StreamField)) {
          body[StreamField] = true;
        }
      }
    }
    read.model = std::move(match->model);
    read.path_variables = std::move(match->variables);
  }
  // Strip provider-specific body fields and record provider-specific headers for removal by the
  // caller so they do not leak when routing to a different provider.
  for (const LlmBinding::Mutation* mutation : edits) {
    if (!mutation->value.has_value()) {
      continue;
    }
    if (mutation->isHeader()) {
      if (std::optional<std::string> sent = header(mutation->header); sent.has_value()) {
        read.written_headers.emplace(mutation->header, *std::move(sent));
      }
      continue;
    }
    if (!body.is_object()) {
      continue;
    }
    const auto field = body.find(mutation->body_field);
    if (field == body.end()) {
      continue;
    }
    read.written_fields.emplace(mutation->body_field, std::move(*field));
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
  // `header_mutations` and `body_mutations` are flattened into one list, headers then body, each
  // keeping its own order. Order across the two does not matter: a header edit never reads or
  // writes a body field, nor the reverse. Order within each does, e.g. a removal and then an
  // `ADD_IF_ABSENT` of the same header.
  std::vector<Mutation> mutations;
  mutations.reserve(proto.header_mutations_size() + proto.body_mutations_size());
  for (const envoy::config::common::mutation_rules::v3::HeaderMutation& mutation :
       proto.header_mutations()) {
    absl::StatusOr<Mutation> converted = headerMutation(mutation);
    if (!converted.ok()) {
      return converted.status();
    }
    mutations.push_back(*std::move(converted));
  }
  for (const envoy::config::core::v3::KeyValueMutation& mutation : proto.body_mutations()) {
    absl::StatusOr<Mutation> converted = bodyMutation(mutation);
    if (!converted.ok()) {
      return converted.status();
    }
    mutations.push_back(*std::move(converted));
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
  // Stage header edits locally so `headers` remains untouched if a mutation fails mid-loop.
  std::vector<HeaderMutation> header_edits;
  // Whether `name` is set once the edits staged so far apply, which is what `keep_existing` asks
  // about. The last staged edit to it decides; only a header none touched is answered by the
  // request, so a header edit sees earlier edits as a body edit does.
  const auto header_present = [&header_edits, &header](absl::string_view name) {
    for (auto edit = header_edits.rbegin(); edit != header_edits.rend(); ++edit) {
      if (edit->name == name) {
        return edit->value.has_value();
      }
    }
    return header(name).has_value();
  };
  for (const LlmBinding::Mutation* mutation : editsOf(binding, api_binding)) {
    if (mutation->isHeader()) {
      if (!mutation->value.has_value()) {
        header_edits.push_back({mutation->header, std::nullopt});
        continue;
      }
      if (mutation->keep_existing && header_present(mutation->header)) {
        continue;
      }
      if (same_binding) {
        if (const auto written = arrived->written_headers.find(mutation->header);
            written != arrived->written_headers.end()) {
          header_edits.push_back({mutation->header, written->second});
          continue;
        }
      }
      absl::StatusOr<std::string> filled =
          fillPlaceholders(mutation->value->get_ref<const std::string&>(), value);
      if (!filled.ok()) {
        return filled.status();
      }
      // Checked after filling: a placeholder takes the client's path or body, so the value is
      // only known here, and a CR, LF or NUL in it would end the header and start another.
      if (!Http::HeaderUtility::headerValueIsValid(*filled)) {
        return absl::InvalidArgumentError(absl::StrCat(
            "a binding mutation writes the `", mutation->header, "` header with an invalid value"));
      }
      header_edits.push_back({mutation->header, *std::move(filled)});
      continue;
    }
    if (!body.is_object()) {
      continue;
    }
    if (!mutation->value.has_value()) {
      body.erase(mutation->body_field);
      continue;
    }
    if (mutation->keep_existing && body.contains(mutation->body_field)) {
      continue;
    }
    if (same_binding) {
      if (const auto written = arrived->written_fields.find(mutation->body_field);
          written != arrived->written_fields.end()) {
        body[mutation->body_field] = written->second;
        continue;
      }
    }
    if (!mutation->value->is_string()) {
      body[mutation->body_field] = *mutation->value;
      continue;
    }
    absl::StatusOr<std::string> filled =
        fillPlaceholders(mutation->value->get_ref<const std::string&>(), value);
    if (!filled.ok()) {
      return filled.status();
    }
    body[mutation->body_field] = *std::move(filled);
  }
  headers.insert(headers.end(), std::make_move_iterator(header_edits.begin()),
                 std::make_move_iterator(header_edits.end()));
  return path;
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
