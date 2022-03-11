#include "source/server/config_validation/cluster_manager.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Upstream {

ClusterManagerPtr ValidationClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  return std::make_unique<ValidationClusterManager>(
      bootstrap, *this, stats_, tls_, context_.runtime(), local_info_, log_manager_,
      context_.mainThreadDispatcher(), admin_, validation_context_, context_.api(), http_context_,
      grpc_context_, router_context_, server_);
}

CdsApiPtr ValidationClusterManagerFactory::createCds(
    const envoy::config::core::v3::ConfigSource& cds_config,
    const xds::core::v3::ResourceLocator* cds_resources_locator, ClusterManager& cm) {
  // Create the CdsApiImpl...
  ProdClusterManagerFactory::createCds(cds_config, cds_resources_locator, cm);
  // ... and then throw it away, so that we don't actually connect to it.
  return nullptr;
}

} // namespace Upstream
} // namespace Envoy
