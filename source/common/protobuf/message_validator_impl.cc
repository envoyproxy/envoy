#include "common/protobuf/message_validator_impl.h"

#include "envoy/common/exception.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/macros.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace ProtobufMessage {

void WarningValidationVisitorImpl::setUnknownCounter(Stats::Counter& counter) {
  ASSERT(unknown_counter_ == nullptr);
  unknown_counter_ = &counter;
  counter.add(prestats_unknown_count_);
}

void WarningValidationVisitorImpl::setDeprecatedCounter(Stats::Counter& counter) {
  ASSERT(deprecated_counter_ == nullptr);
  deprecated_counter_ = &counter;
  counter.add(prestats_deprecated_count_);
}

void WarningValidationVisitorImpl::onUnknownField(absl::string_view description) {
  onUnexpectedField(description, unknown_counter_, ValidationType::UnknownFields);
}

void WarningValidationVisitorImpl::onDeprecatedField(absl::string_view description) {
  std::string message = absl::StrCat(description, ValidationError::deprecation_error);
  onUnexpectedField(description, deprecated_counter_, ValidationType::DeprecatedFields);
}

void WarningValidationVisitorImpl::onUnexpectedField(absl::string_view description,
                                                     Stats::Counter* counter,
                                                     const ValidationType& validation_type) {
  const uint64_t hash = HashUtil::xxHash64(description);
  auto it = descriptions_.insert(hash);
  // If we've seen this before, skip.
  if (!it.second) {
    return;
  }
  // It's a new/deprecated field, log and bump stat.
  ENVOY_LOG(warn, "Unexpected field: {}", description);
  if (counter != nullptr) {
    counter->inc();
  } else {
    switch (validation_type) {
    case UnknownFields:
      ++prestats_unknown_count_;
      break;
    case DeprecatedFields:
      ++prestats_deprecated_count_;
      break;
    }
  }
}

void StrictValidationVisitorImpl::onUnknownField(absl::string_view description) {
  throw ValidationError::UnknownProtoFieldException(
      absl::StrCat("Protobuf message (", description, ") has unknown fields"));
}

void StrictValidationVisitorImpl::onDeprecatedField(absl::string_view description) {
  throw ValidationError::DeprecatedProtoFieldException(
      absl::StrCat(description, ValidationError::deprecation_error));
}

ValidationVisitor& getNullValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(NullValidationVisitorImpl);
}

ValidationVisitor& getStrictValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(StrictValidationVisitorImpl);
}

} // namespace ProtobufMessage
} // namespace Envoy
