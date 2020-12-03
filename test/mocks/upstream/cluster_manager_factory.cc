#include "cluster_manager_factory.h"

namespace Envoy {
namespace Upstream {

MockClusterManagerFactory::MockClusterManagerFactory() : upstream_stat_names_(*symbol_table_) {}

MockClusterManagerFactory::~MockClusterManagerFactory() = default;

} // namespace Upstream
} // namespace Envoy
