#include "common/protobuf/message_validator_impl.h"
#include "extensions/retry/host/omit_hosts/config.h"

#include "envoy/config/retry/omit_hosts/omit_hosts_config.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/upstream/retry.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

Upstream::RetryHostPredicateSharedPtr OmitHostsRetryPredicateFactory::createHostPredicate(
    const Protobuf::Message& config,
    uint32_t retry_count) {
  return std::make_shared<OmitHostsRetryPredicate>(
      MessageUtil::downcastAndValidate<const envoy::config::retry::omit_hosts::OmitHostsConfig&>(
          config, ProtobufMessage::getStrictValidationVisitor())
          .metadata_match(),
      retry_count);
}

REGISTER_FACTORY(OmitHostsRetryPredicateFactory, Upstream::RetryHostPredicateFactory);

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
