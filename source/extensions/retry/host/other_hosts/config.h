#pragma once

#include "envoy/upstream/retry.h"

#include "extensions/retry/host/other_hosts/other_hosts.h"
#include "extensions/retry/host/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Host {

class OtherHostsRetryPredicateFactory : public Upstream::RetryHostPredicateFactory {
public:
  void createHostPredicate(Upstream::RetryHostPredicateFactoryCallbacks& callbacks,
                           const Protobuf::Message&, uint32_t retry_count) override {
    callbacks.addHostPredicate(std::make_shared<OtherHostsRetryPredicate>(retry_count));
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
