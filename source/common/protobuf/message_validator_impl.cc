#include "source/common/protobuf/message_validator_impl.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"
#include "source/common/common/macros.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace ProtobufMessage {

namespace {
const char deprecation_error[] = " If continued use of this field is absolutely necessary, "
                                 "see " ENVOY_DOC_URL_RUNTIME_OVERRIDE_DEPRECATED " for "
                                 "how to apply a temporary and highly discouraged override.";

void onDeprecatedFieldCommon(absl::string_view description, bool soft_deprecation) {
  if (soft_deprecation) {
    ENVOY_LOG_MISC(warn, "Deprecated field: {}", absl::StrCat(description, deprecation_error));
  } else {
    throw DeprecatedProtoFieldException(absl::StrCat(description, deprecation_error));
  }
}

void onWorkInProgressCommon(absl::string_view description) {
  ENVOY_LOG_MISC(warn, "{}", description);
}
} // namespace

void WarningValidationVisitorImpl::onUnknownField(absl::string_view description) {
  const uint64_t hash = HashUtil::xxHash64(description);
  auto it = descriptions_.insert(hash);
  // If we've seen this before, skip.
  if (!it.second) {
    return;
  }

  // It's a new field, log and bump stat.
  ENVOY_LOG(warn, "Unknown field: {}", description);
  unknown_counter_.inc();
}

void WarningValidationVisitorImpl::onDeprecatedField(absl::string_view description,
                                                     bool soft_deprecation) {
  onDeprecatedFieldCommon(description, soft_deprecation);
}

void WarningValidationVisitorImpl::onWorkInProgress(absl::string_view description) {
  onWorkInProgressCommon(description);
}

void StrictValidationVisitorImpl::onUnknownField(absl::string_view description) {
  throw UnknownProtoFieldException(
      absl::StrCat("Protobuf message (", description, ") has unknown fields"));
}

void StrictValidationVisitorImpl::onDeprecatedField(absl::string_view description,
                                                    bool soft_deprecation) {
  onDeprecatedFieldCommon(description, soft_deprecation);
}

void StrictValidationVisitorImpl::onWorkInProgress(absl::string_view description) {
  onWorkInProgressCommon(description);
}

ValidationVisitor& getNullValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(NullValidationVisitorImpl);
}

ValidationVisitor& getStrictValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(StrictValidationVisitorImpl);
}

} // namespace ProtobufMessage
} // namespace Envoy
