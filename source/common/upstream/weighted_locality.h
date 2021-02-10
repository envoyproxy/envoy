#pragma once

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

namespace Envoy {
namespace Upstream {

// An endpoint's locality decorated with priority and weight.
class WeightedLocality {
public:
  WeightedLocality(const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint);

  bool isWeightSet() const { return is_weight_set_; }
  uint32_t loadBalancingWeight() const { return load_balancing_weight_; }
  const envoy::config::core::v3::Locality& locality() const { return locality_; }
  uint32_t priority() const { return priority_; }

private:
  const bool is_weight_set_;
  const uint32_t load_balancing_weight_;
  const envoy::config::core::v3::Locality locality_;
  const uint32_t priority_;
};

} // namespace Upstream
} // namespace Envoy
