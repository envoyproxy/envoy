#pragma once

#include "envoy/common/exception.h"
#include "envoy/json/json_object.h"

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
  ((message).has_##field_name()                                                                    \
       ? Protobuf::util::TimeUtil::DurationToMilliseconds((message).field_name())                  \
       : (default_value))

// Obtain the milliseconds value of a google.protobuf.Duration field if set. Otherwise, throw a
// MissingFieldException.
#define PROTOBUF_GET_MS_REQUIRED(message, field_name)                                              \
  ((message).has_##field_name()                                                                    \
       ? Protobuf::util::TimeUtil::DurationToMilliseconds((message).field_name())                  \
       : throw MissingFieldException(#field_name, (message)))

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
};

class MessageUtil {
public:
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
  static void loadFromFile(const std::string& path, Protobuf::Message& message);

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
   * @return std::string of JSON object.
   */
  static std::string getJsonStringFromMessage(const Protobuf::Message& message);

  /**
   * Extract JSON object from a google.protobuf.Message.
   * @param message message of type type.googleapis.com/google.protobuf.Message.
   * @return Json::ObjectSharedPtr of JSON object or nullptr if unable to extract.
   */
  static Json::ObjectSharedPtr getJsonObjectFromMessage(const Protobuf::Message& message) {
    return Json::Factory::loadFromString(MessageUtil::getJsonStringFromMessage(message));
  }
};

} // namespace Envoy
