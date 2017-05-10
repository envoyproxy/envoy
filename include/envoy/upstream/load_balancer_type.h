#pragma once

namespace Lyft {
namespace Upstream {

/**
 * Type of load balancing to perform.
 */
enum class LoadBalancerType { RoundRobin, LeastRequest, Random, RingHash };

} // Upstream
} // Lyft