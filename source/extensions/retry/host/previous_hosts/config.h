#pragma once

#include "envoy/config/retry/previous_hosts/v2/previous_hosts.pb.validate.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/host/previous_hosts/previous_hosts.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class PreviousHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&,
                                                            uint32_t retry_count) override {
    return std::make_shared<PreviousHostsRetryPredicate>(retry_count);
  }

  std::string name() const override { return "envoy.retry_host_predicates.previous_hosts"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::retry::previous_hosts::v2::PreviousHostsPredicate>();
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
