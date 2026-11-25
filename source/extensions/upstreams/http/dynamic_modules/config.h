#pragma once

#include "envoy/extensions/upstreams/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/upstreams/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

/**
 * Config registration for the dynamic module upstream HTTP TCP bridge.
 * @see Router::GenericConnPoolFactory
 */
class DynamicModuleGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override;
  std::string category() const override { return "envoy.upstreams"; }
  Router::GenericConnPoolPtr createGenericConnPool(
      Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& thread_local_cluster,
      Router::GenericConnPoolFactory::UpstreamProtocol upstream_protocol,
      Upstream::ResourcePriority priority, std::optional<Envoy::Http::Protocol> downstream_protocol,
      Upstream::LoadBalancerContext* ctx, const Protobuf::Message& config) const override;
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::dynamic_modules::v3::Config>();
  }
  // Loads the module and creates the bridge configuration of a cluster, on the main thread.
  absl::StatusOr<Upstream::ProtocolOptionsConfigConstSharedPtr>
  createClusterOptions(const Protobuf::Any& typed_config,
                       Server::Configuration::ServerFactoryContext& context) const override;
};

DECLARE_FACTORY(DynamicModuleGenericConnPoolFactory);

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
