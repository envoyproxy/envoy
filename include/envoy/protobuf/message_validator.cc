#include "envoy/protobuf/message_validator.h"

#include "envoy/common/exception.h"

#include "common/common/logger.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace ProtobufMessage {

void ValidationVisitor::onDeprecatedFieldDefault(absl::string_view description,
                                                 bool soft_deprecation) {
  if (soft_deprecation) {
    ENVOY_LOG_MISC(warn, "Unexpected field: {}",
              absl::StrCat(description, ValidationError::deprecation_error));
  } else {
    throw ValidationError::DeprecatedProtoFieldException(
        absl::StrCat(description, ValidationError::deprecation_error));
  }
}

} // namespace ProtobufMessage
} // namespace Envoy
