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
                           const Protobuf::Message&) override {
    callbacks.addHostPredicate(std::make_shared<OtherHostsRetryPredicate>());
  }

  std::string name() override { return RetryHostPredicateValues::get().PreviousHostsPredicate; }
};

} // namespace Host
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
