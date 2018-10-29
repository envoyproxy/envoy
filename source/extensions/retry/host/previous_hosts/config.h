#pragma once

#include "envoy/upstream/retry.h"

#include "extensions/retry/host/previous_hosts/previous_hosts.h"
#include "extensions/retry/host/well_known_names.h"

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

  std::string name() override { return RetryHostPredicateValues::get().PreviousHostsPredicate; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{new Envoy::ProtobufWkt::Empty()};
  }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
