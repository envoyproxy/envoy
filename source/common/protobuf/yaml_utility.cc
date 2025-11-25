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
#include "source/common/protobuf/utility.h"
#include "source/common/protobuf/visitor.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/match.h"
#include "udpa/annotations/sensitive.pb.h"
#include "udpa/annotations/status.pb.h"
#include "utf8_validity.h"
#include "validate/validate.h"
#include "xds/annotations/v3/status.pb.h"
#include "yaml-cpp/yaml.h"

using namespace std::chrono_literals;

namespace Envoy {
namespace {

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

Protobuf::Value parseYamlNode(const YAML::Node& node) {
  Protobuf::Value value;
  switch (node.Type()) {
  case YAML::NodeType::Null:
    value.set_null_value(Protobuf::NULL_VALUE);
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
        // Protobuf::Struct itself, only convert small numbers into number_value here.
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

} // namespace

void MessageUtil::loadFromJson(absl::string_view json, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
  bool has_unknown_field;
  auto load_status = loadFromJsonNoThrow(json, message, has_unknown_field);
  if (load_status.ok()) {
    return;
  }
  if (has_unknown_field) {
    // If the parsing failure is caused by the unknown fields.
    THROW_IF_NOT_OK(validation_visitor.onUnknownField(
        fmt::format("type {} reason {}", message.GetTypeName(), load_status.ToString())));
  } else {
    // If the error has nothing to do with unknown field.
    throw EnvoyException(
        fmt::format("Unable to parse JSON as proto ({}): {}", load_status.ToString(), json));
  }
}

absl::Status MessageUtil::loadFromJsonNoThrow(absl::string_view json, Protobuf::Message& message,
                                              bool& has_unknown_fileld) {
  has_unknown_fileld = false;
  Protobuf::util::JsonParseOptions options;
  options.case_insensitive_enum_parsing = true;
  // Let's first try and get a clean parse when checking for unknown fields;
  // this should be the common case.
  options.ignore_unknown_fields = false;
  // Clear existing values (if any) from the destination message before serialization.
  message.Clear();
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

absl::Status MessageUtil::loadFromJsonNoThrow(absl::string_view json, Protobuf::Struct& message) {
  message.Clear();
  return Protobuf::util::JsonStringToMessage(json, &message);
}

void MessageUtil::loadFromJson(absl::string_view json, Protobuf::Struct& message) {
  // No need to validate if converting to a Struct, since there are no unknown
  // fields possible.
  loadFromJson(json, message, ProtobufMessage::getNullValidationVisitor());
}

void MessageUtil::loadFromYaml(const std::string& yaml, Protobuf::Message& message,
                               ProtobufMessage::ValidationVisitor& validation_visitor) {
  Protobuf::Value value = ValueUtil::loadFromYaml(yaml);
  if (value.kind_case() == Protobuf::Value::kStructValue ||
      value.kind_case() == Protobuf::Value::kListValue) {
    jsonConvertInternal(value, validation_visitor, message);
    return;
  }
  throw EnvoyException("Unable to convert YAML as JSON: " + yaml);
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

absl::StatusOr<std::string>
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
    json_options.always_print_fields_with_no_presence = true;
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

void MessageUtil::jsonConvert(const Protobuf::Message& source, Protobuf::Message& dest) {
  jsonConvertInternal(source, ProtobufMessage::getNullValidationVisitor(), dest);
}

void MessageUtil::jsonConvert(const Protobuf::Message& source, Protobuf::Struct& dest) {
  // Any proto3 message can be transformed to Struct, so there is no need to check for unknown
  // fields. There is one catch; Duration/Timestamp etc. which have non-object canonical JSON
  // representations don't work.
  jsonConvertInternal(source, ProtobufMessage::getNullValidationVisitor(), dest);
}

void MessageUtil::jsonConvert(const Protobuf::Struct& source,
                              ProtobufMessage::ValidationVisitor& validation_visitor,
                              Protobuf::Message& dest) {
  jsonConvertInternal(source, validation_visitor, dest);
}

bool MessageUtil::jsonConvertValue(const Protobuf::Message& source, Protobuf::Value& dest) {
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

Protobuf::Value ValueUtil::loadFromYaml(const std::string& yaml) {
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

namespace {

// This is a modified copy of the UTF8CoerceToStructurallyValid from the protobuf code.
// A copy was needed after if was removed from the protobuf.
std::string utf8CoerceToStructurallyValid(absl::string_view str, const char replace_char) {
  std::string result(str);
  auto replace_pos = result.begin();
  while (!str.empty()) {
    size_t n_valid_bytes = utf8_range::SpanStructurallyValid(str);
    if (n_valid_bytes == str.size()) {
      break;
    }
    replace_pos += n_valid_bytes;
    *replace_pos++ = replace_char; // replace one bad byte
    str.remove_prefix(n_valid_bytes + 1);
  }
  return result;
}

} // namespace

std::string MessageUtil::sanitizeUtf8String(absl::string_view input) {
  // The choice of '!' is somewhat arbitrary, but we wanted to avoid any character that has
  // special semantic meaning in URLs or similar.
  return utf8CoerceToStructurallyValid(input, '!');
}

} // namespace Envoy
