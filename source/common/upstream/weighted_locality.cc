#include "common/upstream/weighted_locality.h"

namespace Envoy {
namespace Upstream {

WeightedLocality::WeightedLocality(
    const envoy::config::endpoint::v3::LocalityLbEndpoints& locality_lb_endpoint)
    : is_weight_set_{locality_lb_endpoint.has_locality() &&
                     locality_lb_endpoint.has_load_balancing_weight()},
      load_balancing_weight_{locality_lb_endpoint.load_balancing_weight().value()},
      locality_{locality_lb_endpoint.locality()}, priority_{locality_lb_endpoint.priority()} {}

} // namespace Upstream
} // namespace Envoy
