#include "server/config_validation/cluster_manager.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Upstream {

ClusterManagerPtr ValidationClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  return std::make_unique<ValidationClusterManager>(
      bootstrap, *this, stats_, tls_, runtime_, local_info_, log_manager_, main_thread_dispatcher_,
      admin_, validation_context_, api_, http_context_, grpc_context_, router_context_);
}

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
