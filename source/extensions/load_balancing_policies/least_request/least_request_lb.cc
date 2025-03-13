#include "source/extensions/load_balancing_policies/least_request/least_request_lb.h"

namespace Envoy {
namespace Upstream {

double LeastRequestLoadBalancer::hostWeight(const Host& host) const {
  // This method is called to calculate the dynamic weight as following when all load balancing
  // weights are not equal:
  //
  // `weight = load_balancing_weight / (active_requests + 1)^active_request_bias`
  //
  // `active_request_bias` can be configured via runtime and its value is cached in
  // `active_request_bias_` to avoid having to do a runtime lookup each time a host weight is
  // calculated.
  //
  // When `active_request_bias == 0.0` we behave like `RoundRobinLoadBalancer` and return the
  // host weight without considering the number of active requests at the time we do the pick.
  //
  // When `active_request_bias > 0.0` we scale the host weight by the number of active
  // requests at the time we do the pick. We always add 1 to avoid division by 0.
  //
  // It might be possible to do better by picking two hosts off of the schedule, and selecting the
  // one with fewer active requests at the time of selection.

  double host_weight = static_cast<double>(host.weight());

  // If the value of active requests is the max value, adding +1 will overflow
  // it and cause a divide by zero. This won't happen in normal cases but stops
  // failing fuzz tests
  const uint64_t active_request_value =
      host.stats().rq_active_.value() != std::numeric_limits<uint64_t>::max()
          ? host.stats().rq_active_.value() + 1
          : host.stats().rq_active_.value();

  if (active_request_bias_ == 1.0) {
    host_weight = static_cast<double>(host.weight()) / active_request_value;
  } else if (active_request_bias_ != 0.0) {
    host_weight =
        static_cast<double>(host.weight()) / std::pow(active_request_value, active_request_bias_);
  }

  if (!noHostsAreInSlowStart()) {
    return applySlowStartFactor(host_weight, host);
  } else {
    return host_weight;
  }
}

HostConstSharedPtr LeastRequestLoadBalancer::unweightedHostPeek(const HostVector&,
                                                                const HostsSource&) {
  // LeastRequestLoadBalancer can not do deterministic preconnecting, because
  // any other thread might select the least-requested-host between preconnect and
  // host-pick, and change the rq_active checks.
  return nullptr;
}

HostConstSharedPtr LeastRequestLoadBalancer::unweightedHostPick(const HostVector& hosts_to_use,
                                                                const HostsSource&) {
  HostSharedPtr candidate_host = nullptr;

  switch (selection_method_) {
  case envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest::FULL_SCAN:
    candidate_host = unweightedHostPickFullScan(hosts_to_use);
    break;
  case envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest::N_CHOICES:
    candidate_host = unweightedHostPickNChoices(hosts_to_use);
    break;
  default:
    IS_ENVOY_BUG("unknown selection method specified for least request load balancer");
  }

  return candidate_host;
}

HostSharedPtr LeastRequestLoadBalancer::unweightedHostPickFullScan(const HostVector& hosts_to_use) {
  HostSharedPtr candidate_host = nullptr;

  size_t num_hosts_known_tied_for_least = 0;

  const size_t num_hosts = hosts_to_use.size();

  for (size_t i = 0; i < num_hosts; ++i) {
    const HostSharedPtr& sampled_host = hosts_to_use[i];

    if (candidate_host == nullptr) {
      // Make a first choice to start the comparisons.
      num_hosts_known_tied_for_least = 1;
      candidate_host = sampled_host;
      continue;
    }

    const auto candidate_active_rq = candidate_host->stats().rq_active_.value();
    const auto sampled_active_rq = sampled_host->stats().rq_active_.value();

    if (sampled_active_rq < candidate_active_rq) {
      // Reset the count of known tied hosts.
      num_hosts_known_tied_for_least = 1;
      candidate_host = sampled_host;
    } else if (sampled_active_rq == candidate_active_rq) {
      ++num_hosts_known_tied_for_least;

      // Use reservoir sampling to select 1 unique sample from the total number of hosts N
      // that will tie for least requests after processing the full hosts array.
      //
      // Upon each new tie encountered, replace candidate_host with sampled_host
      // with probability (1 / num_hosts_known_tied_for_least percent).
      // The end result is that each tied host has an equal 1 / N chance of being the
      // candidate_host returned by this function.
      const size_t random_tied_host_index = random_.random() % num_hosts_known_tied_for_least;
      if (random_tied_host_index == 0) {
        candidate_host = sampled_host;
      }
    }
  }

  return candidate_host;
}

HostSharedPtr LeastRequestLoadBalancer::unweightedHostPickNChoices(const HostVector& hosts_to_use) {
  HostSharedPtr candidate_host = nullptr;

  for (uint32_t choice_idx = 0; choice_idx < choice_count_; ++choice_idx) {
    const int rand_idx = random_.random() % hosts_to_use.size();
    const HostSharedPtr& sampled_host = hosts_to_use[rand_idx];

    if (candidate_host == nullptr) {
      // Make a first choice to start the comparisons.
      candidate_host = sampled_host;
      continue;
    }

    const auto candidate_active_rq = candidate_host->stats().rq_active_.value();
    const auto sampled_active_rq = sampled_host->stats().rq_active_.value();

    if (sampled_active_rq < candidate_active_rq) {
      candidate_host = sampled_host;
    }
  }

  return candidate_host;
}

} // namespace Upstream
} // namespace Envoy
