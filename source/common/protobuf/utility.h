#pragma once

#include <numeric>
#include <string>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/runtime/runtime.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/common/hash.h"
#include "source/common/common/stl_helpers.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

// Obtain the value of a wrapped field (e.g. google.protobuf.UInt32Value) if set. Otherwise, return
// the default value.
#define PROTOBUF_GET_WRAPPED_OR_DEFAULT(message, field_name, default_value)                        \
  ((message).has_##field_name() ? (message).field_name().value() : (default_value))

// Obtain the value of a wrapped field (e.g. google.protobuf.UInt32Value) if set. Otherwise, return
// absl::nullopt.
#define PROTOBUF_GET_OPTIONAL_WRAPPED(message, field_name)                                         \
  ((message).has_##field_name() ? absl::make_optional((message).field_name().value())              \
                                : absl::nullopt)

// Obtain the value of a wrapped field (e.g. google.protobuf.UInt32Value) if set. Otherwise, throw
// a EnvoyException.

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

// Obtain the milliseconds value of a google.protobuf.Duration field if set. Otherwise, throw an
// EnvoyException.
#define PROTOBUF_GET_MS_REQUIRED(message, field_name)                                              \
  ([](const auto& msg) {                                                                           \
    if (!msg.has_##field_name()) {                                                                 \
      ::Envoy::ProtoExceptionUtil::throwMissingFieldException(#field_name, msg);                   \
    }                                                                                              \
    return DurationUtil::durationToMilliseconds(msg.field_name());                                 \
  }((message)))

// Obtain the seconds value of a google.protobuf.Duration field if set. Otherwise, return the
// default value.
#define PROTOBUF_GET_SECONDS_OR_DEFAULT(message, field_name, default_value)                        \
  ((message).has_##field_name() ? DurationUtil::durationToSeconds((message).field_name())          \
                                : (default_value))

// Obtain the seconds value of a google.protobuf.Duration field if set. Otherwise, throw an
// EnvoyException.
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
// Issue: https://github.com/bufbuild/protoc-gen-validate/issues/85
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

class TypeUtil {
public:
  static absl::string_view typeUrlToDescriptorFullName(absl::string_view type_url);

  static std::string descriptorFullNameToTypeUrl(absl::string_view type);
};

class RepeatedPtrUtil {
public:
  static std::string join(const Protobuf::RepeatedPtrField<std::string>& source,
                          const std::string& delimiter) {
    return absl::StrJoin(std::vector<std::string>(source.begin(), source.end()), delimiter);
  }

  template <class ProtoType>
  static std::string debugString(const Protobuf::RepeatedPtrField<ProtoType>& source) {
    return accumulateToString<ProtoType>(
        source, [](const Protobuf::Message& message) { return message.DebugString(); });
  }

  // Based on MessageUtil::hash() defined below.
  template <class ProtoType>
  static std::size_t hash(const Protobuf::RepeatedPtrField<ProtoType>& source) {
    std::string text;
#if defined(ENVOY_ENABLE_FULL_PROTOS)
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
#else
    for (const auto& message : source) {
      absl::StrAppend(&text, message.SerializeAsString());
    }
#endif
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
                     clone->CheckTypeAndMergeFrom(proto_message);
                     return std::unique_ptr<const Protobuf::Message>(clone);
                   });
    return ret_container;
  }
};

using ProtoValidationException = EnvoyException;

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
    return Protobuf::util::MessageDifferencer::Equals(lhs, rhs);
  }

  class FileExtensionValues {
  public:
    const std::string ProtoBinary = ".pb";
    const std::string ProtoBinaryLengthDelimited = ".pb_length_delimited";
    const std::string ProtoText = ".pb_text";
    const std::string Json = ".json";
    const std::string Yaml = ".yaml";
    const std::string Yml = ".yml";
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

#ifdef ENVOY_ENABLE_YAML
  static void loadFromJson(absl::string_view json, Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor);
  /**
   * Return ok only when strict conversion(don't ignore unknown field) succeeds.
   * Return error status for strict conversion and set has_unknown_field to true if relaxed
   * conversion(ignore unknown field) succeeds.
   * Return error status for relaxed conversion and set has_unknown_field to false if relaxed
   * conversion(ignore unknown field) fails.
   */
  static absl::Status loadFromJsonNoThrow(absl::string_view json, Protobuf::Message& message,
                                          bool& has_unknown_field);
  static absl::Status loadFromJsonNoThrow(absl::string_view json, Protobuf::Struct& message);
  static void loadFromJson(absl::string_view json, Protobuf::Struct& message);
  static void loadFromYaml(const std::string& yaml, Protobuf::Message& message,
                           ProtobufMessage::ValidationVisitor& validation_visitor);
#endif

  // This function attempts to load Envoy configuration from the specified file
  // based on the file type.
  // It handles .pb .pb_text .json .yaml and .yml files which are well
  // structured based on the file type.
  // It has somewhat inconsistent handling of invalid file contents,
  // occasionally failing over to try another type of parsing, or silently
  // failing instead of throwing an exception.
  static absl::Status loadFromFile(const std::string& path, Protobuf::Message& message,
                                   ProtobufMessage::ValidationVisitor& validation_visitor,
                                   Api::Api& api);

  /**
   * Checks for use of deprecated fields in message and all sub-messages.
   * @param message message to validate.
   * @param validation_visitor the validation visitor to use.
   * @param recurse_into_any whether to recurse into Any messages during unexpected checking.
   * @throw EnvoyException if deprecated fields are used and listed
   *    in disallowed_features in runtime_features.h
   */
  static void checkForUnexpectedFields(const Protobuf::Message& message,
                                       ProtobufMessage::ValidationVisitor& validation_visitor,
                                       bool recurse_into_any = false);

  /**
   * Validates that duration fields in the config are valid.
   * @param message message to validate.
   * @param recurse_into_any whether to recurse into Any messages during unexpected checking.
   * @throw EnvoyException if a duration field is invalid.
   */
  static void validateDurationFields(const Protobuf::Message& message,
                                     bool recurse_into_any = false);

  /**
   * Perform a PGV check on the entire message tree, recursing into Any messages as needed.
   */
  static void recursivePgvCheck(const Protobuf::Message& message);

  /**
   * Validate protoc-gen-validate constraints on a given protobuf as well as performing
   * unexpected field validation.
   * Note the corresponding `.pb.validate.h` for the message has to be included in the source file
   * of caller.
   * @param message message to validate.
   * @param validation_visitor the validation visitor to use.
   * @param recurse_into_any whether to recurse into Any messages during unexpected checking.
   * @throw EnvoyException if the message does not satisfy its type constraints.
   */
  template <class MessageType>
  static void validate(const MessageType& message,
                       ProtobufMessage::ValidationVisitor& validation_visitor,
                       bool recurse_into_any = false) {
    // TODO(adisuissa): There are multiple recursive traversals done by the
    // calls in this function. This can be refactored into a single recursive
    // traversal that invokes the various validators.

    // Log warnings or throw errors if deprecated fields or unknown fields are in use.
    if (!validation_visitor.skipValidation()) {
      checkForUnexpectedFields(message, validation_visitor, recurse_into_any);
    }

    // Throw an exception if the config has an invalid Duration field. This is needed
    // because Envoy validates the duration in a strict way that is not supported by PGV.
    validateDurationFields(message, recurse_into_any);

    // TODO(mattklein123): This will recurse the message twice, once above and once for PGV. When
    // we move to always recursing, satisfying the TODO below, we should merge into a single
    // recursion for performance reasons.
    if (recurse_into_any) {
      return recursivePgvCheck(message);
    }

    // TODO(mattklein123): Now that PGV is capable of doing recursive message checks on abstract
    // types, we can remove bottom up validation from the entire codebase and only validate
    // at top level ingestion (bootstrap, discovery response). This is a large change and will be
    // done as a separate PR. This change will also allow removing templating from most/all of
    // related functions.
    std::string err;
    if (!Validate(message, &err)) {
      ProtoExceptionUtil::throwProtoValidationException(err, message);
    }
  }

#ifdef ENVOY_ENABLE_YAML
  template <class MessageType>
  static void loadFromYamlAndValidate(const std::string& yaml, MessageType& message,
                                      ProtobufMessage::ValidationVisitor& validation_visitor) {
    loadFromYaml(yaml, message, validation_visitor);
    validate(message, validation_visitor);
  }
#endif

  /**
   * Downcast and validate protoc-gen-validate constraints on a given protobuf.
   * Note the corresponding `.pb.validate.h` for the message has to be included in the source file
   * of caller.
   * @param message const Protobuf::Message& to downcast and validate.
   * @return const MessageType& the concrete message type downcasted to on success.
   * @throw EnvoyException if the message does not satisfy its type constraints.
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
   * Utility method to swap between protobuf bytes type using absl::Cord instead of std::string.
   * Noop for now.
   */
  static const std::string& bytesToString(const std::string& bytes) { return bytes; }

  /**
   * Convert from a typed message into a google.protobuf.Any. This should be used
   * instead of the inbuilt PackTo, as PackTo is not available with lite protos.
   *
   * @param any_message destination google.protobuf.Any.
   * @param message source to pack from.
   *
   * @throw EnvoyException if the message does not unpack.
   */
  static void packFrom(Protobuf::Any& any_message, const Protobuf::Message& message);

  /**
   * Convert from google.protobuf.Any to a typed message. This should be used
   * instead of the inbuilt UnpackTo as it performs validation of results.
   *
   * @param any_message source google.protobuf.Any message.
   * @param message destination to unpack to.
   *
   * @return absl::Status
   */
  static absl::Status unpackTo(const Protobuf::Any& any_message, Protobuf::Message& message);

  /**
   * Convert from google.protobuf.Any to bytes as std::string
   * @param any source google.protobuf.Any message.
   *
   * @return std::string consists of bytes in the input message or error status.
   */
  static absl::StatusOr<std::string> anyToBytes(const Protobuf::Any& any) {
    if (any.Is<Protobuf::StringValue>()) {
      Protobuf::StringValue s;
      RETURN_IF_NOT_OK(MessageUtil::unpackTo(any, s));
      return s.value();
    }
    if (any.Is<Protobuf::BytesValue>()) {
      Protobuf::BytesValue b;
      RETURN_IF_NOT_OK(MessageUtil::unpackTo(any, b));
      return bytesToString(b.value());
    }
    return bytesToString(any.value());
  };

  /**
   * Convert from google.protobuf.Any to a typed message.
   * @param message source google.protobuf.Any message.
   *
   * @return MessageType the typed message inside the Any.
   */
  template <class MessageType>
  static inline void anyConvert(const Protobuf::Any& message, MessageType& typed_message) {
    THROW_IF_NOT_OK(unpackTo(message, typed_message));
  };

  template <class MessageType> static inline MessageType anyConvert(const Protobuf::Any& message) {
    MessageType typed_message;
    anyConvert(message, typed_message);
    return typed_message;
  };

  /**
   * Convert and validate from google.protobuf.Any to a typed message.
   * @param message source google.protobuf.Any message.
   *
   * @return MessageType the typed message inside the Any.
   * @throw EnvoyException if the message does not satisfy its type constraints.
   */
  template <class MessageType>
  static inline void anyConvertAndValidate(const Protobuf::Any& message, MessageType& typed_message,
                                           ProtobufMessage::ValidationVisitor& validation_visitor) {
    anyConvert<MessageType>(message, typed_message);
    validate(typed_message, validation_visitor);
  };

  template <class MessageType>
  static inline MessageType
  anyConvertAndValidate(const Protobuf::Any& message,
                        ProtobufMessage::ValidationVisitor& validation_visitor) {
    MessageType typed_message;
    anyConvertAndValidate<MessageType>(message, typed_message, validation_visitor);
    return typed_message;
  };

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
    Protobuf::ReflectableMessage reflectable_message = createReflectableMessage(message);
    const Protobuf::Descriptor* descriptor = reflectable_message->GetDescriptor();
    const Protobuf::FieldDescriptor* name_field = descriptor->FindFieldByName(field_name);
    const Protobuf::Reflection* reflection = reflectable_message->GetReflection();
    return reflection->GetString(*reflectable_message, name_field);
  }

#ifdef ENVOY_ENABLE_YAML
  /**
   * Convert between two protobufs via a JSON round-trip. This is used to translate arbitrary
   * messages to/from google.protobuf.Struct.
   * TODO(htuch): Avoid round-tripping via JSON strings by doing whatever
   * Protobuf::util::MessageToJsonString does but generating a google.protobuf.Struct instead.
   * @param source message.
   * @param dest message.
   */
  static void jsonConvert(const Protobuf::Message& source, Protobuf::Message& dest);
  static void jsonConvert(const Protobuf::Message& source, Protobuf::Struct& dest);
  static void jsonConvert(const Protobuf::Struct& source,
                          ProtobufMessage::ValidationVisitor& validation_visitor,
                          Protobuf::Message& dest);
  // Convert a message to a Protobuf::Value, return false upon failure.
  static bool jsonConvertValue(const Protobuf::Message& source, Protobuf::Value& dest);

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
  static absl::StatusOr<std::string>
  getJsonStringFromMessage(const Protobuf::Message& message, bool pretty_print = false,
                           bool always_print_primitive_fields = false);

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
#endif

  static std::string convertToStringForLogs(const Protobuf::Message& message,
                                            bool pretty_print = false,
                                            bool always_print_primitive_fields = false);

  /**
   * Utility method to create a Struct containing the passed in key/value strings.
   *
   * @param key the key to use to set the value
   * @param value the string value to associate with the key
   */
  static Protobuf::Struct keyValueStruct(const std::string& key, const std::string& value);

  /**
   * Utility method to create a Struct containing the passed in key/value map.
   *
   * @param fields the key/value pairs to initialize the Struct proto
   */
  static Protobuf::Struct keyValueStruct(const std::map<std::string, std::string>& fields);

  /**
   * Utility method to print a human readable string of the code passed in.
   *
   * @param code the protobuf error code
   */
  static std::string codeEnumToString(absl::StatusCode code);

  /**
   * Modifies a message such that all sensitive data (that is, fields annotated as
   * `udpa.annotations.sensitive`) is redacted for display. String-typed fields annotated as
   * `sensitive` will be replaced with the string "[redacted]", bytes-typed fields will be replaced
   * with the bytes `5B72656461637465645D` (the ASCII / UTF-8 encoding of the string "[redacted]"),
   * primitive-typed fields (including enums) will be cleared, and message-typed fields will be
   * traversed recursively to redact their contents.
   *
   * LIMITATION: This works properly for strongly-typed messages, as well as for messages packed in
   * a `Protobuf::Any` with a `type_url` corresponding to a proto that was compiled into the
   * Envoy binary. However it does not work for messages encoded as `Protobuf::Struct`, since
   * structs are missing the "sensitive" annotations that this function expects. Similarly, it fails
   * for messages encoded as `Protobuf::Any` with a `type_url` that isn't registered with the
   * binary. If you're working with struct-typed messages, including those that might be hiding
   * within strongly-typed messages, please reify them to strongly-typed messages using
   * `MessageUtil::jsonConvert()` before calling `MessageUtil::redact()`.
   *
   * @param message message to redact.
   */
  static void redact(Protobuf::Message& message);

  /**
   * Sanitizes a string to contain only valid UTF-8. Invalid UTF-8 characters will be replaced. If
   * the input string is valid UTF-8, it will be returned unmodified.
   */
  static std::string sanitizeUtf8String(absl::string_view str);

  /**
   * Return text proto representation of the `message`.
   * @param message proto to print.
   * @return text representation of the proto `message`.
   */
  static std::string toTextProto(const Protobuf::Message& message);
};

class ValueUtil {
public:
  static std::size_t hash(const Protobuf::Value& value) { return MessageUtil::hash(value); }

#ifdef ENVOY_ENABLE_YAML
  /**
   * Load YAML string into Protobuf::Value.
   */
  static Protobuf::Value loadFromYaml(const std::string& yaml);
#endif

  /**
   * Compare two Protobuf::Values for equality.
   * @param v1 message of type type.googleapis.com/google.protobuf.Value
   * @param v2 message of type type.googleapis.com/google.protobuf.Value
   * @return true if v1 and v2 are identical
   */
  static bool equal(const Protobuf::Value& v1, const Protobuf::Value& v2);

  /**
   * @return wrapped Protobuf::NULL_VALUE.
   */
  static const Protobuf::Value& nullValue();

  /**
   * Wrap absl::string_view into Protobuf::Value string value.
   * @param str string to be wrapped.
   * @return wrapped string.
   */
  static Protobuf::Value stringValue(absl::string_view str);

  /**
   * Wrap optional std::string into Protobuf::Value string value.
   * If the argument contains a null optional, return Protobuf::NULL_VALUE.
   * @param str string to be wrapped.
   * @return wrapped string.
   */
  static Protobuf::Value optionalStringValue(const absl::optional<std::string>& str);

  /**
   * Wrap boolean into Protobuf::Value boolean value.
   * @param str boolean to be wrapped.
   * @return wrapped boolean.
   */
  static Protobuf::Value boolValue(bool b);

  /**
   * Wrap Protobuf::Struct into Protobuf::Value struct value.
   * @param obj struct to be wrapped.
   * @return wrapped struct.
   */
  static Protobuf::Value structValue(const Protobuf::Struct& obj);

  /**
   * Wrap number into Protobuf::Value double value.
   * @param num number to be wrapped.
   * @return wrapped number.
   */
  template <typename T> static Protobuf::Value numberValue(const T num) {
    Protobuf::Value val;
    val.set_number_value(static_cast<double>(num));
    return val;
  }

  /**
   * Wrap a collection of Protobuf::Values into Protobuf::Value list value.
   * @param values collection of Protobuf::Values to be wrapped.
   * @return wrapped list value.
   */
  static Protobuf::Value listValue(const std::vector<Protobuf::Value>& values);
};

/**
 * HashedValue is a wrapper around Protobuf::Value that computes
 * and stores a hash code for the Value at construction.
 */
class HashedValue {
public:
  HashedValue(const Protobuf::Value& value) : value_(value), hash_(ValueUtil::hash(value)) {};
  HashedValue(const HashedValue& v) = default;

  const Protobuf::Value& value() const { return value_; }
  std::size_t hash() const { return hash_; }

  bool operator==(const HashedValue& rhs) const {
    return hash_ == rhs.hash_ && ValueUtil::equal(value_, rhs.value_);
  }

  bool operator!=(const HashedValue& rhs) const { return !(*this == rhs); }

private:
  const Protobuf::Value value_;
  const std::size_t hash_;
};

class DurationUtil {
public:
  /**
   * Same as DurationUtil::durationToMilliseconds but with extra validation logic.
   * Same as Protobuf::util::TimeUtil::DurationToSeconds but with extra validation logic.
   * Specifically, we ensure that the duration is positive.
   * @param duration protobuf.
   * @return duration in milliseconds.
   * @throw EnvoyException when duration is out-of-range.
   */
  static uint64_t durationToMilliseconds(const Protobuf::Duration& duration);

  /**
   * Same as DurationUtil::durationToMilliseconds but does not throw an exception.
   * @param duration protobuf.
   * @return duration in milliseconds or an error status.
   */
  static absl::StatusOr<uint64_t> durationToMillisecondsNoThrow(const Protobuf::Duration& duration);

  /**
   * Same as Protobuf::util::TimeUtil::DurationToSeconds but with extra validation logic.
   * Specifically, we ensure that the duration is positive.
   * @param duration protobuf.
   * @return duration in seconds.
   * @throw EnvoyException when duration is out-of-range.
   */
  static uint64_t durationToSeconds(const Protobuf::Duration& duration);
};

class TimestampUtil {
public:
  /**
   * Writes a time_point<system_clock> (SystemTime) to a protobuf Timestamp, by way of time_t.
   * @param system_clock_time the time to write
   * @param timestamp a pointer to the mutable protobuf member to be written into.
   */
  static void systemClockToTimestamp(const SystemTime system_clock_time,
                                     Protobuf::Timestamp& timestamp);
};

class StructUtil {
public:
  /**
   * Recursively updates in-place a protobuf structure with keys from another
   * object.
   *
   * The merging strategy is the following. If a key from \p other does not
   * exists, it's just copied into \p obj. If the key exists but has a
   * different type, it is replaced by the new value. Otherwise:
   * - for scalar values (null, string, number, boolean) are replaced with the new value
   * - for lists: new values are added to the current list
   * - for structures: recursively apply this scheme
   *
   * @param obj the object to update in-place
   * @param with the object to update \p obj with
   */
  static void update(Protobuf::Struct& obj, const Protobuf::Struct& with);
};

} // namespace Envoy

// Specialize std::hash on Envoy::HashedValue.
template <> struct std::hash<Envoy::HashedValue> {
  std::size_t operator()(Envoy::HashedValue const& v) const { return v.hash(); }
};
