#include "extensions/retry/host/omit_host_metadata/config.h"

#include "envoy/extensions/retry/omit_host_metadata/v3alpha/omit_host_metadata_config.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

#include "common/protobuf/message_validator_impl.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

Upstream::RetryHostPredicateSharedPtr
OmitHostsRetryPredicateFactory::createHostPredicate(const Protobuf::Message& config, uint32_t) {
  return std::make_shared<OmitHostsRetryPredicate>(
      MessageUtil::downcastAndValidate<
          const envoy::extensions::retry::omit_host_metadata::v3alpha::OmitHostMetadataConfig&>(
          config, ProtobufMessage::getStrictValidationVisitor())
          .metadata_match());
}

REGISTER_FACTORY(OmitHostsRetryPredicateFactory, Upstream::RetryHostPredicateFactory);

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
