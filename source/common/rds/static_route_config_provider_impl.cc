#include "source/common/rds/static_route_config_provider_impl.h"

namespace Envoy {
namespace Rds {

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const Protobuf::Message& route_config_proto, ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context,
    RouteConfigProviderManager& route_config_provider_manager)
    : route_config_proto_(
          route_config_provider_manager.protoTraits().cloneProto(route_config_proto)),
      config_(config_traits.createConfig(*route_config_proto_)),
      last_updated_(factory_context.timeSource().systemTime()),
      route_config_provider_manager_(route_config_provider_manager) {}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

absl::optional<RouteConfigProvider::ConfigInfo> StaticRouteConfigProviderImpl::configInfo() const {
  return RouteConfigProvider::ConfigInfo{*route_config_proto_, ""};
}

} // namespace Rds
} // namespace Envoy
