#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace ProtobufMessage {

/**
 * Exception class for reporting validation errors due to the presence of unknown
 * fields in a protobuf.
 */
class UnknownProtoFieldException : public EnvoyException {
public:
  UnknownProtoFieldException(const std::string& message) : EnvoyException(message) {}
};

/**
 * Exception class for reporting validation errors due to the presence of deprecated
 * fields in a protobuf.
 */
class DeprecatedProtoFieldException : public EnvoyException {
public:
  DeprecatedProtoFieldException(const std::string& message) : EnvoyException(message) {}
};

/**
 * Visitor interface for a Protobuf::Message. The methods of ValidationVisitor are invoked to
 * perform validation based on events encountered during or after the parsing of proto binary
 * or JSON/YAML.
 */
class ValidationVisitor {
public:
  virtual ~ValidationVisitor() = default;

  /**
   * Invoked when an unknown field is encountered.
   * @param description human readable description of the field.
   */
  virtual void onUnknownField(absl::string_view description) PURE;

  /**
   * If true, skip this validation visitor in the interest of speed when
   * possible.
   **/
  virtual bool skipValidation() PURE;

  /**
   * Invoked when deprecated field is encountered.
   * @param description human readable description of the field.
   * @param soft_deprecation is set to true, visitor would log a warning message, otherwise would
   * throw an exception.
   */
  virtual void onDeprecatedField(absl::string_view description, bool soft_deprecation) PURE;
};

class ValidationContext {
public:
  virtual ~ValidationContext() = default;

  /**
   * @return ValidationVisitor& the validation visitor for static configuration.
   */
  virtual ValidationVisitor& staticValidationVisitor() PURE;

  /**
   * @return ValidationVisitor& the validation visitor for dynamic configuration.
   */
  virtual ValidationVisitor& dynamicValidationVisitor() PURE;
};

} // namespace ProtobufMessage
} // namespace Envoy
