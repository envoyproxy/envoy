#include "cluster_manager_factory.h"

namespace Envoy {
namespace Upstream {

std::ostream& operator<<(std::ostream& out, const ThreadAwareLoadBalancerPtr&) { return out; }

MockClusterManagerFactory::MockClusterManagerFactory() = default;

MockClusterManagerFactory::~MockClusterManagerFactory() = default;
} // namespace Upstream
} // namespace Envoy
