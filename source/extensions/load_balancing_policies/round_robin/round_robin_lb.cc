#include "source/extensions/load_balancing_policies/round_robin/round_robin_lb.h"

namespace Envoy {
namespace Upstream {

TypedRoundRobinLbConfig::TypedRoundRobinLbConfig(const RoundRobinLbProto& lb_config)
    : lb_config_(lb_config) {}

TypedRoundRobinLbConfig::TypedRoundRobinLbConfig(const CommonLbConfigProto& common_lb_config,
                                                 const LegacyRoundRobinLbProto& lb_config) {
  Upstream::LoadBalancerConfigHelper::convertSlowStartConfigTo(lb_config, lb_config_);
  Upstream::LoadBalancerConfigHelper::convertLocalityLbConfigTo(common_lb_config, lb_config_);
}

} // namespace Upstream
} // namespace Envoy
