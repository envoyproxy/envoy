#pragma once

#include "envoy/extensions/retry/host/previous_hosts/v3/previous_hosts.pb.validate.h"
#include "envoy/upstream/retry.h"

#include "source/extensions/retry/host/previous_hosts/previous_hosts.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class PreviousHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&,
                                                            uint32_t) override {
    return std::make_shared<PreviousHostsRetryPredicate>();
  }

  std::string name() const override { return "envoy.retry_host_predicates.previous_hosts"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::retry::host::previous_hosts::v3::PreviousHostsPredicate>();
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
