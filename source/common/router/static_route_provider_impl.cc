#include "source/common/router/static_route_provider_impl.h"

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

namespace Envoy {
namespace Router {

StaticRouteConfigProviderImpl::StaticRouteConfigProviderImpl(
    const envoy::config::route::v3::RouteConfiguration& config, Rds::ConfigTraits& config_traits,
    Server::Configuration::ServerFactoryContext& factory_context,
    Rds::RouteConfigProviderManager& route_config_provider_manager)
    : base_(config, config_traits, factory_context, route_config_provider_manager),
      route_config_provider_manager_(route_config_provider_manager) {}

StaticRouteConfigProviderImpl::~StaticRouteConfigProviderImpl() {
  route_config_provider_manager_.eraseStaticProvider(this);
}

ConfigConstSharedPtr StaticRouteConfigProviderImpl::configCast() const {
  ASSERT(dynamic_cast<const Config*>(StaticRouteConfigProviderImpl::config().get()));
  return std::static_pointer_cast<const Config>(StaticRouteConfigProviderImpl::config());
}

} // namespace Router
} // namespace Envoy
