#include "common/upstream/upstream_impl.h"

namespace Envoy {
namespace Upstream {

/**
 * Factory for unit testing Upstream::HdsDelegate
 */

class ClusterInfoFactory {
public:
  virtual ~ClusterInfoFactory() {}
  /**
   * @return ClusterInfoConstSharedPtr
   */

  virtual ClusterInfoConstSharedPtr
  createHdsClusterInfo(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
                       const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager,
                       Secret::SecretManager& secret_manager, bool added_via_api) PURE;
};

} // namespace Upstream
} // namespace Envoy
