#pragma once

#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
class OtherHostsRetryPredicate : public Upstream::RetryHostPredicate {
public:
  bool shouldSelectAnotherHost(const Upstream::Host& candidate_host) override {
    return attempted_hosts_.find(candidate_host.address()->asString()) != attempted_hosts_.end();
  }
  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr attempted_host) override {
    attempted_hosts_.insert(attempted_host->address()->asString());
  }

private:
  std::unordered_set<std::string> attempted_hosts_;
};
} // namespace Envoy
