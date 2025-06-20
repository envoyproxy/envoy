#include "cluster_real_priority_set.h"

namespace Envoy {
namespace Upstream {
MockClusterRealPrioritySet::MockClusterRealPrioritySet() : priority_set_(info_->statsScope()) {}

MockClusterRealPrioritySet::~MockClusterRealPrioritySet() = default;
} // namespace Upstream
} // namespace Envoy
