#include "hds_helper.h"

namespace Envoy {
namespace Upstream {

ClusterInfoConstSharedPtr TestHdsInfoFactory::createHdsClusterInfo(
    Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
    const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
    Ssl::ContextManager& ssl_context_manager, Secret::SecretManager& secret_manager,
    bool added_via_api) {

  ENVOY_LOG(debug, "we're here in the test");
  ClusterInfoConstSharedPtr info;
  UNREFERENCED_PARAMETER(runtime);
  UNREFERENCED_PARAMETER(cluster);
  UNREFERENCED_PARAMETER(bind_config);
  UNREFERENCED_PARAMETER(stats);
  UNREFERENCED_PARAMETER(ssl_context_manager);
  UNREFERENCED_PARAMETER(secret_manager);
  UNREFERENCED_PARAMETER(added_via_api);

  info.reset(new NiceMock<Upstream::MockClusterInfo>());
  return info;
}

} // namespace Upstream
} // namespace Envoy
