#include "cluster_manager_factory.h"

namespace Envoy {
namespace Upstream {

MockClusterManagerFactory::MockClusterManagerFactory()
    : cluster_manager_stat_names(*symbol_table_) {}

MockClusterManagerFactory::~MockClusterManagerFactory() = default;

} // namespace Upstream
} // namespace Envoy
