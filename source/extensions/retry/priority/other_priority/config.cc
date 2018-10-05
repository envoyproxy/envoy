#include "extensions/retry/priority/other_priority/config.h"

#include "envoy/config/retry/other_priority/other_priority_config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

void OtherPriorityRetryPriorityFactory::createRetryPriority(
    Upstream::RetryPriorityFactoryCallbacks& callbacks, const Protobuf::Message& config,
    uint32_t max_retries) {
  callbacks.addRetryPriority(std::make_shared<OtherPriorityRetryPriority>(
      MessageUtil::downcastAndValidate<
          const envoy::config::retry::other_priority::OtherPriorityConfig&>(config)
          .update_frequency(),
      max_retries));
}

static Registry::RegisterFactory<OtherPriorityRetryPriorityFactory, Upstream::RetryPriorityFactory>
    register_;

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
