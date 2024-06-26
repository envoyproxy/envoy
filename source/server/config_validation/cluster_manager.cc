#include "source/server/config_validation/cluster_manager.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Upstream {

ClusterManagerPtr ValidationClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  auto cluster_manager = std::unique_ptr<ValidationClusterManager>{new ValidationClusterManager(
      bootstrap, *this, context_, stats_, tls_, context_.runtime(), context_.localInfo(),
      context_.accessLogManager(), context_.mainThreadDispatcher(), context_.admin(),
      context_.messageValidationContext(), context_.api(), http_context_, context_.grpcContext(),
      context_.routerContext(), server_)};
  return cluster_manager;
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
