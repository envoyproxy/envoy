#include "common/protobuf/message_validator_impl.h"

#include "envoy/common/exception.h"

#include "common/common/logger.h"
#include "common/common/macros.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace ProtobufMessage {

void StrictValidationVisitorImpl::onUnknownField(absl::string_view description) {
  throw EnvoyException(absl::StrCat("Protobuf message (", description, ") has unknown fields"));
}

ValidationVisitor& getNullValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(NullValidationVisitorImpl);
}

ValidationVisitor& getStrictValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(StrictValidationVisitorImpl);
}

} // namespace ProtobufMessage
} // namespace Envoy
