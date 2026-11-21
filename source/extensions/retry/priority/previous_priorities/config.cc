#include "source/extensions/retry/priority/previous_priorities/config.h"

#include <memory>

#include "envoy/extensions/retry/priority/previous_priorities/v3/previous_priorities_config.pb.h"
#include "envoy/extensions/retry/priority/previous_priorities/v3/previous_priorities_config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "source/common/common/thread.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

absl::StatusOr<Upstream::RetryPriorityFactoryCb>
PreviousPrioritiesRetryPriorityFactory::createRetryPriorityFactory(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) {
  if (!Runtime::runtimeFeatureEnabled(
          "envoy.reloadable_features.validate_retry_priority_config_at_load")) {
    auto config_copy = std::shared_ptr<Protobuf::Message>(config.New());
    config_copy->CheckTypeAndMergeFrom(config);
    return [config_copy =
                std::move(config_copy)](uint32_t max_retries) -> Upstream::RetryPrioritySharedPtr {
      return std::make_shared<PreviousPrioritiesRetryPriority>(
          MessageUtil::downcastAndValidate<const envoy::extensions::retry::priority::
                                               previous_priorities::v3::PreviousPrioritiesConfig&>(
              *config_copy, ProtobufMessage::getNullValidationVisitor())
              .update_frequency(),
          max_retries);
    };
  }

  const auto* typed_config = Envoy::Protobuf::DynamicCastMessage<
      const envoy::extensions::retry::priority::previous_priorities::v3::PreviousPrioritiesConfig>(
      &config);
  if (typed_config == nullptr) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid config type '", config.GetTypeName(),
                                                   "' for PreviousPrioritiesRetryPriorityFactory"));
  }
  TRY_NEEDS_AUDIT { MessageUtil::validate(*typed_config, validation_visitor); }
  END_TRY CATCH(const EnvoyException& e, { return absl::InvalidArgumentError(e.what()); });
  const uint32_t update_frequency = typed_config->update_frequency();
  return [update_frequency](uint32_t max_retries) -> Upstream::RetryPrioritySharedPtr {
    return std::make_shared<PreviousPrioritiesRetryPriority>(update_frequency, max_retries);
  };
}

REGISTER_FACTORY(PreviousPrioritiesRetryPriorityFactory, Upstream::RetryPriorityFactory);

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
