#include "common/protobuf/utility.h"

#include <numeric>

#include "envoy/protobuf/message_validator.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/json/json_loader.h"
#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/match.h"
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

bool evaluateFractionalPercent(envoy::type::FractionalPercent percent, uint64_t random_value) {
  return random_value % fractionalPercentDenominatorToInt(percent.denominator()) <
         percent.numerator();
}

uint64_t fractionalPercentDenominatorToInt(
    const envoy::type::FractionalPercent::DenominatorType& denominator) {
  switch (denominator) {
  case envoy::type::FractionalPercent::HUNDRED:
    return 100;
  case envoy::type::FractionalPercent::TEN_THOUSAND:
    return 10000;
  case envoy::type::FractionalPercent::MILLION:
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

void MessageUtil::loadFromJson(const std::string& json, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
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
  // We know it's an unknown field at this point.
  validation_visitor.onUnknownField("type " + message.GetTypeName() + " reason " +
                                    strict_status.ToString());
}

void MessageUtil::loadFromJson(const std::string& json, ProtobufWkt::Struct& message) {
  // No need to validate if converting to a Struct, since there are no unknown
  // fields possible.
  return loadFromJson(json, message, ProtobufMessage::getNullValidationVisitor());
}

void MessageUtil::loadFromYaml(const std::string& yaml, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto loaded_object = Json::Factory::loadFromYamlString(yaml);
  // Load the message if the loaded object has type Object or Array.
  if (loaded_object->isObject() || loaded_object->isArray()) {
    const std::string json = loaded_object->asJsonString();
    loadFromJson(json, message, validation_visitor);
    return;
  }
  throw EnvoyException("Unable to convert YAML as JSON: " + yaml);
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
    if (Protobuf::TextFormat::ParseFromString(contents, &message)) {
      return;
    }
    throw EnvoyException("Unable to parse file \"" + path + "\" as a text protobuf (type " +
                         message.GetTypeName() + ")");
  }
  if (absl::EndsWith(path, FileExtensions::get().Yaml)) {
    loadFromYaml(contents, message, validation_visitor);
  } else {
    loadFromJson(contents, message, validation_visitor);
  }
}

void MessageUtil::checkForUnexpectedFields(const Protobuf::Message& message,
                                           ProtobufMessage::ValidationVisitor& validation_visitor,
                                           Runtime::Loader* runtime) {
  // Reject unknown fields.
  const auto& unknown_fields = message.GetReflection()->GetUnknownFields(message);
  if (!unknown_fields.empty()) {
    std::string error_msg;
    for (int n = 0; n < unknown_fields.field_count(); ++n) {
      error_msg += absl::StrCat(n > 0 ? ", " : "", unknown_fields.field(n).number());
    }
    // We use the validation visitor but have hard coded behavior below for deprecated fields.
    // TODO(htuch): Unify the deprecated and unknown visitor handling behind the validation
    // visitor pattern. https://github.com/envoyproxy/envoy/issues/8092.
    validation_visitor.onUnknownField("type " + message.GetTypeName() +
                                      " with unknown field set {" + error_msg + "}");
  }

  const Protobuf::Descriptor* descriptor = message.GetDescriptor();
  const Protobuf::Reflection* reflection = message.GetReflection();
  for (int i = 0; i < descriptor->field_count(); ++i) {
    const auto* field = descriptor->field(i);

    // If this field is not in use, continue.
    if ((field->is_repeated() && reflection->FieldSize(message, field) == 0) ||
        (!field->is_repeated() && !reflection->HasField(message, field))) {
      continue;
    }

#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
    bool warn_only = false;
#else
    bool warn_only = true;
#endif
    absl::string_view filename = filenameFromPath(field->file()->name());
    // Allow runtime to be null both to not crash if this is called before server initialization,
    // and so proto validation works in context where runtime singleton is not set up (e.g.
    // standalone config validation utilities)
    if (runtime && field->options().deprecated() &&
        !runtime->snapshot().deprecatedFeatureEnabled(
            absl::StrCat("envoy.deprecated_features.", filename, ":", field->name()))) {
      warn_only = false;
    }

    // If this field is deprecated, warn or throw an error.
    if (field->options().deprecated()) {
      std::string err = fmt::format(
          "Using deprecated option '{}' from file {}. This configuration will be removed from "
          "Envoy soon. Please see https://www.envoyproxy.io/docs/envoy/latest/intro/deprecated "
          "for details.",
          field->full_name(), filename);
      if (warn_only) {
        ENVOY_LOG_MISC(warn, "{}", err);
      } else {
        const char fatal_error[] =
            " If continued use of this field is absolutely necessary, see "
            "https://www.envoyproxy.io/docs/envoy/latest/configuration/operations/runtime"
            "#using-runtime-overrides-for-deprecated-features for how to apply a temporary and "
            "highly discouraged override.";
        throw ProtoValidationException(err + fatal_error, message);
      }
    }

    // If this is a message, recurse to check for deprecated fields in the sub-message.
    if (field->cpp_type() == Protobuf::FieldDescriptor::CPPTYPE_MESSAGE) {
      if (field->is_repeated()) {
        const int size = reflection->FieldSize(message, field);
        for (int j = 0; j < size; ++j) {
          checkForUnexpectedFields(reflection->GetRepeatedMessage(message, field, j),
                                   validation_visitor, runtime);
        }
      } else {
        checkForUnexpectedFields(reflection->GetMessage(message, field), validation_visitor,
                                 runtime);
      }
    }
  }
}

std::string MessageUtil::getYamlStringFromMessage(const Protobuf::Message& message,
                                                  const bool block_print,
                                                  const bool always_print_primitive_fields) {
  std::string json = getJsonStringFromMessage(message, false, always_print_primitive_fields);
  auto node = YAML::Load(json);
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

namespace {

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

} // namespace

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

ProtobufWkt::Struct MessageUtil::keyValueStruct(const std::string& key, const std::string& value) {
  ProtobufWkt::Struct struct_obj;
  ProtobufWkt::Value val;
  val.set_string_value(value);
  (*struct_obj.mutable_fields())[key] = val;
  return struct_obj;
}

// TODO(alyssawilk) see if we can get proto's CodeEnumToString made accessible
// to avoid copying it. Otherwise change this to absl::string_view.
std::string MessageUtil::CodeEnumToString(ProtobufUtil::error::Code code) {
  switch (code) {
  case ProtobufUtil::error::OK:
    return "OK";
  case ProtobufUtil::error::CANCELLED:
    return "CANCELLED";
  case ProtobufUtil::error::UNKNOWN:
    return "UNKNOWN";
  case ProtobufUtil::error::INVALID_ARGUMENT:
    return "INVALID_ARGUMENT";
  case ProtobufUtil::error::DEADLINE_EXCEEDED:
    return "DEADLINE_EXCEEDED";
  case ProtobufUtil::error::NOT_FOUND:
    return "NOT_FOUND";
  case ProtobufUtil::error::ALREADY_EXISTS:
    return "ALREADY_EXISTS";
  case ProtobufUtil::error::PERMISSION_DENIED:
    return "PERMISSION_DENIED";
  case ProtobufUtil::error::UNAUTHENTICATED:
    return "UNAUTHENTICATED";
  case ProtobufUtil::error::RESOURCE_EXHAUSTED:
    return "RESOURCE_EXHAUSTED";
  case ProtobufUtil::error::FAILED_PRECONDITION:
    return "FAILED_PRECONDITION";
  case ProtobufUtil::error::ABORTED:
    return "ABORTED";
  case ProtobufUtil::error::OUT_OF_RANGE:
    return "OUT_OF_RANGE";
  case ProtobufUtil::error::UNIMPLEMENTED:
    return "UNIMPLEMENTED";
  case ProtobufUtil::error::INTERNAL:
    return "INTERNAL";
  case ProtobufUtil::error::UNAVAILABLE:
    return "UNAVAILABLE";
  case ProtobufUtil::error::DATA_LOSS:
    return "DATA_LOSS";
  default:
    return "";
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

} // namespace Envoy
