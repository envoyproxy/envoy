#include "source/common/rds/static_route_config_provider_impl.h"

#include "source/common/rds/util.h"

namespace Envoy {
namespace Rds {

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const Protobuf::Message& route_config_proto, ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context,
    RouteConfigProviderManager& route_config_provider_manager)
    : route_config_proto_(
          cloneProto(route_config_provider_manager.protoTraits(), route_config_proto)),
      config_(config_traits.createConfig(*route_config_proto_, factory_context,
                                         true /* validate unknown cluster */)),
      last_updated_(factory_context.timeSource().systemTime()),
      config_info_(ConfigInfo{*route_config_proto_, ""}),
      route_config_provider_manager_(route_config_provider_manager) {}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

} // namespace Rds
} // namespace Envoy
