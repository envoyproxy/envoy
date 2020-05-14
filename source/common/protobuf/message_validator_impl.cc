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

void WarningValidationVisitorImpl::onUnknownField(absl::string_view description) {
  const uint64_t hash = HashUtil::xxHash64(description);
  auto it = descriptions_.insert(hash);
  // If we've seen this before, skip.
  if (!it.second) {
    return;
  }
  // It's a new field, log and bump stat.
  ENVOY_LOG(warn, "Unexpected field: {}", description);
  if (unknown_counter_ == nullptr) {
    ++prestats_unknown_count_;
  } else {
    unknown_counter_->inc();
  }
}

void WarningValidationVisitorImpl::onDeprecatedField(absl::string_view description,
                                                     bool soft_deprecation) {
  onDeprecatedFieldDefault(description, soft_deprecation);
}

void StrictValidationVisitorImpl::onUnknownField(absl::string_view description) {
  throw ValidationError::UnknownProtoFieldException(
      absl::StrCat("Protobuf message (", description, ") has unknown fields"));
}

void StrictValidationVisitorImpl::onDeprecatedField(absl::string_view description,
                                                    bool soft_deprecation) {
  onDeprecatedFieldDefault(description, soft_deprecation);
}

ValidationVisitor& getNullValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(NullValidationVisitorImpl);
}

ValidationVisitor& getStrictValidationVisitor() {
  MUTABLE_CONSTRUCT_ON_FIRST_USE(StrictValidationVisitorImpl);
}

} // namespace ProtobufMessage
} // namespace Envoy
