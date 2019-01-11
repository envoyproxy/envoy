#pragma once

#include "envoy/upstream/retry.h"

namespace Envoy {

/**
 * A simple host predicate that will remember the first host it sees and reject
 * all other hosts.
 */
class TestHostPredicate : public Upstream::RetryHostPredicate {
  bool shouldSelectAnotherHost(const Upstream::Host& candidate_host) override {
    return !first_host_address_ || *first_host_address_ != candidate_host.address()->asString();
  }

  void onHostAttempted(Upstream::HostDescriptionConstSharedPtr attempted_host) override {
    if (!first_host_address_) {
      first_host_address_ = attempted_host->address()->asString();
    }
  }

  absl::optional<std::string> first_host_address_;
};
} // namespace Envoy
