#pragma once

#include <numeric>

#include "envoy/common/exception.h"
#include "envoy/json/json_object.h"
#include "envoy/type/percent.pb.h"

#include "common/common/hash.h"
#include "common/common/utility.h"
#include "common/json/json_loader.h"
#include "common/protobuf/protobuf.h"

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
 * Convert a fractional percent denominator enum into an integer.
 * @param percent supplies percent to convert.
 * @return the converted denominator.
 */
uint64_t fractionalPercentDenominatorToInt(const envoy::type::FractionalPercent& percent);

} // namespace ProtobufPercentHelper
} // namespace Envoy

// Convert an envoy::api::v2::core::Percent to a rounded integer or a default.
// @param message supplies the proto message containing the field.
// @param field_name supplies the field name in the message.
// @param max_value supplies the maximum allowed integral value (e.g., 100, 10000, etc.).
// @param default_value supplies the default if the field is not present.
#define PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(message, field_name, max_value,             \
                                                       default_value)                              \
  ((message).has_##field_name()                                                                    \
       ? ProtobufPercentHelper::convertPercent((message).field_name().value(), max_value)          \
       : ProtobufPercentHelper::checkAndReturnDefault(default_value, max_value))

namespace Envoy {

class MissingFieldException : public EnvoyException {
public:
  MissingFieldException(const std::string& field_name, const Protobuf::Message& message);
};

class RepeatedPtrUtil {
public:
  static std::string join(const Protobuf::RepeatedPtrField<ProtobufTypes::String>& source,
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
    ProtobufTypes::String text;
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

  static std::size_t hash(const Protobuf::Message& message) {
    // Use Protobuf::io::CodedOutputStream to force deterministic serialization, so that the same
    // message doesn't hash to different values.
    ProtobufTypes::String text;
    {
      // For memory safety, the StringOutputStream needs to be destroyed before
      // we read the string.
      Protobuf::io::StringOutputStream string_stream(&text);
      Protobuf::io::CodedOutputStream coded_stream(&string_stream);
      coded_stream.SetSerializationDeterministic(true);
      message.SerializeToCodedStream(&coded_stream);
    }
    return HashUtil::xxHash64(text);
  }

  static void loadFromJson(const std::string& json, Protobuf::Message& message);
  static void loadFromYaml(const std::string& yaml, Protobuf::Message& message);
  static void loadFromFile(const std::string& path, Protobuf::Message& message);

  /**
   * Validate protoc-gen-validate constraints on a given protobuf.
   * Note the corresponding `.pb.validate.h` for the message has to be included in the source file
   * of caller.
   * @param message message to validate.
   * @throw ProtoValidationException if the message does not satisfy its type constraints.
   */
  template <class MessageType> static void validate(const MessageType& message) {
    std::string err;
    if (!Validate(message, &err)) {
      throw ProtoValidationException(err, message);
    }
  }

  template <class MessageType>
  static void loadFromFileAndValidate(const std::string& path, MessageType& message) {
    loadFromFile(path, message);
    validate(message);
  }

  template <class MessageType>
  static void loadFromYamlAndValidate(const std::string& yaml, MessageType& message) {
    loadFromYaml(yaml, message);
    validate(message);
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
  static const MessageType& downcastAndValidate(const Protobuf::Message& config) {
    const auto& typed_config = dynamic_cast<MessageType>(config);
    validate(typed_config);
    return typed_config;
  }

  /**
   * Convert from google.protobuf.Any to a typed message.
   * @param message source google.protobuf.Any message.
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
  static void jsonConvert(const Protobuf::Message& source, Protobuf::Message& dest);

  /**
   * Extract JSON as string from a google.protobuf.Message.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @param pretty_print whether the returned JSON should be formatted.
   * @return std::string of formatted JSON object.
   */
  static std::string getJsonStringFromMessage(const Protobuf::Message& message,
                                              bool pretty_print = false);

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
  HashedValue(const HashedValue& v) : value_(v.value_), hash_(v.hash_){};

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

} // namespace Envoy

namespace std {
// Inject an implementation of std::hash for Envoy::HashedValue into the std namespace.
template <> struct hash<Envoy::HashedValue> {
  std::size_t operator()(Envoy::HashedValue const& v) const { return v.hash(); }
};

} // namespace std
