#include "common/protobuf/utility.h"

#include <limits>
#include <numeric>

#include "envoy/annotations/deprecation.pb.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/config/api_type_oracle.h"
#include "common/config/version_converter.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/visitor.h"
#include "common/protobuf/well_known.h"

#include "absl/strings/match.h"
#include "udpa/annotations/sensitive.pb.h"
#include "yaml-cpp/yaml.h"

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
    for (auto it : node) {
      blockFormat(it);
    }
  }
  if (node.Type() == YAML::NodeType::Map) {
    for (auto it : node) {
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
      struct_fields[it.first.as<std::string>()] = parseYamlNode(it.second);
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

enum class MessageVersion {
  // This is an earlier version of a message, a later one exists.
  EARLIER_VERSION,
  // This is the latest version of a message.
  LATEST_VERSION,
};

using MessageXformFn = std::function<void(Protobuf::Message&, MessageVersion)>;

class ApiBoostRetryException : public EnvoyException {
public:
  ApiBoostRetryException(const std::string& message) : EnvoyException(message) {}
};

// Apply a function transforming a message (e.g. loading JSON into the message).
// First we try with the message's earlier type, and if unsuccessful (or no
// earlier) type, then the current type. This allows us to take a v3 Envoy
// internal proto and ingest both v2 and v3 in methods such as loadFromJson.
// This relies on the property that any v3 configuration that is readable as
// v2 has the same semantics in v2/v3, which holds due to the highly structured
// vN/v(N+1) mechanical transforms.
void tryWithApiBoosting(MessageXformFn f, Protobuf::Message& message) {
  const Protobuf::Descriptor* earlier_version_desc =
      Config::ApiTypeOracle::getEarlierVersionDescriptor(message.GetDescriptor()->full_name());
  // If there is no earlier version of a message, just apply f directly.
  if (earlier_version_desc == nullptr) {
    f(message, MessageVersion::LATEST_VERSION);
    return;
  }
  Protobuf::DynamicMessageFactory dmf;
  auto earlier_message = ProtobufTypes::MessagePtr(dmf.GetPrototype(earlier_version_desc)->New());
  ASSERT(earlier_message != nullptr);
  try {
    // Try apply f with an earlier version of the message, then upgrade the
    // result.
    f(*earlier_message, MessageVersion::EARLIER_VERSION);
    Config::VersionConverter::upgrade(*earlier_message, message);
  } catch (ApiBoostRetryException&) {
    // If we fail at the earlier version, try f at the current version of the
    // message.
    f(message, MessageVersion::LATEST_VERSION);
  }
}

// Logs a warning for use of a deprecated field or runtime-overridden use of an
// otherwise fatal field. Throws a warning on use of a fatal by default field.
void deprecatedFieldHelper(Runtime::Loader* runtime, bool proto_annotated_as_deprecated,
                           bool proto_annotated_as_disallowed, const std::string& feature_name,
                           std::string error, const Protobuf::Message& message) {
// This option is for Envoy builds with --define deprecated_features=disabled
// The build options CI then verifies that as Envoy developers deprecate fields,
// that they update canonical configs and unit tests to not use those deprecated
// fields, by making their use fatal in the build options CI.
#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
  bool warn_only = false;
#else
  bool warn_only = true;
#endif

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
      error,
      (runtime_overridden ? "runtime overrides to continue using now fatal-by-default " : ""));
  if (warn_only) {
    ENVOY_LOG_MISC(warn, "{}", with_overridden);
  } else {
    const char fatal_error[] =
        " If continued use of this field is absolutely necessary, see "
        "https://www.envoyproxy.io/docs/envoy/latest/configuration/operations/runtime"
        "#using-runtime-overrides-for-deprecated-features for how to apply a temporary and "
        "highly discouraged override.";
    throw ProtoValidationException(with_overridden + fatal_error, message);
  }
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
  case envoy::type::v3::FractionalPercent::HUNDRED:
    return 100;
  case envoy::type::v3::FractionalPercent::TEN_THOUSAND:
    return 10000;
  case envoy::type::v3::FractionalPercent::MILLION:
    return 1000000;
  default:
    // Checked by schema.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
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

size_t MessageUtil::hash(const Protobuf::Message& message) {
  std::string text_format;

  {
    Protobuf::TextFormat::Printer printer;
    printer.SetExpandAny(true);
    printer.SetUseFieldNumber(true);
    printer.SetSingleLineMode(true);
    printer.PrintToString(message, &text_format);
  }

  return HashUtil::xxHash64(text_format);
}

void MessageUtil::loadFromJson(const std::string& json, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
  tryWithApiBoosting(
      [&json, &validation_visitor](Protobuf::Message& message, MessageVersion message_version) {
        Protobuf::util::JsonParseOptions options;
        options.case_insensitive_enum_parsing = true;
        // Let's first try and get a clean parse when checking for unknown fields;
        // this should be the common case.
        options.ignore_unknown_fields = false;
        const auto strict_status = Protobuf::util::JsonStringToMessage(json, &message, options);
        if (strict_status.ok()) {
          // Success, no need to do any extra work.
          return;
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
        if (!relaxed_status.ok()) {
          throw EnvoyException("Unable to parse JSON as proto (" + relaxed_status.ToString() +
                               "): " + json);
        }
        // We know it's an unknown field at this point. If we're at the latest
        // version, then it's definitely an unknown field, otherwise we try to
        // load again at a later version.
        if (message_version == MessageVersion::LATEST_VERSION) {
          validation_visitor.onUnknownField("type " + message.GetTypeName() + " reason " +
                                            strict_status.ToString());
        } else {
          throw ApiBoostRetryException("Unknown field, possibly a rename, try again.");
        }
      },
      message);
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

void MessageUtil::loadFromYaml(const std::string& yaml, ProtobufWkt::Struct& message) {
  // No need to validate if converting to a Struct, since there are no unknown
  // fields possible.
  return loadFromYaml(yaml, message, ProtobufMessage::getNullValidationVisitor());
}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor,
                               Api::Api& api) {
  const std::string contents = api.fileSystem().fileReadToEnd(path);
  // If the filename ends with .pb, attempt to parse it as a binary proto.
  if (absl::EndsWith(path, FileExtensions::get().ProtoBinary)) {
    // Attempt to parse the binary format.
    if (message.ParseFromString(contents)) {
      MessageUtil::checkForUnexpectedFields(message, validation_visitor);
      return;
    }
    throw EnvoyException("Unable to parse file \"" + path + "\" as a binary protobuf (type " +
                         message.GetTypeName() + ")");
  }
  // If the filename ends with .pb_text, attempt to parse it as a text proto.
  if (absl::EndsWith(path, FileExtensions::get().ProtoText)) {
    tryWithApiBoosting(
        [&contents, &path](Protobuf::Message& message, MessageVersion message_version) {
          if (Protobuf::TextFormat::ParseFromString(contents, &message)) {
            return;
          }
          if (message_version == MessageVersion::LATEST_VERSION) {
            throw EnvoyException("Unable to parse file \"" + path + "\" as a text protobuf (type " +
                                 message.GetTypeName() + ")");
          } else {
            throw ApiBoostRetryException(
                "Failed to parse at earlier version, trying again at later version.");
          }
        },
        message);
    return;
  }
  if (absl::EndsWith(path, FileExtensions::get().Yaml)) {
    loadFromYaml(contents, message, validation_visitor);
  } else {
    loadFromJson(contents, message, validation_visitor);
  }
}

namespace {

void checkForDeprecatedNonRepeatedEnumValue(const Protobuf::Message& message,
                                            absl::string_view filename,
                                            const Protobuf::FieldDescriptor* field,
                                            const Protobuf::Reflection* reflection,
                                            Runtime::Loader* runtime) {
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
                   ". Please see https://www.envoyproxy.io/docs/envoy/latest/intro/deprecated "
                   "for details.");
  deprecatedFieldHelper(
      runtime, true /*deprecated*/,
      enum_value_descriptor->options().GetExtension(envoy::annotations::disallowed_by_default_enum),
      absl::StrCat("envoy.deprecated_features:", enum_value_descriptor->full_name()), error,
      message);
}

class UnexpectedFieldProtoVisitor : public ProtobufMessage::ConstProtoVisitor {
public:
  UnexpectedFieldProtoVisitor(ProtobufMessage::ValidationVisitor& validation_visitor,
                              Runtime::Loader* runtime)
      : validation_visitor_(validation_visitor), runtime_(runtime) {}

  const void* onField(const Protobuf::Message& message, const Protobuf::FieldDescriptor& field,
                      const void*) override {
    const Protobuf::Reflection* reflection = message.GetReflection();
    absl::string_view filename = filenameFromPath(field.file()->name());

    // Before we check to see if the field is in use, see if there's a
    // deprecated default enum value.
    checkForDeprecatedNonRepeatedEnumValue(message, filename, &field, reflection, runtime_);

    // If this field is not in use, continue.
    if ((field.is_repeated() && reflection->FieldSize(message, &field) == 0) ||
        (!field.is_repeated() && !reflection->HasField(message, &field))) {
      return nullptr;
    }

    // If this field is deprecated, warn or throw an error.
    if (field.options().deprecated()) {
      const std::string warning = absl::StrCat(
          "Using {}deprecated option '", field.full_name(), "' from file ", filename,
          ". This configuration will be removed from "
          "Envoy soon. Please see https://www.envoyproxy.io/docs/envoy/latest/intro/deprecated "
          "for details.");

      deprecatedFieldHelper(runtime_, true /*deprecated*/,
                            field.options().GetExtension(envoy::annotations::disallowed_by_default),
                            absl::StrCat("envoy.deprecated_features:", field.full_name()), warning,
                            message);
    }
    return nullptr;
  }

  void onMessage(const Protobuf::Message& message, const void*) override {
    // Reject unknown fields.
    const auto& unknown_fields = message.GetReflection()->GetUnknownFields(message);
    if (!unknown_fields.empty()) {
      std::string error_msg;
      for (int n = 0; n < unknown_fields.field_count(); ++n) {
        if (unknown_fields.field(n).number() == ProtobufWellKnown::OriginalTypeFieldNumber) {
          continue;
        }
        error_msg += absl::StrCat(n > 0 ? ", " : "", unknown_fields.field(n).number());
      }
      // We use the validation visitor but have hard coded behavior below for deprecated fields.
      // TODO(htuch): Unify the deprecated and unknown visitor handling behind the validation
      // visitor pattern. https://github.com/envoyproxy/envoy/issues/8092.
      if (!error_msg.empty()) {
        validation_visitor_.onUnknownField("type " + message.GetTypeName() +
                                           " with unknown field set {" + error_msg + "}");
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
                                           Runtime::Loader* runtime) {
  UnexpectedFieldProtoVisitor unexpected_field_visitor(validation_visitor, runtime);
  ProtobufMessage::traverseMessage(unexpected_field_visitor, API_RECOVER_ORIGINAL(message),
                                   nullptr);
}

std::string MessageUtil::getYamlStringFromMessage(const Protobuf::Message& message,
                                                  const bool block_print,
                                                  const bool always_print_primitive_fields) {
  std::string json = getJsonStringFromMessage(message, false, always_print_primitive_fields);
  YAML::Node node;
  try {
    node = YAML::Load(json);
  } catch (YAML::ParserException& e) {
    throw EnvoyException(e.what());
  } catch (YAML::BadConversion& e) {
    throw EnvoyException(e.what());
  } catch (std::exception& e) {
    // There is a potentially wide space of exceptions thrown by the YAML parser,
    // and enumerating them all may be difficult. Envoy doesn't work well with
    // unhandled exceptions, so we capture them and record the exception name in
    // the Envoy Exception text.
    throw EnvoyException(fmt::format("Unexpected YAML exception: {}", +e.what()));
  }
  if (block_print) {
    blockFormat(node);
  }
  YAML::Emitter out;
  out << node;
  return out.c_str();
}

std::string MessageUtil::getJsonStringFromMessage(const Protobuf::Message& message,
                                                  const bool pretty_print,
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
  const auto status = Protobuf::util::MessageToJsonString(message, &json, json_options);
  // This should always succeed unless something crash-worthy such as out-of-memory.
  RELEASE_ASSERT(status.ok(), "");
  return json;
}

void MessageUtil::unpackTo(const ProtobufWkt::Any& any_message, Protobuf::Message& message) {
  // If we don't have a type URL match, try an earlier version.
  const absl::string_view any_full_name =
      TypeUtil::typeUrlToDescriptorFullName(any_message.type_url());
  if (any_full_name != message.GetDescriptor()->full_name()) {
    const Protobuf::Descriptor* earlier_version_desc =
        Config::ApiTypeOracle::getEarlierVersionDescriptor(message.GetDescriptor()->full_name());
    // If the earlier version matches, unpack and upgrade.
    if (earlier_version_desc != nullptr && any_full_name == earlier_version_desc->full_name()) {
      Protobuf::DynamicMessageFactory dmf;
      auto earlier_message =
          ProtobufTypes::MessagePtr(dmf.GetPrototype(earlier_version_desc)->New());
      ASSERT(earlier_message != nullptr);
      if (!any_message.UnpackTo(earlier_message.get())) {
        throw EnvoyException(fmt::format("Unable to unpack as {}: {}",
                                         earlier_message->GetDescriptor()->full_name(),
                                         any_message.DebugString()));
      }
      Config::VersionConverter::upgrade(*earlier_message, message);
      return;
    }
  }
  // Otherwise, just unpack to the message. Type URL mismatches will be signaled
  // by UnpackTo failure.
  if (!any_message.UnpackTo(&message)) {
    throw EnvoyException(fmt::format("Unable to unpack as {}: {}",
                                     message.GetDescriptor()->full_name(),
                                     any_message.DebugString()));
  }
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

void MessageUtil::jsonConvertValue(const Protobuf::Message& source, ProtobufWkt::Value& dest) {
  jsonConvertInternal(source, ProtobufMessage::getNullValidationVisitor(), dest);
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

std::string MessageUtil::CodeEnumToString(ProtobufUtil::error::Code code) {
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
  ASSERT(type_url_field_descriptor != nullptr && value_field_descriptor != nullptr &&
         reflection->HasField(*message, type_url_field_descriptor));
  if (!reflection->HasField(*message, value_field_descriptor)) {
    return true;
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
bool redactTypedStruct(Protobuf::Message* message, bool ancestor_is_sensitive) {
  return redactOpaque(
      message, ancestor_is_sensitive, "udpa.type.v1.TypedStruct",
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
      redactTypedStruct(message, ancestor_is_sensitive)) {
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
      if (field_descriptor->is_repeated()) {
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

ProtobufWkt::Value ValueUtil::loadFromYaml(const std::string& yaml) {
  try {
    return parseYamlNode(YAML::Load(yaml));
  } catch (YAML::ParserException& e) {
    throw EnvoyException(e.what());
  } catch (YAML::BadConversion& e) {
    throw EnvoyException(e.what());
  } catch (std::exception& e) {
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

  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
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

void validateDuration(const ProtobufWkt::Duration& duration) {
  if (duration.seconds() < 0 || duration.nanos() < 0) {
    throw DurationUtil::OutOfRangeException(
        fmt::format("Expected positive duration: {}", duration.DebugString()));
  }
  if (duration.nanos() > 999999999 ||
      duration.seconds() > Protobuf::util::TimeUtil::kDurationMaxSeconds) {
    throw DurationUtil::OutOfRangeException(
        fmt::format("Duration out-of-range: {}", duration.DebugString()));
  }
}

} // namespace

uint64_t DurationUtil::durationToMilliseconds(const ProtobufWkt::Duration& duration) {
  validateDuration(duration);
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

} // namespace Envoy
