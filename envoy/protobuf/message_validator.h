#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/optref.h"
#include "envoy/common/pure.h"
#include "envoy/runtime/runtime.h"

#include "source/common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace ProtobufMessage {

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
   * @return a status indicating if an unknown field was found.
   */
  virtual absl::Status onUnknownField(absl::string_view description) PURE;

  /**
   * If true, skip this validation visitor in the interest of speed when
   * possible.
   **/
  virtual bool skipValidation() PURE;

  /**
   * Invoked when deprecated field is encountered.
   * @param description human readable description of the field.
   * @param soft_deprecation is set to true, visitor would log a warning message, otherwise would
   * return an error
   */
  virtual absl::Status onDeprecatedField(absl::string_view description, bool soft_deprecation) PURE;

  /**
   * Called when a message or field is marked as work in progress or a message is contained in a
   * proto file marked as work in progress.
   */
  virtual void onWorkInProgress(absl::string_view description) PURE;

  /**
   * Called to update runtime stats on deprecated fields.
   */
  virtual OptRef<Runtime::Loader> runtime() PURE;
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
