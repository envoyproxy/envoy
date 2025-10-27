#include "source/server/config_validation/cluster_manager.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"

#include "source/common/common/utility.h"

namespace Envoy {
namespace Upstream {

absl::StatusOr<ClusterManagerPtr> ValidationClusterManagerFactory::clusterManagerFromProto(
    const envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
  absl::Status creation_status = absl::OkStatus();
  auto cluster_manager = std::unique_ptr<ValidationClusterManager>{
      new ValidationClusterManager(bootstrap, *this, context_, creation_status)};
  RETURN_IF_NOT_OK(creation_status);
  return cluster_manager;
}

absl::StatusOr<CdsApiPtr> ValidationClusterManagerFactory::createCds(
    const envoy::config::core::v3::ConfigSource& cds_config,
    const xds::core::v3::ResourceLocator* cds_resources_locator, ClusterManager& cm,
    bool support_multi_ads_sources) {
  // Create the CdsApiImpl...
  auto cluster_or_error = ProdClusterManagerFactory::createCds(cds_config, cds_resources_locator,
                                                               cm, support_multi_ads_sources);
  RETURN_IF_NOT_OK_REF(cluster_or_error.status());
  // ... and then throw it away, so that we don't actually connect to it.
  return nullptr;
}

} // namespace Upstream
} // namespace Envoy
