#pragma once

#include <cstdint>
#include <vector>

#include "common/common/phantom.h"

namespace Envoy {
namespace Upstream {

// Phantom type indicating that the type is related to load.
struct Load {};

// Mapping from a priority to how much of the total traffic load should be directed to this
// priority. For example, {50, 30, 20} means that 50% of traffic should go to P0, 30% to P1
// and 20% to P2.
//
// This should either sum to 100 or consist of all zeros.
using PriorityLoad = Phantom<std::vector<uint32_t>, Load>;

// PriorityLoad specific to degraded hosts.
struct DegradedLoad : PriorityLoad {
  using PriorityLoad::PriorityLoad;
};

// PriorityLoad specific to healthy hosts.
struct HealthyLoad : PriorityLoad {
  using PriorityLoad::PriorityLoad;
};

struct HealthyAndDegradedLoad {
  HealthyLoad healthy_priority_load_;
  DegradedLoad degraded_priority_load_;
};

// Phantom type indicating that the type is related to host availability.
struct Availability {};

// Mapping from a priority how available the given priority is, e.g., the ratio of healthy host to
// total hosts.
using PriorityAvailability = Phantom<std::vector<uint32_t>, Availability>;

// Availability specific to degraded hosts.
struct DegradedAvailability : PriorityAvailability {
  using PriorityAvailability::PriorityAvailability;
};

// Availability specific to healthy hosts.
struct HealthyAvailability : PriorityAvailability {
  using PriorityAvailability::PriorityAvailability;
};

// Phantom type indicating that the type is related to healthy hosts.
struct Healthy {};
// Phantom type indicating that the type is related to degraded hosts.
struct Degraded {};
// Phantom type indicating that the type is related to excluded hosts.
struct Excluded {};

} // namespace Upstream
} // namespace Envoy
