#pragma once

#include "envoy/common/exception.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {

class MissingFieldException : public EnvoyException {
public:
  MissingFieldException(const std::string& field_name, const Protobuf::Message& message);
};

class MessageUtil {
public:
  /**
   * Obtain a pointer to a mutable sub-message by field name.
   * @param message message reference.
   * @param field_name field name.
   * @return Protobuf::Message* mutable sub-message pointer if field name is valid, otherwise
   *                            nullptr.
   */
  static Protobuf::Message* getMutableMessage(Protobuf::Message& message,
                                              const std::string& field_name);

  /**
   * Obtain a const reference to a sub-message by field name.
   * @param message message reference.
   * @param field_name field name.
   * @return const Protobuf::Message* sub-message reference if field name is valid, otherwise
   *                                  nullptr.
   */
  static const Protobuf::Message* getMessage(const Protobuf::Message& message,
                                             const std::string& field_name);

  /**
   * Obtain a wrapped uint32_t by field name.
   * @param message message reference.
   * @param field_name field name.
   * @param default_value value to return if field is not set.
   * @return uint32_t field's value in message if set, otherwise default_value.
   */
  static uint32_t getUInt32(const Protobuf::Message& message, const std::string& field_name,
                            uint32_t default_value);

  /**
   * Obtain a wrapped uint32_t by field name.
   * @param message message reference.
   * @param field_name field name.
   * @return uint32_t field's value in message if set.
   * @throw MissingFieldException if field is not set.
   */
  static uint32_t getUInt32(const Protobuf::Message& message, const std::string& field_name);

  /**
   * Obtain a Duration's millisecond value by field name.
   * @param message message reference.
   * @param field_name field name.
   * @param default_value value to return if field is not set.
   * @return uint64_t field's millisecond value in message if set, otherwise default_value.
   */
  static uint64_t getMilliseconds(const Protobuf::Message& message, const std::string& field_name,
                                  uint64_t default_ms);

  /**
   * Obtain a Duration's millisecond value by field name.
   * @param message message reference.
   * @param field_name field name.
   * @return uint64_t field's millisecond value in message if set.
   * @throw MissingFieldException if field is not set.
   */
  static uint64_t getMilliseconds(const Protobuf::Message& message, const std::string& field_name);
};

} // namespace Envoy
