#pragma once

#include "envoy/config/retry/omit_hosts/v2/omit_hosts_config.pb.validate.h"
#include "envoy/upstream/retry.h"

#include "extensions/retry/host/omit_hosts/omit_hosts.h"
#include "extensions/retry/host/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OmitHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  Upstream::RetryHostPredicateSharedPtr createHostPredicate(const Protobuf::Message&,
                                                            uint32_t) override;

  std::string name() override { return RetryHostPredicateValues::get().OmitHostsPredicate; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(new envoy::config::retry::omit_hosts::v2::OmitHostsConfig());
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
