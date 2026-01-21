#pragma once

#include "envoy/extensions/upstreams/http/dynamic_modules/v3/dynamic_modules.pb.h"
#include "envoy/extensions/upstreams/http/dynamic_modules/v3/dynamic_modules.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Http {
namespace DynamicModules {

/**
 * Factory for creating DynamicModuleTcpConnPool instances.
 * @see Router::GenericConnPoolFactory.
 */
class DynamicModuleGenericConnPoolFactory : public Router::GenericConnPoolFactory {
public:
  std::string name() const override { return "envoy.upstreams.http.dynamic_modules"; }
  std::string category() const override { return "envoy.upstreams"; }

  Router::GenericConnPoolPtr createGenericConnPool(
      Upstream::HostConstSharedPtr host, Upstream::ThreadLocalCluster& thread_local_cluster,
      Router::GenericConnPoolFactory::UpstreamProtocol upstream_protocol,
      Upstream::ResourcePriority priority,
      absl::optional<Envoy::Http::Protocol> downstream_protocol, Upstream::LoadBalancerContext* ctx,
      const Protobuf::Message& config) const override;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::upstreams::http::dynamic_modules::v3::
                                DynamicModuleHttpTcpBridgeConfig>();
  }
};

DECLARE_FACTORY(DynamicModuleGenericConnPoolFactory);

} // namespace DynamicModules
} // namespace Http
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
