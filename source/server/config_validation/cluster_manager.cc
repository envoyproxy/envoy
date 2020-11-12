#include "server/config_validation/cluster_manager.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Upstream {

CdsApiPtr
ValidationClusterManagerFactory::createCds(const envoy::config::core::v3::ConfigSource& cds_config,
                                           ClusterManager& cm) {
  // Create the CdsApiImpl...
  ProdClusterManagerFactory::createCds(cds_config, cm);
  // ... and then throw it away, so that we don't actually connect to it.
  return nullptr;
}

} // namespace Upstream
} // namespace Envoy
