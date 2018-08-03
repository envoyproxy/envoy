#include "common/protobuf/utility.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
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

uint64_t fractionalPercentDenominatorToInt(const envoy::type::FractionalPercent& percent) {
  switch (percent.denominator()) {
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

void MessageUtil::loadFromJson(const std::string& json, Protobuf::Message& message) {
  Protobuf::util::JsonParseOptions options;
  options.ignore_unknown_fields = true;
  const auto status = Protobuf::util::JsonStringToMessage(json, &message, options);
  if (!status.ok()) {
    throw EnvoyException("Unable to parse JSON as proto (" + status.ToString() + "): " + json);
  }
}

void MessageUtil::loadFromYaml(const std::string& yaml, Protobuf::Message& message) {
  const std::string json = Json::Factory::loadFromYamlString(yaml)->asJsonString();
  loadFromJson(json, message);
}

void MessageUtil::loadFromFile(const std::string& path, Protobuf::Message& message) {
  const std::string contents = Filesystem::fileReadToEnd(path);
  // If the filename ends with .pb, attempt to parse it as a binary proto.
  if (StringUtil::endsWith(path, ".pb")) {
    // Attempt to parse the binary format.
    if (message.ParseFromString(contents)) {
      return;
    }
    throw EnvoyException("Unable to parse file \"" + path + "\" as a binary protobuf (type " +
                         message.GetTypeName() + ")");
  }
  // If the filename ends with .pb_text, attempt to parse it as a text proto.
  if (StringUtil::endsWith(path, ".pb_text")) {
    if (Protobuf::TextFormat::ParseFromString(contents, &message)) {
      return;
    }
    throw EnvoyException("Unable to parse file \"" + path + "\" as a text protobuf (type " +
                         message.GetTypeName() + ")");
  }
  if (StringUtil::endsWith(path, ".yaml")) {
    loadFromYaml(contents, message);
  } else {
    loadFromJson(contents, message);
  }
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
  ProtobufTypes::String json;
  const auto status = Protobuf::util::MessageToJsonString(message, &json, json_options);
  // This should always succeed unless something crash-worthy such as out-of-memory.
  RELEASE_ASSERT(status.ok(), "");
  return json;
}

void MessageUtil::jsonConvert(const Protobuf::Message& source, Protobuf::Message& dest) {
  // TODO(htuch): Consolidate with the inflight cleanups here.
  Protobuf::util::JsonPrintOptions json_options;
  json_options.preserve_proto_field_names = true;
  ProtobufTypes::String json;
  const auto status = Protobuf::util::MessageToJsonString(source, &json, json_options);
  if (!status.ok()) {
    throw EnvoyException(fmt::format("Unable to convert protobuf message to JSON string: {} {}",
                                     status.ToString(), source.DebugString()));
  }
  MessageUtil::loadFromJson(json, dest);
}

ProtobufWkt::Struct MessageUtil::keyValueStruct(const std::string& key, const std::string& value) {
  ProtobufWkt::Struct struct_obj;
  ProtobufWkt::Value val;
  val.set_string_value(value);
  (*struct_obj.mutable_fields())[key] = val;
  return struct_obj;
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
