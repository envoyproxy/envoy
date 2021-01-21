#pragma once

#include <numeric>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/v3/percent.pb.h"

#include "common/common/hash.h"
#include "common/common/utility.h"
#include "common/config/version_converter.h"
#include "common/protobuf/protobuf.h"
#include "common/singleton/const_singleton.h"

#include "absl/strings/str_join.h"

// Obtain the value of a wrapped field (e.g. google.protobuf.UInt32Value) if set. Otherwise, return
// the default value.
#define PROTOBUF_GET_WRAPPED_OR_DEFAULT(message, field_name, default_value)                        \
  ((message).has_##field_name() ? (message).field_name().value() : (default_value))

// Obtain the value of a wrapped field (e.g. google.protobuf.UInt32Value) if set. Otherwise, throw
// a MissingFieldException.

#define PROTOBUF_GET_WRAPPED_REQUIRED(message, field_name)                                         \
  ([](const auto& msg) {                                                                           \
    if (!msg.has_##field_name()) {                                                                 \
      ::Envoy::ProtoExceptionUtil::throwMissingFieldException(#field_name, msg);                   \
    }                                                                                              \
    return msg.field_name().value();                                                               \
  }((message)))
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
  ([](const auto& msg) {                                                                           \
    if (!msg.has_##field_name()) {                                                                 \
      ::Envoy::ProtoExceptionUtil::throwMissingFieldException(#field_name, msg);                   \
    }                                                                                              \
    return DurationUtil::durationToMilliseconds(msg.field_name());                                 \
  }((message)))

// Obtain the seconds value of a google.protobuf.Duration field if set. Otherwise, throw a
// MissingFieldException.
#define PROTOBUF_GET_SECONDS_REQUIRED(message, field_name)                                         \
  ([](const auto& msg) {                                                                           \
    if (!msg.has_##field_name()) {                                                                 \
      ::Envoy::ProtoExceptionUtil::throwMissingFieldException(#field_name, msg);                   \
    }                                                                                              \
    return DurationUtil::durationToSeconds(msg.field_name());                                      \
  }((message)))

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
bool evaluateFractionalPercent(envoy::type::v3::FractionalPercent percent, uint64_t random_value);

/**
 * Convert a fractional percent denominator enum into an integer.
 * @param denominator supplies denominator to convert.
 * @return the converted denominator.
 */
uint64_t fractionalPercentDenominatorToInt(
    const envoy::type::v3::FractionalPercent::DenominatorType& denominator);

} // namespace ProtobufPercentHelper
} // namespace Envoy

// Convert an envoy::type::v3::Percent to a double or a default.
// @param message supplies the proto message containing the field.
// @param field_name supplies the field name in the message.
// @param default_value supplies the default if the field is not present.
#define PROTOBUF_PERCENT_TO_DOUBLE_OR_DEFAULT(message, field_name, default_value)                  \
  ([](const auto& msg) -> double {                                                                 \
    if (std::isnan(msg.field_name().value())) {                                                    \
      ::Envoy::ExceptionUtil::throwEnvoyException(                                                 \
          fmt::format("Value not in the range of 0..100 range."));                                 \
    }                                                                                              \
    return (msg).has_##field_name() ? (msg).field_name().value() : default_value;                  \
  }((message)))
// Convert an envoy::type::v3::Percent to a rounded integer or a default.
// @param message supplies the proto message containing the field.
// @param field_name supplies the field name in the message.
// @param max_value supplies the maximum allowed integral value (e.g., 100, 10000, etc.).
// @param default_value supplies the default if the field is not present.
//
// TODO(anirudhmurali): Recommended to capture and validate NaN values in PGV
// Issue: https://github.com/envoyproxy/protoc-gen-validate/issues/85
#define PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(message, field_name, max_value,             \
                                                       default_value)                              \
  ([](const auto& msg) {                                                                           \
    if (std::isnan(msg.field_name().value())) {                                                    \
      ::Envoy::ExceptionUtil::throwEnvoyException(                                                 \
          fmt::format("Value not in the range of 0..100 range."));                                 \
    }                                                                                              \
    return (msg).has_##field_name()                                                                \
               ? ProtobufPercentHelper::convertPercent((msg).field_name().value(), max_value)      \
               : ProtobufPercentHelper::checkAndReturnDefault(default_value, max_value);           \
  }((message)))

namespace Envoy {

/**
 * Exception class for rejecting a deprecated major version.
 */
class DeprecatedMajorVersionException : public EnvoyException {
public:
  DeprecatedMajorVersionException(const std::string& message) : EnvoyException(message) {}
};

class MissingFieldException : public EnvoyException {
public:
  MissingFieldException(const std::string& field_name, const Protobuf::Message& message);
};

class RepeatedPtrUtil {
public:
  static std::string join(const Protobuf::RepeatedPtrField<std::string>& source,
                          const std::string& delimiter) {
    return absl::StrJoin(std::vector<std::string>(source.begin(), source.end()), delimiter);
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
    std::string text;
    {
      Protobuf::TextFormat::Printer printer;
      printer.SetExpandAny(true);
      printer.SetUseFieldNumber(true);
      printer.SetSingleLineMode(true);
      printer.SetHideUnknownFields(true);
      for (const auto& message : source) {
        std::string text_message;
        printer.PrintToString(message, &text_message);
        absl::StrAppend(&text, text_message);
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

/**
 * utility functions to call when throwing exceptions in header files
 */
class ProtoExceptionUtil {
public:
  static void throwMissingFieldException(const std::string& field_name,
                                         const Protobuf::Message& message);
  static void throwProtoValidationException(const std::string& validation_error,
                                            const Protobuf::Message& message);
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
                           ProtobufMessage::ValidationVisitor& validation_visitor,
                           bool do_boosting = true);
  static void loadFromJson(const std::string& json, ProtobufWkt::Struct& message);
  static void loadFromYaml(const std::string& yaml, Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor,
                           bool do_boosting = true);
  static void loadFromYaml(const std::string& yaml, ProtobufWkt::Struct& message);
  static void loadFromFile(const std::string& path, Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api,
                           bool do_boosting = true);

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
    if (!validation_visitor.skipValidation()) {
      checkForUnexpectedFields(message, validation_visitor);
    }

    std::string err;
    if (!Validate(message, &err)) {
      ProtoExceptionUtil::throwProtoValidationException(err, API_RECOVER_ORIGINAL(message));
    }
  }

  template <class MessageType>
  static void loadFromYamlAndValidate(const std::string& yaml, MessageType& message,
                                      ProtobufMessage::ValidationVisitor& validation_visitor,
                                      bool avoid_boosting = false) {
    loadFromYaml(yaml, message, validation_visitor, !avoid_boosting);
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
   * Convert from google.protobuf.Any to a typed message. This should be used
   * instead of the inbuilt UnpackTo as it performs validation of results.
   *
   * @param any_message source google.protobuf.Any message.
   * @param message destination to unpack to.
   *
   * @throw EnvoyException if the message does not unpack.
   */
  static void unpackTo(const ProtobufWkt::Any& any_message, Protobuf::Message& message);

  /**
   * Convert from google.protobuf.Any to a typed message.
   * @param message source google.protobuf.Any message.
   *
   * @return MessageType the typed message inside the Any.
   */
  template <class MessageType>
  static inline void anyConvert(const ProtobufWkt::Any& message, MessageType& typed_message) {
    unpackTo(message, typed_message);
  };

  template <class MessageType>
  static inline MessageType anyConvert(const ProtobufWkt::Any& message) {
    MessageType typed_message;
    anyConvert(message, typed_message);
    return typed_message;
  };

  /**
   * Convert and validate from google.protobuf.Any to a typed message.
   * @param message source google.protobuf.Any message.
   *
   * @return MessageType the typed message inside the Any.
   * @throw ProtoValidationException if the message does not satisfy its type constraints.
   */
  template <class MessageType>
  static inline void anyConvertAndValidate(const ProtobufWkt::Any& message,
                                           MessageType& typed_message,
                                           ProtobufMessage::ValidationVisitor& validation_visitor) {
    anyConvert<MessageType>(message, typed_message);
    validate(typed_message, validation_visitor);
  };

  template <class MessageType>
  static inline MessageType
  anyConvertAndValidate(const ProtobufWkt::Any& message,
                        ProtobufMessage::ValidationVisitor& validation_visitor) {
    MessageType typed_message;
    anyConvertAndValidate<MessageType>(message, typed_message, validation_visitor);
    return typed_message;
  };

  /**
   * Invoke when a version upgrade (e.g. v2 -> v3) is detected. This may warn or throw
   * depending on where we are in the major version deprecation cycle.
   * @param desc description of upgrade to include in warning or exception.
   * @param reject should a DeprecatedMajorVersionException be thrown on failure?
   */
  static void onVersionUpgradeDeprecation(absl::string_view desc, bool reject = true);

  /**
   * Obtain a string field from a protobuf message dynamically.
   *
   * @param message message to extract from.
   * @param field_name field name.
   *
   * @return std::string with field value.
   */
  static inline std::string getStringField(const Protobuf::Message& message,
                                           const std::string& field_name) {
    const Protobuf::Descriptor* descriptor = message.GetDescriptor();
    const Protobuf::FieldDescriptor* name_field = descriptor->FindFieldByName(field_name);
    const Protobuf::Reflection* reflection = message.GetReflection();
    return reflection->GetString(message, name_field);
  }

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
  static void jsonConvertValue(const Protobuf::Message& source, ProtobufWkt::Value& dest);

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
   * Extract JSON as string from a google.protobuf.Message. Returns an error if the message cannot
   * be represented as JSON, which can occur if it contains an Any proto with an unrecognized type
   * URL or invalid data, or if memory cannot be allocated.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @param pretty_print whether the returned JSON should be formatted.
   * @param always_print_primitive_fields whether to include primitive fields set to their default
   * values, e.g. an int32 set to 0 or a bool set to false.
   * @return ProtobufUtil::StatusOr<std::string> of formatted JSON object, or an error status if
   * conversion fails.
   */
  static ProtobufUtil::StatusOr<std::string>
  getJsonStringFromMessage(const Protobuf::Message& message, bool pretty_print = false,
                           bool always_print_primitive_fields = false);

  /**
   * Extract JSON as string from a google.protobuf.Message, crashing if the conversion to JSON
   * fails. This method is safe so long as the message does not contain an Any proto with an
   * unrecognized type or invalid data.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @param pretty_print whether the returned JSON should be formatted.
   * @param always_print_primitive_fields whether to include primitive fields set to their default
   * values, e.g. an int32 set to 0 or a bool set to false.
   * @return std::string of formatted JSON object.
   */
  static std::string getJsonStringFromMessageOrDie(const Protobuf::Message& message,
                                                   bool pretty_print = false,
                                                   bool always_print_primitive_fields = false) {
    auto json_or_error =
        getJsonStringFromMessage(message, pretty_print, always_print_primitive_fields);
    RELEASE_ASSERT(json_or_error.ok(), json_or_error.status().ToString());
    return std::move(json_or_error).value();
  }

  /**
   * Extract JSON as string from a google.protobuf.Message, returning some error string if the
   * conversion to JSON fails.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @param pretty_print whether the returned JSON should be formatted.
   * @param always_print_primitive_fields whether to include primitive fields set to their default
   * values, e.g. an int32 set to 0 or a bool set to false.
   * @return std::string of formatted JSON object, or an error message if conversion fails.
   */
  static std::string getJsonStringFromMessageOrError(const Protobuf::Message& message,
                                                     bool pretty_print = false,
                                                     bool always_print_primitive_fields = false);

  /**
   * Utility method to create a Struct containing the passed in key/value strings.
   *
   * @param key the key to use to set the value
   * @param value the string value to associate with the key
   */
  static ProtobufWkt::Struct keyValueStruct(const std::string& key, const std::string& value);

  /**
   * Utility method to create a Struct containing the passed in key/value map.
   *
   * @param fields the key/value pairs to initialize the Struct proto
   */
  static ProtobufWkt::Struct keyValueStruct(const std::map<std::string, std::string>& fields);

  /**
   * Utility method to print a human readable string of the code passed in.
   *
   * @param code the protobuf error code
   */
  static std::string CodeEnumToString(ProtobufUtil::error::Code code);

  /**
   * Modifies a message such that all sensitive data (that is, fields annotated as
   * `udpa.annotations.sensitive`) is redacted for display. String-typed fields annotated as
   * `sensitive` will be replaced with the string "[redacted]", bytes-typed fields will be replaced
   * with the bytes `5B72656461637465645D` (the ASCII / UTF-8 encoding of the string "[redacted]"),
   * primitive-typed fields (including enums) will be cleared, and message-typed fields will be
   * traversed recursively to redact their contents.
   *
   * LIMITATION: This works properly for strongly-typed messages, as well as for messages packed in
   * a `ProtobufWkt::Any` with a `type_url` corresponding to a proto that was compiled into the
   * Envoy binary. However it does not work for messages encoded as `ProtobufWkt::Struct`, since
   * structs are missing the "sensitive" annotations that this function expects. Similarly, it fails
   * for messages encoded as `ProtobufWkt::Any` with a `type_url` that isn't registered with the
   * binary. If you're working with struct-typed messages, including those that might be hiding
   * within strongly-typed messages, please reify them to strongly-typed messages using
   * `MessageUtil::jsonConvert()` before calling `MessageUtil::redact()`.
   *
   * @param message message to redact.
   */
  static void redact(Protobuf::Message& message);
};

class ValueUtil {
public:
  static std::size_t hash(const ProtobufWkt::Value& value) { return MessageUtil::hash(value); }

  /**
   * Load YAML string into ProtobufWkt::Value.
   */
  static ProtobufWkt::Value loadFromYaml(const std::string& yaml);

  /**
   * Compare two ProtobufWkt::Values for equality.
   * @param v1 message of type type.googleapis.com/google.protobuf.Value
   * @param v2 message of type type.googleapis.com/google.protobuf.Value
   * @return true if v1 and v2 are identical
   */
  static bool equal(const ProtobufWkt::Value& v1, const ProtobufWkt::Value& v2);

  /**
   * @return wrapped ProtobufWkt::NULL_VALUE.
   */
  static const ProtobufWkt::Value& nullValue();

  /**
   * Wrap std::string into ProtobufWkt::Value string value.
   * @param str string to be wrapped.
   * @return wrapped string.
   */
  static ProtobufWkt::Value stringValue(const std::string& str);

  /**
   * Wrap optional std::string into ProtobufWkt::Value string value.
   * If the argument contains a null optional, return ProtobufWkt::NULL_VALUE.
   * @param str string to be wrapped.
   * @return wrapped string.
   */
  static ProtobufWkt::Value optionalStringValue(const absl::optional<std::string>& str);

  /**
   * Wrap boolean into ProtobufWkt::Value boolean value.
   * @param str boolean to be wrapped.
   * @return wrapped boolean.
   */
  static ProtobufWkt::Value boolValue(bool b);

  /**
   * Wrap ProtobufWkt::Struct into ProtobufWkt::Value struct value.
   * @param obj struct to be wrapped.
   * @return wrapped struct.
   */
  static ProtobufWkt::Value structValue(const ProtobufWkt::Struct& obj);

  /**
   * Wrap number into ProtobufWkt::Value double value.
   * @param num number to be wrapped.
   * @return wrapped number.
   */
  template <typename T> static ProtobufWkt::Value numberValue(const T num) {
    ProtobufWkt::Value val;
    val.set_number_value(static_cast<double>(num));
    return val;
  }

  /**
   * Wrap a collection of ProtobufWkt::Values into ProtobufWkt::Value list value.
   * @param values collection of ProtobufWkt::Values to be wrapped.
   * @return wrapped list value.
   */
  static ProtobufWkt::Value listValue(const std::vector<ProtobufWkt::Value>& values);
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
