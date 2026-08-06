#include "source/extensions/retry/priority/header_order/config.h"

#include "envoy/extensions/retry/priority/header_order/v3/header_order.pb.h"
#include "envoy/extensions/retry/priority/header_order/v3/header_order.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

Upstream::RetryPrioritySharedPtr HeaderOrderRetryPriorityFactory::createRetryPriority(
    const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor,
    uint32_t max_retries) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::retry::priority::header_order::v3::HeaderOrderConfig&>(
      config, validation_visitor);
  return std::make_shared<HeaderOrderRetryPriority>(typed_config.metadata_namespace(),
                                                     typed_config.metadata_key(), max_retries);
}

REGISTER_FACTORY(HeaderOrderRetryPriorityFactory, Upstream::RetryPriorityFactory);

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
