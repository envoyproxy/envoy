#include "common/upstream/health_discovery_service.h"

#include "test/mocks/upstream/mocks.h"

namespace Envoy {
namespace Upstream {

class TestHdsInfoFactory : public HdsInfoFactory, Logger::Loggable<Logger::Id::upstream> {
public:
  ClusterInfoConstSharedPtr
  createHdsClusterInfo(Runtime::Loader& runtime, const envoy::api::v2::Cluster& cluster,
                       const envoy::api::v2::core::BindConfig& bind_config, Stats::Store& stats,
                       Ssl::ContextManager& ssl_context_manager,
                       Secret::SecretManager& secret_manager, bool added_via_api) override;
};

} // namespace Upstream
} // namespace Envoy
