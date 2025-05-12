#pragma once

#include "source/extensions/load_balancing_policies/common/load_balancer_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Weighted Least Request load balancer.
 *
 * In a normal setup when all hosts have the same weight it randomly picks up N healthy hosts
 * (where N is specified in the LB configuration) and compares number of active requests. Technique
 * is based on http://www.eecs.harvard.edu/~michaelm/postscripts/mythesis.pdf and is known as P2C
 * (power of two choices).
 *
 * When hosts have different weights, an RR EDF schedule is used. Host weight is scaled
 * by the number of active requests at pick/insert time. Thus, hosts will never fully drain as
 * they would in normal P2C, though they will get picked less and less often. In the future, we
 * can consider two alternate algorithms:
 * 1) Expand out all hosts by weight (using more memory) and do standard P2C.
 * 2) Use a weighted Maglev table, and perform P2C on two random hosts selected from the table.
 *    The benefit of the Maglev table is at the expense of resolution, memory usage is capped.
 *    Additionally, the Maglev table can be shared amongst all threads.
 */
class LeastRequestLoadBalancer : public EdfLoadBalancerBase {
public:
  LeastRequestLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random,
      const envoy::config::cluster::v3::Cluster::CommonLbConfig& common_config,
      OptRef<const envoy::config::cluster::v3::Cluster::LeastRequestLbConfig> least_request_config,
      TimeSource& time_source)
      : EdfLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random,
            PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(common_config, healthy_panic_threshold,
                                                           100, 50),
            LoadBalancerConfigHelper::localityLbConfigFromCommonLbConfig(common_config),
            least_request_config.has_value()
                ? LoadBalancerConfigHelper::slowStartConfigFromLegacyProto(
                      least_request_config.ref())
                : absl::nullopt,
            time_source),
        choice_count_(
            least_request_config.has_value()
                ? PROTOBUF_GET_WRAPPED_OR_DEFAULT(least_request_config.ref(), choice_count, 2)
                : 2),
        active_request_bias_runtime_(
            least_request_config.has_value() && least_request_config->has_active_request_bias()
                ? absl::optional<Runtime::Double>(
                      {least_request_config->active_request_bias(), runtime})
                : absl::nullopt) {
    initialize();
  }

  LeastRequestLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
      const envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest&
          least_request_config,
      TimeSource& time_source)
      : EdfLoadBalancerBase(
            priority_set, local_priority_set, stats, runtime, random, healthy_panic_threshold,
            LoadBalancerConfigHelper::localityLbConfigFromProto(least_request_config),
            LoadBalancerConfigHelper::slowStartConfigFromProto(least_request_config), time_source),
        choice_count_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(least_request_config, choice_count, 2)),
        active_request_bias_runtime_(
            least_request_config.has_active_request_bias()
                ? absl::optional<Runtime::Double>(
                      {least_request_config.active_request_bias(), runtime})
                : absl::nullopt),
        selection_method_(least_request_config.selection_method()) {
    initialize();
  }

protected:
  void refresh(uint32_t priority) override {
    active_request_bias_ = active_request_bias_runtime_ != absl::nullopt
                               ? active_request_bias_runtime_.value().value()
                               : 1.0;

    if (active_request_bias_ < 0.0 || std::isnan(active_request_bias_)) {
      ENVOY_LOG_MISC(warn,
                     "upstream: invalid active request bias supplied (runtime key {}), using 1.0",
                     active_request_bias_runtime_->runtimeKey());
      active_request_bias_ = 1.0;
    }

    EdfLoadBalancerBase::refresh(priority);
  }

private:
  void refreshHostSource(const HostsSource&) override {}
  double hostWeight(const Host& host) const override;
  HostConstSharedPtr unweightedHostPeek(const HostVector& hosts_to_use,
                                        const HostsSource& source) override;
  HostConstSharedPtr unweightedHostPick(const HostVector& hosts_to_use,
                                        const HostsSource& source) override;
  HostSharedPtr unweightedHostPickFullScan(const HostVector& hosts_to_use);
  HostSharedPtr unweightedHostPickNChoices(const HostVector& hosts_to_use);

  const uint32_t choice_count_;

  // The exponent used to calculate host weights can be configured via runtime. We cache it for
  // performance reasons and refresh it in `LeastRequestLoadBalancer::refresh(uint32_t priority)`
  // whenever a `HostSet` is updated.
  double active_request_bias_{};

  const absl::optional<Runtime::Double> active_request_bias_runtime_;
  const envoy::extensions::load_balancing_policies::least_request::v3::LeastRequest::SelectionMethod
      selection_method_{};
};

} // namespace Upstream
} // namespace Envoy
