#pragma once

#include <numeric>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/json/json_object.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/percent.pb.h"

#include "common/common/hash.h"
#include "common/common/utility.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"
#include "common/singleton/const_singleton.h"

// Obtain the value of a wrapped field (e.g. google.protobuf.UInt32Value) if set. Otherwise, return
// the default value.
#define PROTOBUF_GET_WRAPPED_OR_DEFAULT(message, field_name, default_value)                        \
  ((message).has_##field_name() ? (message).field_name().value() : (default_value))

// Obtain the value of a wrapped field (e.g. google.protobuf.UInt32Value) if set. Otherwise, throw
// a MissingFieldException.
#define PROTOBUF_GET_WRAPPED_REQUIRED(message, field_name)                                         \
  ((message).has_##field_name() ? (message).field_name().value()                                   \
                                : throw MissingFieldException(#field_name, (message)))

// Obtain the milliseconds value of a google.protobuf.Duration field if set. Otherwise, return the
// default value.
#define PROTOBUF_GET_MS_OR_DEFAULT(message, field_name, default_value)                             \
  ((message).has_##field_name() ? DurationUtil::durationToMilliseconds((message).field_name())     \
                                : (default_value))

// Obtain the string value if the field is set. Otherwise, return the default value.
#define PROTOBUF_GET_STRING_OR_DEFAULT(message, field_name, default_value)                         \
  (!(message).field_name().empty() ? (message).field_name() : (default_value))

// Obtain the milliseconds value of a google.protobuf.Duration field if set. Otherwise, return
// absl::nullopt.
#define PROTOBUF_GET_OPTIONAL_MS(message, field_name)                                              \
  ((message).has_##field_name()                                                                    \
       ? absl::optional<std::chrono::milliseconds>(                                                \
             DurationUtil::durationToMilliseconds((message).field_name()))                         \
       : absl::nullopt)

// Obtain the milliseconds value of a google.protobuf.Duration field if set. Otherwise, throw a
// MissingFieldException.
#define PROTOBUF_GET_MS_REQUIRED(message, field_name)                                              \
  ((message).has_##field_name() ? DurationUtil::durationToMilliseconds((message).field_name())     \
                                : throw MissingFieldException(#field_name, (message)))

// Obtain the seconds value of a google.protobuf.Duration field if set. Otherwise, throw a
// MissingFieldException.
#define PROTOBUF_GET_SECONDS_REQUIRED(message, field_name)                                         \
  ((message).has_##field_name() ? DurationUtil::durationToSeconds((message).field_name())          \
                                : throw MissingFieldException(#field_name, (message)))

namespace Envoy {
namespace ProtobufPercentHelper {

// The following are helpers used in the PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT macro.
// This avoids a giant macro mess when trying to do asserts, casts, etc.
uint64_t checkAndReturnDefault(uint64_t default_value, uint64_t max_value);
uint64_t convertPercent(double percent, uint64_t max_value);

/**
 * Given a fractional percent chance of a given event occurring, evaluate to a yes/no decision
 * based on a provided random value.
 * @param percent the chance of a given event happening.
 * @param random_value supplies a numerical value to use to evaluate the event.
 * @return bool decision about whether the event should occur.
 */
bool evaluateFractionalPercent(envoy::type::FractionalPercent percent, uint64_t random_value);

/**
 * Convert a fractional percent denominator enum into an integer.
 * @param denominator supplies denominator to convert.
 * @return the converted denominator.
 */
uint64_t fractionalPercentDenominatorToInt(
    const envoy::type::FractionalPercent::DenominatorType& denominator);

} // namespace ProtobufPercentHelper
} // namespace Envoy

// Convert an envoy::api::v2::core::Percent to a double or a default.
// @param message supplies the proto message containing the field.
// @param field_name supplies the field name in the message.
// @param default_value supplies the default if the field is not present.
#define PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(message, field_name, default_value)                  \
  (!std::isnan((message).field_name().value())                                                     \
       ? (message).has_##field_name() ? (message).field_name().value() : default_value             \
       : throw EnvoyException(fmt::format("Value not in the range of 0..100 range.")))

// Convert an envoy::api::v2::core::Percent to a rounded integer or a default.
// @param message supplies the proto message containing the field.
// @param field_name supplies the field name in the message.
// @param max_value supplies the maximum allowed integral value (e.g., 100, 10000, etc.).
// @param default_value supplies the default if the field is not present.
//
// TODO(anirudhmurali): Recommended to capture and validate NaN values in PGV
// Issue: https://github.com/envoyproxy/protoc-gen-validate/issues/85
#define PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(message, field_name, max_value,             \
                                                       default_value)                              \
  (!std::isnan((message).field_name().value())                                                     \
       ? (message).has_##field_name()                                                              \
             ? ProtobufPercentHelper::convertPercent((message).field_name().value(), max_value)    \
             : ProtobufPercentHelper::checkAndReturnDefault(default_value, max_value)              \
       : throw EnvoyException(fmt::format("Value not in the range of 0..100 range.")))

namespace Envoy {

class MissingFieldException : public EnvoyException {
public:
  MissingFieldException(const std::string& field_name, const Protobuf::Message& message);
};

class RepeatedPtrUtil {
public:
  static std::string join(const Protobuf::RepeatedPtrField<std::string>& source,
                          const std::string& delimiter) {
    return StringUtil::join(std::vector<std::string>(source.begin(), source.end()), delimiter);
  }

  template <class ProtoType>
  static std::string debugString(const Protobuf::RepeatedPtrField<ProtoType>& source) {
    if (source.empty()) {
      return "[]";
    }
    return std::accumulate(std::next(source.begin()), source.end(), "[" + source[0].DebugString(),
                           [](std::string debug_string, const Protobuf::Message& message) {
                             return debug_string + ", " + message.DebugString();
                           }) +
           "]";
  }

  // Based on MessageUtil::hash() defined below.
  template <class ProtoType>
  static std::size_t hash(const Protobuf::RepeatedPtrField<ProtoType>& source) {
    // Use Protobuf::io::CodedOutputStream to force deterministic serialization, so that the same
    // message doesn't hash to different values.
    std::string text;
    {
      // For memory safety, the StringOutputStream needs to be destroyed before
      // we read the string.
      Protobuf::io::StringOutputStream string_stream(&text);
      Protobuf::io::CodedOutputStream coded_stream(&string_stream);
      coded_stream.SetSerializationDeterministic(true);
      for (const auto& message : source) {
        message.SerializeToCodedStream(&coded_stream);
      }
    }
    return HashUtil::xxHash64(text);
  }

  /**
   * Converts a proto repeated field into a container of const Protobuf::Message unique_ptr's.
   *
   * @param repeated_field the proto repeated field to convert.
   * @return ReturnType the container of const Message pointers.
   */
  template <typename ProtoType, typename ReturnType>
  static ReturnType
  convertToConstMessagePtrContainer(const Protobuf::RepeatedPtrField<ProtoType>& repeated_field) {
    ReturnType ret_container;
    std::transform(repeated_field.begin(), repeated_field.end(), std::back_inserter(ret_container),
                   [](const ProtoType& proto_message) -> std::unique_ptr<const Protobuf::Message> {
                     Protobuf::Message* clone = proto_message.New();
                     clone->MergeFrom(proto_message);
                     return std::unique_ptr<const Protobuf::Message>(clone);
                   });
    return ret_container;
  }
};

class ProtoValidationException : public EnvoyException {
public:
  ProtoValidationException(const std::string& validation_error, const Protobuf::Message& message);
};

class MessageUtil {
public:
  // std::hash
  std::size_t operator()(const Protobuf::Message& message) const { return hash(message); }

  // std::equals_to
  bool operator()(const Protobuf::Message& lhs, const Protobuf::Message& rhs) const {
    return Protobuf::util::MessageDifferencer::Equivalent(lhs, rhs);
  }

  class FileExtensionValues {
  public:
    const std::string ProtoBinary = ".pb";
    const std::string ProtoBinaryLengthDelimited = ".pb_length_delimited";
    const std::string ProtoText = ".pb_text";
    const std::string Json = ".json";
    const std::string Yaml = ".yaml";
  };

  using FileExtensions = ConstSingleton<FileExtensionValues>;

  /**
   * A hash function uses Protobuf::TextFormat to force deterministic serialization recursively
   * including known types in google.protobuf.Any. See
   * https://github.com/protocolbuffers/protobuf/issues/5731 for the context.
   * Using this function is discouraged, see discussion in
   * https://github.com/envoyproxy/envoy/issues/8301.
   */
  static std::size_t hash(const Protobuf::Message& message);

  static void loadFromJson(const std::string& json, Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor);
  static void loadFromJson(const std::string& json, ProtobufWkt::Struct& message);
  static void loadFromYaml(const std::string& yaml, Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor);
  static void loadFromFile(const std::string& path, Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  /**
   * Checks for use of deprecated fields in message and all sub-messages.
   * @param message message to validate.
   * @param loader optional a pointer to the runtime loader for live deprecation status.
   * @throw ProtoValidationException if deprecated fields are used and listed
   *    in disallowed_features in runtime_features.h
   */
  static void
  checkForUnexpectedFields(const Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor,
                           Runtime::Loader* loader = Runtime::LoaderSingleton::getExisting());

  /**
   * Validate protoc-gen-validate constraints on a given protobuf.
   * Note the corresponding `.pb.validate.h` for the message has to be included in the source file
   * of caller.
   * @param message message to validate.
   * @throw ProtoValidationException if the message does not satisfy its type constraints.
   */
  template <class MessageType>
  static void validate(const MessageType& message,
                       ProtobufMessage::ValidationVisitor& validation_visitor) {
    // Log warnings or throw errors if deprecated fields or unknown fields are in use.
    checkForUnexpectedFields(message, validation_visitor);

    std::string err;
    if (!Validate(message, &err)) {
      throw ProtoValidationException(err, message);
    }
  }

  template <class MessageType>
  static void loadFromFileAndValidate(const std::string& path, MessageType& message,
                                      ProtobufMessage::ValidationVisitor& validation_visitor) {
    loadFromFile(path, message, validation_visitor);
    validate(message, validation_visitor);
  }

  template <class MessageType>
  static void loadFromYamlAndValidate(const std::string& yaml, MessageType& message,
                                      ProtobufMessage::ValidationVisitor& validation_visitor) {
    loadFromYaml(yaml, message, validation_visitor);
    validate(message, validation_visitor);
  }

  /**
   * Downcast and validate protoc-gen-validate constraints on a given protobuf.
   * Note the corresponding `.pb.validate.h` for the message has to be included in the source file
   * of caller.
   * @param message const Protobuf::Message& to downcast and validate.
   * @return const MessageType& the concrete message type downcasted to on success.
   * @throw ProtoValidationException if the message does not satisfy its type constraints.
   */
  template <class MessageType>
  static const MessageType&
  downcastAndValidate(const Protobuf::Message& config,
                      ProtobufMessage::ValidationVisitor& validation_visitor) {
    const auto& typed_config = dynamic_cast<MessageType>(config);
    validate(typed_config, validation_visitor);
    return typed_config;
  }

  /**
   * Convert from google.protobuf.Any to a typed message.
   * @param message source google.protobuf.Any message.
   * @param validation_visitor message validation visitor instance.
   *
   * @return MessageType the typed message inside the Any.
   */
  template <class MessageType>
  static inline MessageType anyConvert(const ProtobufWkt::Any& message) {
    MessageType typed_message;
    if (!message.UnpackTo(&typed_message)) {
      throw EnvoyException("Unable to unpack " + message.DebugString());
    }
    return typed_message;
  };

  /**
   * Convert between two protobufs via a JSON round-trip. This is used to translate arbitrary
   * messages to/from google.protobuf.Struct.
   * TODO(htuch): Avoid round-tripping via JSON strings by doing whatever
   * Protobuf::util::MessageToJsonString does but generating a google.protobuf.Struct instead.
   * @param source message.
   * @param dest message.
   */
  static void jsonConvert(const Protobuf::Message& source, ProtobufWkt::Struct& dest);
  static void jsonConvert(const ProtobufWkt::Struct& source,
                          ProtobufMessage::ValidationVisitor& validation_visitor,
                          Protobuf::Message& dest);

  /**
   * Extract YAML as string from a google.protobuf.Message.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @param block_print whether the returned JSON should be in block style rather than flow style.
   * @param always_print_primitive_fields whether to include primitive fields set to their default
   * values, e.g. an int32 set to 0 or a bool set to false.
   * @return std::string of formatted YAML object.
   */
  static std::string getYamlStringFromMessage(const Protobuf::Message& message,
                                              const bool block_print = true,
                                              const bool always_print_primitive_fields = false);

  /**
   * Extract JSON as string from a google.protobuf.Message.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @param pretty_print whether the returned JSON should be formatted.
   * @param always_print_primitive_fields whether to include primitive fields set to their default
   * values, e.g. an int32 set to 0 or a bool set to false.
   * @return std::string of formatted JSON object.
   */
  static std::string getJsonStringFromMessage(const Protobuf::Message& message,
                                              bool pretty_print = false,
                                              bool always_print_primitive_fields = false);

  /**
   * Extract JSON object from a google.protobuf.Message.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @return Json::ObjectSharedPtr of JSON object or nullptr if unable to extract.
   */
  static Json::ObjectSharedPtr getJsonObjectFromMessage(const Protobuf::Message& message) {
    return Json::Factory::loadFromString(MessageUtil::getJsonStringFromMessage(message));
  }

  /**
   * Utility method to create a Struct containing the passed in key/value strings.
   *
   * @param key the key to use to set the value
   * @param value the string value to associate with the key
   */
  static ProtobufWkt::Struct keyValueStruct(const std::string& key, const std::string& value);

  /**
   * Utility method to print a human readable string of the code passed in.
   *
   * @param code the protobuf error code
   */
  static std::string CodeEnumToString(ProtobufUtil::error::Code code);
};

class ValueUtil {
public:
  static std::size_t hash(const ProtobufWkt::Value& value) { return MessageUtil::hash(value); }

  /**
   * Compare two ProtobufWkt::Values for equality.
   * @param v1 message of type type.googleapis.com/google.protobuf.Value
   * @param v2 message of type type.googleapis.com/google.protobuf.Value
   * @return true if v1 and v2 are identical
   */
  static bool equal(const ProtobufWkt::Value& v1, const ProtobufWkt::Value& v2);
};

/**
 * HashedValue is a wrapper around ProtobufWkt::Value that computes
 * and stores a hash code for the Value at construction.
 */
class HashedValue {
public:
  HashedValue(const ProtobufWkt::Value& value) : value_(value), hash_(ValueUtil::hash(value)){};
  HashedValue(const HashedValue& v) = default;

  const ProtobufWkt::Value& value() const { return value_; }
  std::size_t hash() const { return hash_; }

  bool operator==(const HashedValue& rhs) const {
    return hash_ == rhs.hash_ && ValueUtil::equal(value_, rhs.value_);
  }

  bool operator!=(const HashedValue& rhs) const { return !(*this == rhs); }

private:
  const ProtobufWkt::Value value_;
  const std::size_t hash_;
};

class DurationUtil {
public:
  class OutOfRangeException : public EnvoyException {
  public:
    OutOfRangeException(const std::string& error) : EnvoyException(error) {}
  };

  /**
   * Same as DurationUtil::durationToMilliseconds but with extra validation logic.
   * Same as Protobuf::util::TimeUtil::DurationToSeconds but with extra validation logic.
   * Specifically, we ensure that the duration is positive.
   * @param duration protobuf.
   * @return duration in milliseconds.
   * @throw OutOfRangeException when duration is out-of-range.
   */
  static uint64_t durationToMilliseconds(const ProtobufWkt::Duration& duration);

  /**
   * Same as Protobuf::util::TimeUtil::DurationToSeconds but with extra validation logic.
   * Specifically, we ensure that the duration is positive.
   * @param duration protobuf.
   * @return duration in seconds.
   * @throw OutOfRangeException when duration is out-of-range.
   */
  static uint64_t durationToSeconds(const ProtobufWkt::Duration& duration);
};

class TimestampUtil {
public:
  /**
   * Writes a time_point<system_clock> (SystemTime) to a protobuf Timestamp, by way of time_t.
   * @param system_clock_time the time to write
   * @param timestamp a pointer to the mutable protobuf member to be written into.
   */
  static void systemClockToTimestamp(const SystemTime system_clock_time,
                                     ProtobufWkt::Timestamp& timestamp);
};

} // namespace Envoy

namespace std {
// Inject an implementation of std::hash for Envoy::HashedValue into the std namespace.
template <> struct hash<Envoy::HashedValue> {
  std::size_t operator()(Envoy::HashedValue const& v) const { return v.hash(); }
};

} // namespace std
