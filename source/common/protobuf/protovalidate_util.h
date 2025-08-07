#pragma once

#include <string>

#include "absl/status/status.h"

namespace google {
namespace protobuf {
class Message;
} // namespace protobuf
} // namespace google

namespace Envoy {
namespace ProtobufMessage {

/**
 * Utility for performing protovalidate-based validation on protobuf messages.
 *
 * This class provides static methods for validating Protocol Buffer messages using
 * the protovalidate library, which uses CEL (Common Expression Language) for
 * constraint validation. It serves as a modern replacement for protoc-gen-validate (PGV).
 *
 * The utility handles factory creation, validation execution, and error formatting
 * in a thread-safe manner suitable for Envoy's multi-threaded environment.
 */
class ProtovalidateUtil {
public:
  /**
   * Validate a protobuf message using protovalidate constraints.
   *
   * This method creates a validator factory and validator for the given message,
   * then executes all validation rules defined in the message's .proto file
   * using buf.validate field options. If validation fails, detailed error
   * information including field paths and constraint violations is returned.
   *
   * @param message the protobuf message to validate against its constraints.
   * @return absl::OkStatus() if validation succeeds, or an error status containing
   *         detailed violation information if validation fails.
   */
  static absl::Status validate(const google::protobuf::Message& message);

  /**
   * Check if protovalidate support is available in this build.
   *
   * @return true if protovalidate library is compiled in and available,
   *         false if only stub implementations are available.
   */
  static bool isAvailable();

  /**
   * Format a validation error into a standardized human-readable string.
   *
   * This utility method helps create consistent error messages across Envoy
   * when protovalidate validation fails. It includes the message type name
   * for better debugging context.
   *
   * @param message the protobuf message that failed validation (for type context).
   * @param error_details the detailed error description from validation.
   * @return a formatted error string suitable for logging or exception messages.
   */
  static std::string formatValidationError(const google::protobuf::Message& message,
                                           const std::string& error_details);
};

} // namespace ProtobufMessage
} // namespace Envoy
