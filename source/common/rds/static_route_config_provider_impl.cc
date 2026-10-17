#include "source/common/rds/static_route_config_provider_impl.h"

#include "source/common/rds/util.h"

namespace Envoy {
namespace Rds {

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const Protobuf::Message& route_config_proto, ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context, Init::Manager& init_manager,
    RouteConfigProviderManager& route_config_provider_manager)
    : serialized_route_config_proto_(route_config_proto.SerializeAsString()),
      route_config_name_(
          resourceName(route_config_provider_manager.protoTraits(), route_config_proto)),
      config_(config_traits.createConfig(route_config_proto, factory_context, init_manager,
                                         true /* validate unknown cluster */)),
      last_updated_(factory_context.timeSource().systemTime()),
      config_info_(ConfigInfo{serialized_route_config_proto_, route_config_name_, ""}),
      route_config_provider_manager_(route_config_provider_manager) {}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

} // namespace Rds
} // namespace Envoy
