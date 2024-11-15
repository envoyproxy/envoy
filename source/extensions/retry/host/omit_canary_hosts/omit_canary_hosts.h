#pragma once

#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
class OmitCanaryHostsRetryPredicate : public Upstream::RetryHostPredicate {
public:
  bool shouldSelectAnotherHost(const Upstream::Host& candidate_host) override {
    return candidate_host.canary();
  }

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr) override {}
};
} // namespace Envoy
