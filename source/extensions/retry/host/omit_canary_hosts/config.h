#pragma once

#include "envoy/extensions/retry/host/omit_canary_hosts/v3/omit_canary_hosts.pb.validate.h"
#include "envoy/upstream/retry.h"

#include "source/extensions/retry/host/omit_canary_hosts/omit_canary_hosts.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitCanaryHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {

public:
  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&,
                                                            uint32_t) override {
    return std::make_shared<OmitCanaryHostsRetryPredicate>();
  }

  std::string name() const override { return "envoy.retry_host_predicates.omit_canary_hosts"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::retry::host::omit_canary_hosts::v3::OmitCanaryHostsPredicate>();
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
