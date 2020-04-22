#include "extensions/retry/priority/previous_priorities/config.h"

#include "envoy/config/retry/previous_priorities/previous_priorities_config.pb.h"
#include "envoy/config/retry/previous_priorities/previous_priorities_config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

Upstream::RetryPrioritySharedPtr PreviousPrioritiesRetryPriorityFactory::createRetryPriority(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor,

    uint32_t max_retries) {
  return std::make_shared<PreviousPrioritiesRetryPriority>(
      MessageUtil::downcastAndValidate<
          const envoy::config::retry::previous_priorities::PreviousPrioritiesConfig&>(
          config, validation_visitor)
          .update_frequency(),
      max_retries);
}

REGISTER_FACTORY(PreviousPrioritiesRetryPriorityFactory, Upstream::RetryPriorityFactory);

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
