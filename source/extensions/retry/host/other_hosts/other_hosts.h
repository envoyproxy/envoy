#pragma once

#include "envoy/upstream/retry.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
class OtherHostsRetryPredicate : public Upstream::RetryHostPredicate {
public:
  OtherHostsRetryPredicate(uint32_t retry_count) : attempted_hosts_(retry_count) {}

  bool shouldSelectAnotherHost(const Upstream::Host& candidate_host) override {
    return std::find(attempted_hosts_.begin(), attempted_hosts_.end(), &candidate_host) !=
           attempted_hosts_.end();
  }
  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr attempted_host) override {
    attempted_hosts_.emplace_back(attempted_host.get());
  }

private:
  std::vector<Upstream::HostDescription const*> attempted_hosts_;
};
} // namespace Envoy
