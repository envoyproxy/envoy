#pragma once

#include "envoy/common/exception.h"

#include "common/protobuf/protobuf.h"

#define PROTOBUF_GET_WRAPPED(message, field_name, default_value)                                   \
  ((message).has_##field_name() ? (message).field_name().value() : (default_value))

#define PROTOBUF_GET_WRAPPED_REQUIRED(message, field_name)                                         \
  ((message).has_##field_name() ? (message).field_name().value()                                   \
                                : throw MissingFieldException(#field_name, (message)))

namespace Envoy {

class MissingFieldException : public EnvoyException {
public:
  MissingFieldException(const std::string& field_name, const Protobuf::Message& message);
};

} // namespace Envoy
