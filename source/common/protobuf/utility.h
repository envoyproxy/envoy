#pragma once

#include "envoy/common/exception.h"

#include "common/common/utility.h"
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
    return StringUtil::join(ProtobufTypes::stringVector(source), delimiter);
  }
};

class MessageUtil {
public:
  static std::size_t hash(const Protobuf::Message& message) {
    return std::hash<std::string>{}(message.SerializeAsString());
  }

  static void loadFromFile(const std::string& path, Protobuf::Message& message);
};

} // namespace Envoy
