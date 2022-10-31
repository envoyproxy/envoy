#include "source/common/protobuf/utility.h"

#include <limits>
#include <numeric>

#include "envoy/annotations/deprecation.pb.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/documentation_url.h"
#include "source/common/common/fmt.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/visitor.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"
#include "udpa/annotations/sensitive.pb.h"
#include "udpa/annotations/status.pb.h"
#include "validate/validate.h"
#include "xds/annotations/v3/status.pb.h"
#include "yaml-cpp/yaml.h"

using namespace std::chrono_literals;

namespace Envoy {
namespace {

absl::string_view filenameFromPath(absl::string_view full_path) {
  size_t index = full_path.rfind("/");
  if (index == std::string::npos || index == full_path.size()) {
    return full_path;
  }
  return full_path.substr(index + 1, full_path.size());
}

void blockFormat(YAML::Node node) {
  node.SetStyle(YAML::EmitterStyle::Block);

  if (node.Type() == YAML::NodeType::Sequence) {
    for (const auto& it : node) {
      blockFormat(it);
    }
  }
  if (node.Type() == YAML::NodeType::Map) {
    for (const auto& it : node) {
      blockFormat(it.second);
    }
  }
}

ProtobufWkt::Value parseYamlNode(const YAML::Node& node) {
  ProtobufWkt::Value value;
  switch (node.Type()) {
  case YAML::NodeType::Null:
    value.set_null_value(ProtobufWkt::NULL_VALUE);
    break;
  case YAML::NodeType::Scalar: {
    if (node.Tag() == "!") {
      value.set_string_value(node.as<std::string>());
      break;
    }
    bool bool_value;
    if (YAML::convert<bool>::decode(node, bool_value)) {
      value.set_bool_value(bool_value);
      break;
    }
    int64_t int_value;
    if (YAML::convert<int64_t>::decode(node, int_value)) {
      if (std::numeric_limits<int32_t>::min() <= int_value &&
          std::numeric_limits<int32_t>::max() >= int_value) {
        // We could convert all integer values to string but it will break some stuff relying on
        // ProtobufWkt::Struct itself, only convert small numbers into number_value here.
        value.set_number_value(int_value);
      } else {
        // Proto3 JSON mapping allows use string for integer, this still has to be converted from
        // int_value to support hexadecimal and octal literals.
        value.set_string_value(std::to_string(int_value));
      }
      break;
    }
    // Fall back on string, including float/double case. When protobuf parse the JSON into a message
    // it will convert based on the type in the message definition.
    value.set_string_value(node.as<std::string>());
    break;
  }
  case YAML::NodeType::Sequence: {
    auto& list_values = *value.mutable_list_value()->mutable_values();
    for (const auto& it : node) {
      *list_values.Add() = parseYamlNode(it);
    }
    break;
  }
  case YAML::NodeType::Map: {
    auto& struct_fields = *value.mutable_struct_value()->mutable_fields();
    for (const auto& it : node) {
      if (it.first.Tag() != "!ignore") {
        struct_fields[it.first.as<std::string>()] = parseYamlNode(it.second);
      }
    }
    break;
  }
  case YAML::NodeType::Undefined:
    throw EnvoyException("Undefined YAML value");
  }
  return value;
}

void jsonConvertInternal(const Protobuf::Message& source,
                         ProtobufMessage::ValidationVisitor& validation_visitor,
                         Protobuf::Message& dest) {
  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  std::string json;
  const auto status = Protobuf::util::MessageToJsonString(source, &json, json_options);
  if (!status.ok()) {
    throw EnvoyException(fmt::format("Unable to convert protobuf message to JSON string: {} {}",
                                     status.ToString(), source.DebugString()));
  }
  MessageUtil::loadFromJson(json, dest, validation_visitor);
}

// Logs a warning for use of a deprecated field or runtime-overridden use of an
// otherwise fatal field. Throws a warning on use of a fatal by default field.
void deprecatedFieldHelper(Runtime::Loader* runtime, bool proto_annotated_as_deprecated,
                           bool proto_annotated_as_disallowed, const std::string& feature_name,
                           std::string error, const Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor) {
// This option is for Envoy builds with --define deprecated_features=disabled
// The build options CI then verifies that as Envoy developers deprecate fields,
// that they update canonical configs and unit tests to not use those deprecated
// fields, by making their use fatal in the build options CI.
#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
  bool warn_only = false;
#else
  bool warn_only = true;
#endif
  if (runtime &&
      runtime->snapshot().getBoolean("envoy.features.fail_on_any_deprecated_feature", false)) {
    warn_only = false;
  }
  bool warn_default = warn_only;
  // Allow runtime to be null both to not crash if this is called before server initialization,
  // and so proto validation works in context where runtime singleton is not set up (e.g.
  // standalone config validation utilities)
  if (runtime && proto_annotated_as_deprecated) {
    // This is set here, rather than above, so that in the absence of a
    // registry (i.e. test) the default for if a feature is allowed or not is
    // based on ENVOY_DISABLE_DEPRECATED_FEATURES.
    warn_only &= !proto_annotated_as_disallowed;
    warn_default = warn_only;
    warn_only = runtime->snapshot().deprecatedFeatureEnabled(feature_name, warn_only);
  }
  // Note this only checks if the runtime override has an actual effect. It
  // does not change the logged warning if someone "allows" a deprecated but not
  // yet fatal field.
  const bool runtime_overridden = (warn_default == false && warn_only == true);

  std::string with_overridden = fmt::format(
      fmt::runtime(error),
      (runtime_overridden ? "runtime overrides to continue using now fatal-by-default " : ""));

  validation_visitor.onDeprecatedField("type " + message.GetTypeName() + " " + with_overridden,
                                       warn_only);
}

} // namespace

namespace ProtobufPercentHelper {

uint64_t checkAndReturnDefault(uint64_t default_value, uint64_t max_value) {
  ASSERT(default_value <= max_value);
  return default_value;
}

uint64_t convertPercent(double percent, uint64_t max_value) {
  // Checked by schema.
  ASSERT(percent >= 0.0 && percent <= 100.0);
  return max_value * (percent / 100.0);
}

bool evaluateFractionalPercent(envoy::type::v3::FractionalPercent percent, uint64_t random_value) {
  return random_value % fractionalPercentDenominatorToInt(percent.denominator()) <
         percent.numerator();
}

uint64_t fractionalPercentDenominatorToInt(
    const envoy::type::v3::FractionalPercent::DenominatorType& denominator) {
  switch (denominator) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::type::v3::FractionalPercent::HUNDRED:
    return 100;
  case envoy::type::v3::FractionalPercent::TEN_THOUSAND:
    return 10000;
  case envoy::type::v3::FractionalPercent::MILLION:
    return 1000000;
  }
  PANIC_DUE_TO_CORRUPT_ENUM
}

} // namespace ProtobufPercentHelper

MissingFieldException::MissingFieldException(const std::string& field_name,
                                             const Protobuf::Message& message)
    : EnvoyException(
          fmt::format("Field '{}' is missing in: {}", field_name, message.DebugString())) {}

ProtoValidationException::ProtoValidationException(const std::string& validation_error,
                                                   const Protobuf::Message& message)
    : EnvoyException(fmt::format("Proto constraint validation failed ({}): {}", validation_error,
                                 message.DebugString())) {
  ENVOY_LOG_MISC(debug, "Proto validation error; throwing {}", what());
}

void ProtoExceptionUtil::throwMissingFieldException(const std::string& field_name,
                                                    const Protobuf::Message& message) {
  throw MissingFieldException(field_name, message);
}

void ProtoExceptionUtil::throwProtoValidationException(const std::string& validation_error,
                                                       const Protobuf::Message& message) {
  throw ProtoValidationException(validation_error, message);
}

size_t MessageUtil::hash(const Protobuf::Message& message) {
  std::string text_format;

  {
    Protobuf::TextFormat::Printer printer;
    printer.SetExpandAny(true);
    printer.SetUseFieldNumber(true);
    printer.SetSingleLineMode(true);
    printer.SetHideUnknownFields(true);
    printer.PrintToString(message, &text_format);
  }

  return HashUtil::xxHash64(text_format);
}

void MessageUtil::loadFromJson(const std::string& json, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
  bool has_unknown_field;
  auto status = loadFromJsonNoThrow(json, message, has_unknown_field);
  if (status.ok()) {
    return;
  }
  if (has_unknown_field) {
    // If the parsing failure is caused by the unknown fields.
    validation_visitor.onUnknownField("type " + message.GetTypeName() + " reason " +
                                      status.ToString());
  } else {
    // If the error has nothing to do with unknown field.
    throw EnvoyException("Unable to parse JSON as proto (" + status.ToString() + "): " + json);
  }
}

Protobuf::util::Status MessageUtil::loadFromJsonNoThrow(const std::string& json,
                                                        Protobuf::Message& message,
                                                        bool& has_unknown_fileld) {
  has_unknown_fileld = false;
  Protobuf::util::JsonParseOptions options;
  options.case_insensitive_enum_parsing = true;
  // Let's first try and get a clean parse when checking for unknown fields;
  // this should be the common case.
  options.ignore_unknown_fields = false;
  const auto strict_status = Protobuf::util::JsonStringToMessage(json, &message, options);
  if (strict_status.ok()) {
    // Success, no need to do any extra work.
    return strict_status;
  }
  // If we fail, we see if we get a clean parse when allowing unknown fields.
  // This is essentially a workaround
  // for https://github.com/protocolbuffers/protobuf/issues/5967.
  // TODO(htuch): clean this up when protobuf supports JSON/YAML unknown field
  // detection directly.
  options.ignore_unknown_fields = true;
  const auto relaxed_status = Protobuf::util::JsonStringToMessage(json, &message, options);
  // If we still fail with relaxed unknown field checking, the error has nothing
  // to do with unknown fields.
  if (relaxed_status.ok()) {
    has_unknown_fileld = true;
    return strict_status;
  }
  return relaxed_status;
}

void MessageUtil::loadFromJson(const std::string& json, ProtobufWkt::Struct& message) {
  // No need to validate if converting to a Struct, since there are no unknown
  // fields possible.
  loadFromJson(json, message, ProtobufMessage::getNullValidationVisitor());
}

void MessageUtil::loadFromYaml(const std::string& yaml, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
  ProtobufWkt::Value value = ValueUtil::loadFromYaml(yaml);
  if (value.kind_case() == ProtobufWkt::Value::kStructValue ||
      value.kind_case() == ProtobufWkt::Value::kListValue) {
    jsonConvertInternal(value, validation_visitor, message);
    return;
  }
  throw EnvoyException("Unable to convert YAML as JSON: " + yaml);
}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               Api::Api& api) {
  const std::string contents = api.fileSystem().fileReadToEnd(path);
  // If the filename ends with .pb, attempt to parse it as a binary proto.
  if (absl::EndsWithIgnoreCase(path, FileExtensions::get().ProtoBinary)) {
    // Attempt to parse the binary format.
    if (message.ParseFromString(contents)) {
      MessageUtil::checkForUnexpectedFields(message, validation_visitor);
    }
    return;
  }

  // If the filename ends with .pb_text, attempt to parse it as a text proto.
  if (absl::EndsWithIgnoreCase(path, FileExtensions::get().ProtoText)) {
    if (Protobuf::TextFormat::ParseFromString(contents, &message)) {
      return;
    }
    throw EnvoyException("Unable to parse file \"" + path + "\" as a text protobuf (type " +
                         message.GetTypeName() + ")");
  }
  if (absl::EndsWithIgnoreCase(path, FileExtensions::get().Yaml) ||
      absl::EndsWithIgnoreCase(path, FileExtensions::get().Yml)) {
    loadFromYaml(contents, message, validation_visitor);
  } else {
    loadFromJson(contents, message, validation_visitor);
  }
}

namespace {

void checkForDeprecatedNonRepeatedEnumValue(
    const Protobuf::Message& message, absl::string_view filename,
    const Protobuf::FieldDescriptor* field, const Protobuf::Reflection* reflection,
    Runtime::Loader* runtime, ProtobufMessage::ValidationVisitor& validation_visitor) {
  // Repeated fields will be handled by recursion in checkForUnexpectedFields.
  if (field->is_repeated() || field->cpp_type() != Protobuf::FieldDescriptor::CPPTYPE_ENUM) {
    return;
  }

  bool default_value = !reflection->HasField(message, field);

  const Protobuf::EnumValueDescriptor* enum_value_descriptor = reflection->GetEnum(message, field);
  if (!enum_value_descriptor->options().deprecated()) {
    return;
  }

  const std::string error =
      absl::StrCat("Using {}", (default_value ? "the default now-" : ""), "deprecated value ",
                   enum_value_descriptor->name(), " for enum '", field->full_name(), "' from file ",
                   filename, ". This enum value will be removed from Envoy soon",
                   (default_value ? " so a non-default value must now be explicitly set" : ""),
                   ". Please see " ENVOY_DOC_URL_VERSION_HISTORY " for details.");
  deprecatedFieldHelper(
      runtime, true /*deprecated*/,
      enum_value_descriptor->options().GetExtension(envoy::annotations::disallowed_by_default_enum),
      absl::StrCat("envoy.deprecated_features:", enum_value_descriptor->full_name()), error,
      message, validation_visitor);
}

constexpr absl::string_view WipWarning =
    "API features marked as work-in-progress are not considered stable, are not covered by the "
    "threat model, are not supported by the security team, and are subject to breaking changes. Do "
    "not use this feature without understanding each of the previous points.";

class UnexpectedFieldProtoVisitor : public ProtobufMessage::ConstProtoVisitor {
public:
  UnexpectedFieldProtoVisitor(ProtobufMessage::ValidationVisitor& validation_visitor,
                              Runtime::Loader* runtime)
      : validation_visitor_(validation_visitor), runtime_(runtime) {}

  void onField(const Protobuf::Message& message, const Protobuf::FieldDescriptor& field) override {
    const Protobuf::Reflection* reflection = message.GetReflection();
    absl::string_view filename = filenameFromPath(field.file()->name());

    // Before we check to see if the field is in use, see if there's a
    // deprecated default enum value.
    checkForDeprecatedNonRepeatedEnumValue(message, filename, &field, reflection, runtime_,
                                           validation_visitor_);

    // If this field is not in use, continue.
    if ((field.is_repeated() && reflection->FieldSize(message, &field) == 0) ||
        (!field.is_repeated() && !reflection->HasField(message, &field))) {
      return;
    }

    const auto& field_status = field.options().GetExtension(xds::annotations::v3::field_status);
    if (field_status.work_in_progress()) {
      validation_visitor_.onWorkInProgress(fmt::format(
          "field '{}' is marked as work-in-progress. {}", field.full_name(), WipWarning));
    }

    // If this field is deprecated, warn or throw an error.
    if (field.options().deprecated()) {
      const std::string warning =
          absl::StrCat("Using {}deprecated option '", field.full_name(), "' from file ", filename,
                       ". This configuration will be removed from "
                       "Envoy soon. Please see " ENVOY_DOC_URL_VERSION_HISTORY " for details.");

      deprecatedFieldHelper(runtime_, true /*deprecated*/,
                            field.options().GetExtension(envoy::annotations::disallowed_by_default),
                            absl::StrCat("envoy.deprecated_features:", field.full_name()), warning,
                            message, validation_visitor_);
    }
  }

  void onMessage(const Protobuf::Message& message,
                 absl::Span<const Protobuf::Message* const> parents, bool) override {
    if (message.GetDescriptor()
            ->options()
            .GetExtension(xds::annotations::v3::message_status)
            .work_in_progress()) {
      validation_visitor_.onWorkInProgress(fmt::format(
          "message '{}' is marked as work-in-progress. {}", message.GetTypeName(), WipWarning));
    }

    const auto& udpa_file_options =
        message.GetDescriptor()->file()->options().GetExtension(udpa::annotations::file_status);
    const auto& xds_file_options =
        message.GetDescriptor()->file()->options().GetExtension(xds::annotations::v3::file_status);
    if (udpa_file_options.work_in_progress() || xds_file_options.work_in_progress()) {
      validation_visitor_.onWorkInProgress(
          fmt::format("message '{}' is contained in proto file '{}' marked as work-in-progress. {}",
                      message.GetTypeName(), message.GetDescriptor()->file()->name(), WipWarning));
    }

    // Reject unknown fields.
    const auto& unknown_fields = message.GetReflection()->GetUnknownFields(message);
    if (!unknown_fields.empty()) {
      std::string error_msg;
      for (int n = 0; n < unknown_fields.field_count(); ++n) {
        absl::StrAppend(&error_msg, n > 0 ? ", " : "", unknown_fields.field(n).number());
      }
      if (!error_msg.empty()) {
        validation_visitor_.onUnknownField(
            fmt::format("type {}({}) with unknown field set {{{}}}", message.GetTypeName(),
                        !parents.empty()
                            ? absl::StrJoin(parents, "::",
                                            [](std::string* out, const Protobuf::Message* const m) {
                                              absl::StrAppend(out, m->GetTypeName());
                                            })
                            : "root",
                        error_msg));
      }
    }
  }

private:
  ProtobufMessage::ValidationVisitor& validation_visitor_;
  Runtime::Loader* runtime_;
};

} // namespace

void MessageUtil::checkForUnexpectedFields(const Protobuf::Message& message,
                                           ProtobufMessage::ValidationVisitor& validation_visitor,
                                           bool recurse_into_any) {
  Runtime::Loader* runtime = validation_visitor.runtime().has_value()
                                 ? &validation_visitor.runtime().value().get()
                                 : nullptr;
  UnexpectedFieldProtoVisitor unexpected_field_visitor(validation_visitor, runtime);
  ProtobufMessage::traverseMessage(unexpected_field_visitor, message, recurse_into_any);
}

namespace {

class PgvCheckVisitor : public ProtobufMessage::ConstProtoVisitor {
public:
  void onMessage(const Protobuf::Message& message, absl::Span<const Protobuf::Message* const>,
                 bool was_any_or_top_level) override {
    std::string err;
    // PGV verification is itself recursive up to the point at which it hits an Any message. As
    // such, to avoid N^2 checking of the tree, we only perform an additional check at the point
    // at which PGV would have stopped because it does not itself check within Any messages.
    if (was_any_or_top_level && !pgv::BaseValidator::AbstractCheckMessage(message, &err)) {
      ProtoExceptionUtil::throwProtoValidationException(err, message);
    }
  }

  void onField(const Protobuf::Message&, const Protobuf::FieldDescriptor&) override {}
};

} // namespace

void MessageUtil::recursivePgvCheck(const Protobuf::Message& message) {
  PgvCheckVisitor visitor;
  ProtobufMessage::traverseMessage(visitor, message, true);
}

std::string MessageUtil::getYamlStringFromMessage(const Protobuf::Message& message,
                                                  const bool block_print,
                                                  const bool always_print_primitive_fields) {

  auto json_or_error = getJsonStringFromMessage(message, false, always_print_primitive_fields);
  if (!json_or_error.ok()) {
    throw EnvoyException(json_or_error.status().ToString());
  }
  YAML::Node node;
  TRY_ASSERT_MAIN_THREAD { node = YAML::Load(json_or_error.value()); }
  END_TRY
  catch (YAML::ParserException& e) {
    throw EnvoyException(e.what());
  }
  catch (YAML::BadConversion& e) {
    throw EnvoyException(e.what());
  }
  catch (std::exception& e) {
    // Catch unknown YAML parsing exceptions.
    throw EnvoyException(fmt::format("Unexpected YAML exception: {}", +e.what()));
  }
  if (block_print) {
    blockFormat(node);
  }
  YAML::Emitter out;
  out << node;
  return out.c_str();
}

ProtobufUtil::StatusOr<std::string>
MessageUtil::getJsonStringFromMessage(const Protobuf::Message& message, const bool pretty_print,
                                      const bool always_print_primitive_fields) {
  Protobuf::util::JsonPrintOptions json_options;
  // By default, proto field names are converted to camelCase when the message is converted to JSON.
  // Setting this option makes debugging easier because it keeps field names consistent in JSON
  // printouts.
  json_options.preserve_proto_field_names = true;
  if (pretty_print) {
    json_options.add_whitespace = true;
  }
  // Primitive types such as int32s and enums will not be serialized if they have the default value.
  // This flag disables that behavior.
  if (always_print_primitive_fields) {
    json_options.always_print_primitive_fields = true;
  }
  std::string json;
  if (auto status = Protobuf::util::MessageToJsonString(message, &json, json_options);
      !status.ok()) {
    return status;
  }
  return json;
}

std::string MessageUtil::getJsonStringFromMessageOrError(const Protobuf::Message& message,
                                                         bool pretty_print,
                                                         bool always_print_primitive_fields) {
  auto json_or_error =
      getJsonStringFromMessage(message, pretty_print, always_print_primitive_fields);
  return json_or_error.ok() ? std::move(json_or_error).value()
                            : fmt::format("Failed to convert protobuf message to JSON string: {}",
                                          json_or_error.status().ToString());
}

void MessageUtil::unpackTo(const ProtobufWkt::Any& any_message, Protobuf::Message& message) {
  if (!any_message.UnpackTo(&message)) {
    throw EnvoyException(fmt::format("Unable to unpack as {}: {}",
                                     message.GetDescriptor()->full_name(),
                                     any_message.DebugString()));
  }
}

absl::Status MessageUtil::unpackToNoThrow(const ProtobufWkt::Any& any_message,
                                          Protobuf::Message& message) {
  if (!any_message.UnpackTo(&message)) {
    return absl::InternalError(absl::StrCat("Unable to unpack as ",
                                            message.GetDescriptor()->full_name(), ": ",
                                            any_message.DebugString()));
  }
  // Ok Status is returned if `UnpackTo` succeeded.
  return absl::OkStatus();
}

void MessageUtil::jsonConvert(const Protobuf::Message& source, ProtobufWkt::Struct& dest) {
  // Any proto3 message can be transformed to Struct, so there is no need to check for unknown
  // fields. There is one catch; Duration/Timestamp etc. which have non-object canonical JSON
  // representations don't work.
  jsonConvertInternal(source, ProtobufMessage::getNullValidationVisitor(), dest);
}

void MessageUtil::jsonConvert(const ProtobufWkt::Struct& source,
                              ProtobufMessage::ValidationVisitor& validation_visitor,
                              Protobuf::Message& dest) {
  jsonConvertInternal(source, validation_visitor, dest);
}

bool MessageUtil::jsonConvertValue(const Protobuf::Message& source, ProtobufWkt::Value& dest) {
  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  std::string json;
  auto status = Protobuf::util::MessageToJsonString(source, &json, json_options);
  if (!status.ok()) {
    return false;
  }
  bool has_unknow_field;
  status = MessageUtil::loadFromJsonNoThrow(json, dest, has_unknow_field);
  if (status.ok() || has_unknow_field) {
    return true;
  }
  return false;
}

ProtobufWkt::Struct MessageUtil::keyValueStruct(const std::string& key, const std::string& value) {
  ProtobufWkt::Struct struct_obj;
  ProtobufWkt::Value val;
  val.set_string_value(value);
  (*struct_obj.mutable_fields())[key] = val;
  return struct_obj;
}

ProtobufWkt::Struct MessageUtil::keyValueStruct(const std::map<std::string, std::string>& fields) {
  ProtobufWkt::Struct struct_obj;
  ProtobufWkt::Value val;
  for (const auto& pair : fields) {
    val.set_string_value(pair.second);
    (*struct_obj.mutable_fields())[pair.first] = val;
  }
  return struct_obj;
}

std::string MessageUtil::codeEnumToString(ProtobufUtil::StatusCode code) {
  return ProtobufUtil::Status(code, "").ToString();
}

namespace {

// Forward declaration for mutually-recursive helper functions.
void redact(Protobuf::Message* message, bool ancestor_is_sensitive);

using Transform = std::function<void(Protobuf::Message*, const Protobuf::Reflection*,
                                     const Protobuf::FieldDescriptor*)>;

// To redact opaque types, namely `Any` and `TypedStruct`, we have to reify them to the concrete
// message types specified by their `type_url` before we can redact their contents. This is mostly
// identical between `Any` and `TypedStruct`, the only difference being how they are packed and
// unpacked. Note that we have to use reflection on the opaque type here, rather than downcasting
// to `Any` or `TypedStruct`, because any message we might be handling could have originated from
// a `DynamicMessageFactory`.
bool redactOpaque(Protobuf::Message* message, bool ancestor_is_sensitive,
                  absl::string_view opaque_type_name, Transform unpack, Transform repack) {
  // Ensure this message has the opaque type we're expecting.
  const auto* opaque_descriptor = message->GetDescriptor();
  if (opaque_descriptor->full_name() != opaque_type_name) {
    return false;
  }

  // Find descriptors for the `type_url` and `value` fields. The `type_url` field must not be
  // empty, but `value` may be (in which case our work is done).
  const auto* reflection = message->GetReflection();
  const auto* type_url_field_descriptor = opaque_descriptor->FindFieldByName("type_url");
  const auto* value_field_descriptor = opaque_descriptor->FindFieldByName("value");
  ASSERT(type_url_field_descriptor != nullptr && value_field_descriptor != nullptr);
  if (!reflection->HasField(*message, type_url_field_descriptor) &&
      !reflection->HasField(*message, value_field_descriptor)) {
    return true;
  }
  if (!reflection->HasField(*message, type_url_field_descriptor) ||
      !reflection->HasField(*message, value_field_descriptor)) {
    return false;
  }

  // Try to find a descriptor for `type_url` in the pool and instantiate a new message of the
  // correct concrete type.
  const std::string type_url(reflection->GetString(*message, type_url_field_descriptor));
  const std::string concrete_type_name(TypeUtil::typeUrlToDescriptorFullName(type_url));
  const auto* concrete_descriptor =
      Protobuf::DescriptorPool::generated_pool()->FindMessageTypeByName(concrete_type_name);
  if (concrete_descriptor == nullptr) {
    // If the type URL doesn't correspond to a known proto, don't try to reify it, just treat it
    // like any other message. See the documented limitation on `MessageUtil::redact()` for more
    // context.
    ENVOY_LOG_MISC(warn, "Could not reify {} with unknown type URL {}", opaque_type_name, type_url);
    return false;
  }
  Protobuf::DynamicMessageFactory message_factory;
  std::unique_ptr<Protobuf::Message> typed_message(
      message_factory.GetPrototype(concrete_descriptor)->New());

  // Finally we can unpack, redact, and repack the opaque message using the provided callbacks.
  unpack(typed_message.get(), reflection, value_field_descriptor);
  redact(typed_message.get(), ancestor_is_sensitive);
  repack(typed_message.get(), reflection, value_field_descriptor);
  return true;
}

bool redactAny(Protobuf::Message* message, bool ancestor_is_sensitive) {
  return redactOpaque(
      message, ancestor_is_sensitive, "google.protobuf.Any",
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
        // To unpack an `Any`, parse the serialized proto.
        typed_message->ParseFromString(reflection->GetString(*message, field_descriptor));
      },
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
        // To repack an `Any`, reserialize its proto.
        reflection->SetString(message, field_descriptor, typed_message->SerializeAsString());
      });
}

// To redact a `TypedStruct`, we have to reify it based on its `type_url` to redact it.
bool redactTypedStruct(Protobuf::Message* message, const char* typed_struct_type,
                       bool ancestor_is_sensitive) {
  return redactOpaque(
      message, ancestor_is_sensitive, typed_struct_type,
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
        // To unpack a `TypedStruct`, convert the struct from JSON.
        jsonConvertInternal(reflection->GetMessage(*message, field_descriptor),
                            ProtobufMessage::getNullValidationVisitor(), *typed_message);
      },
      [message](Protobuf::Message* typed_message, const Protobuf::Reflection* reflection,
                const Protobuf::FieldDescriptor* field_descriptor) {
        // To repack a `TypedStruct`, convert the message back to JSON.
        jsonConvertInternal(*typed_message, ProtobufMessage::getNullValidationVisitor(),
                            *(reflection->MutableMessage(message, field_descriptor)));
      });
}

// Recursive helper method for MessageUtil::redact() below.
void redact(Protobuf::Message* message, bool ancestor_is_sensitive) {
  if (redactAny(message, ancestor_is_sensitive) ||
      redactTypedStruct(message, "xds.type.v3.TypedStruct", ancestor_is_sensitive) ||
      redactTypedStruct(message, "udpa.type.v1.TypedStruct", ancestor_is_sensitive)) {
    return;
  }

  const auto* descriptor = message->GetDescriptor();
  const auto* reflection = message->GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const auto* field_descriptor = descriptor->field(i);

    // Redact if this field or any of its ancestors have the `sensitive` option set.
    const bool sensitive = ancestor_is_sensitive ||
                           field_descriptor->options().GetExtension(udpa::annotations::sensitive);

    if (field_descriptor->type() == Protobuf::FieldDescriptor::TYPE_MESSAGE) {
      // Recursive case: traverse message fields.
      if (field_descriptor->is_map()) {
        // Redact values of maps only. Redacting both leaves the map with multiple "[redacted]"
        // keys.
        const int field_size = reflection->FieldSize(*message, field_descriptor);
        for (int i = 0; i < field_size; ++i) {
          Protobuf::Message* map_pair =
              reflection->MutableRepeatedMessage(message, field_descriptor, i);
          auto* value_field_desc = map_pair->GetDescriptor()->FindFieldByName("value");
          if (sensitive && (value_field_desc->type() == Protobuf::FieldDescriptor::TYPE_STRING ||
                            value_field_desc->type() == Protobuf::FieldDescriptor::TYPE_BYTES)) {
            map_pair->GetReflection()->SetString(map_pair, value_field_desc, "[redacted]");
          } else if (value_field_desc->type() == Protobuf::FieldDescriptor::TYPE_MESSAGE) {
            redact(map_pair->GetReflection()->MutableMessage(map_pair, value_field_desc),
                   sensitive);
          } else if (sensitive) {
            map_pair->GetReflection()->ClearField(map_pair, value_field_desc);
          }
        }
      } else if (field_descriptor->is_repeated()) {
        const int field_size = reflection->FieldSize(*message, field_descriptor);
        for (int i = 0; i < field_size; ++i) {
          redact(reflection->MutableRepeatedMessage(message, field_descriptor, i), sensitive);
        }
      } else if (reflection->HasField(*message, field_descriptor)) {
        redact(reflection->MutableMessage(message, field_descriptor), sensitive);
      }
    } else if (sensitive) {
      // Base case: replace strings and bytes with "[redacted]" and clear all others.
      if (field_descriptor->type() == Protobuf::FieldDescriptor::TYPE_STRING ||
          field_descriptor->type() == Protobuf::FieldDescriptor::TYPE_BYTES) {
        if (field_descriptor->is_repeated()) {
          const int field_size = reflection->FieldSize(*message, field_descriptor);
          for (int i = 0; i < field_size; ++i) {
            reflection->SetRepeatedString(message, field_descriptor, i, "[redacted]");
          }
        } else if (reflection->HasField(*message, field_descriptor)) {
          reflection->SetString(message, field_descriptor, "[redacted]");
        }
      } else {
        reflection->ClearField(message, field_descriptor);
      }
    }
  }
}

} // namespace

void MessageUtil::redact(Protobuf::Message& message) {
  ::Envoy::redact(&message, /* ancestor_is_sensitive = */ false);
}

void MessageUtil::wireCast(const Protobuf::Message& src, Protobuf::Message& dst) {
  // This should should generally succeed, but if there are malformed UTF-8 strings in a message,
  // this can fail.
  if (!dst.ParseFromString(src.SerializeAsString())) {
    throw EnvoyException("Unable to deserialize during wireCast()");
  }
}

ProtobufWkt::Value ValueUtil::loadFromYaml(const std::string& yaml) {
  TRY_ASSERT_MAIN_THREAD { return parseYamlNode(YAML::Load(yaml)); }
  END_TRY
  catch (YAML::ParserException& e) {
    throw EnvoyException(e.what());
  }
  catch (YAML::BadConversion& e) {
    throw EnvoyException(e.what());
  }
  catch (std::exception& e) {
    // There is a potentially wide space of exceptions thrown by the YAML parser,
    // and enumerating them all may be difficult. Envoy doesn't work well with
    // unhandled exceptions, so we capture them and record the exception name in
    // the Envoy Exception text.
    throw EnvoyException(fmt::format("Unexpected YAML exception: {}", +e.what()));
  }
}

bool ValueUtil::equal(const ProtobufWkt::Value& v1, const ProtobufWkt::Value& v2) {
  ProtobufWkt::Value::KindCase kind = v1.kind_case();
  if (kind != v2.kind_case()) {
    return false;
  }

  switch (kind) {
  case ProtobufWkt::Value::KIND_NOT_SET:
    return v2.kind_case() == ProtobufWkt::Value::KIND_NOT_SET;

  case ProtobufWkt::Value::kNullValue:
    return true;

  case ProtobufWkt::Value::kNumberValue:
    return v1.number_value() == v2.number_value();

  case ProtobufWkt::Value::kStringValue:
    return v1.string_value() == v2.string_value();

  case ProtobufWkt::Value::kBoolValue:
    return v1.bool_value() == v2.bool_value();

  case ProtobufWkt::Value::kStructValue: {
    const ProtobufWkt::Struct& s1 = v1.struct_value();
    const ProtobufWkt::Struct& s2 = v2.struct_value();
    if (s1.fields_size() != s2.fields_size()) {
      return false;
    }
    for (const auto& it1 : s1.fields()) {
      const auto& it2 = s2.fields().find(it1.first);
      if (it2 == s2.fields().end()) {
        return false;
      }

      if (!equal(it1.second, it2->second)) {
        return false;
      }
    }
    return true;
  }

  case ProtobufWkt::Value::kListValue: {
    const ProtobufWkt::ListValue& l1 = v1.list_value();
    const ProtobufWkt::ListValue& l2 = v2.list_value();
    if (l1.values_size() != l2.values_size()) {
      return false;
    }
    for (int i = 0; i < l1.values_size(); i++) {
      if (!equal(l1.values(i), l2.values(i))) {
        return false;
      }
    }
    return true;
  }
  }
  return false;
}

const ProtobufWkt::Value& ValueUtil::nullValue() {
  static const auto* v = []() -> ProtobufWkt::Value* {
    auto* vv = new ProtobufWkt::Value();
    vv->set_null_value(ProtobufWkt::NULL_VALUE);
    return vv;
  }();
  return *v;
}

ProtobufWkt::Value ValueUtil::stringValue(const std::string& str) {
  ProtobufWkt::Value val;
  val.set_string_value(str);
  return val;
}

ProtobufWkt::Value ValueUtil::optionalStringValue(const absl::optional<std::string>& str) {
  if (str.has_value()) {
    return ValueUtil::stringValue(str.value());
  }
  return ValueUtil::nullValue();
}

ProtobufWkt::Value ValueUtil::boolValue(bool b) {
  ProtobufWkt::Value val;
  val.set_bool_value(b);
  return val;
}

ProtobufWkt::Value ValueUtil::structValue(const ProtobufWkt::Struct& obj) {
  ProtobufWkt::Value val;
  (*val.mutable_struct_value()) = obj;
  return val;
}

ProtobufWkt::Value ValueUtil::listValue(const std::vector<ProtobufWkt::Value>& values) {
  auto list = std::make_unique<ProtobufWkt::ListValue>();
  for (const auto& value : values) {
    *list->add_values() = value;
  }
  ProtobufWkt::Value val;
  val.set_allocated_list_value(list.release());
  return val;
}

namespace {

void validateDuration(const ProtobufWkt::Duration& duration, int64_t max_seconds_value) {
  if (duration.seconds() < 0 || duration.nanos() < 0) {
    throw DurationUtil::OutOfRangeException(
        fmt::format("Expected positive duration: {}", duration.DebugString()));
  }
  if (duration.nanos() > 999999999 || duration.seconds() > max_seconds_value) {
    throw DurationUtil::OutOfRangeException(
        fmt::format("Duration out-of-range: {}", duration.DebugString()));
  }
}

void validateDuration(const ProtobufWkt::Duration& duration) {
  validateDuration(duration, Protobuf::util::TimeUtil::kDurationMaxSeconds);
}

void validateDurationAsMilliseconds(const ProtobufWkt::Duration& duration) {
  // Apply stricter max boundary to the `seconds` value to avoid overflow.
  // Note that protobuf internally converts to nanoseconds.
  // The kMaxInt64Nanoseconds = 9223372036, which is about 300 years.
  constexpr int64_t kMaxInt64Nanoseconds =
      std::numeric_limits<int64_t>::max() / (1000 * 1000 * 1000);
  validateDuration(duration, kMaxInt64Nanoseconds);
}

} // namespace

uint64_t DurationUtil::durationToMilliseconds(const ProtobufWkt::Duration& duration) {
  validateDurationAsMilliseconds(duration);
  return Protobuf::util::TimeUtil::DurationToMilliseconds(duration);
}

uint64_t DurationUtil::durationToSeconds(const ProtobufWkt::Duration& duration) {
  validateDuration(duration);
  return Protobuf::util::TimeUtil::DurationToSeconds(duration);
}

void TimestampUtil::systemClockToTimestamp(const SystemTime system_clock_time,
                                           ProtobufWkt::Timestamp& timestamp) {
  // Converts to millisecond-precision Timestamp by explicitly casting to millisecond-precision
  // time_point.
  timestamp.MergeFrom(Protobuf::util::TimeUtil::MillisecondsToTimestamp(
      std::chrono::time_point_cast<std::chrono::milliseconds>(system_clock_time)
          .time_since_epoch()
          .count()));
}

absl::string_view TypeUtil::typeUrlToDescriptorFullName(absl::string_view type_url) {
  const size_t pos = type_url.rfind('/');
  if (pos != absl::string_view::npos) {
    type_url = type_url.substr(pos + 1);
  }
  return type_url;
}

std::string TypeUtil::descriptorFullNameToTypeUrl(absl::string_view type) {
  return "type.googleapis.com/" + std::string(type);
}

void StructUtil::update(ProtobufWkt::Struct& obj, const ProtobufWkt::Struct& with) {
  auto& obj_fields = *obj.mutable_fields();

  for (const auto& [key, val] : with.fields()) {
    auto& obj_key = obj_fields[key];

    // If the types are different, the last one wins.
    const auto val_kind = val.kind_case();
    if (val_kind != obj_key.kind_case()) {
      obj_key = val;
      continue;
    }

    // Otherwise, the strategy depends on the value kind.
    switch (val.kind_case()) {
    // For scalars, the last one wins.
    case ProtobufWkt::Value::kNullValue:
    case ProtobufWkt::Value::kNumberValue:
    case ProtobufWkt::Value::kStringValue:
    case ProtobufWkt::Value::kBoolValue:
      obj_key = val;
      break;
    // If we got a structure, recursively update.
    case ProtobufWkt::Value::kStructValue:
      update(*obj_key.mutable_struct_value(), val.struct_value());
      break;
    // For lists, append the new values.
    case ProtobufWkt::Value::kListValue: {
      auto& obj_key_vec = *obj_key.mutable_list_value()->mutable_values();
      const auto& vals = val.list_value().values();
      obj_key_vec.MergeFrom(vals);
      break;
    }
    case ProtobufWkt::Value::KIND_NOT_SET:
      break;
    }
  }
}

} // namespace Envoy
