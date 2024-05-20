#include "source/common/router/route_provider_manager.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/config/api_version.h"
#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/common/router/rds_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"
#include "source/common/router/static_route_provider_impl.h"

namespace Envoy {
namespace Router {

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(OptRef<Server::Admin> admin)
    : manager_(admin, "routes", proto_traits_) {}

Router::RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::createRdsRouteConfigProvider(
    const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager) {
  auto provider = manager_.addDynamicProvider(
      rds, rds.route_config_name(), init_manager,
      [&factory_context, &rds, &stat_prefix, this](uint64_t manager_identifier) {
        auto config_update =
            std::make_unique<RouteConfigUpdateReceiverImpl>(proto_traits_, factory_context);
        auto resource_decoder = std::make_shared<
            Envoy::Config::OpaqueResourceDecoderImpl<envoy::config::route::v3::RouteConfiguration>>(
            factory_context.messageValidationContext().dynamicValidationVisitor(), "name");
        auto subscription = std::make_shared<RdsRouteConfigSubscription>(
            std::move(config_update), std::move(resource_decoder), rds, manager_identifier,
            factory_context, stat_prefix, manager_);
        auto provider =
            std::make_shared<RdsRouteConfigProviderImpl>(std::move(subscription), factory_context);
        return std::make_pair(provider, &provider->subscription().initTarget());
      });
  ASSERT(dynamic_cast<RouteConfigProvider*>(provider.get()));
  return std::static_pointer_cast<RouteConfigProvider>(provider);
}

RouteConfigProviderPtr RouteConfigProviderManagerImpl::createStaticRouteConfigProvider(
    const envoy::config::route::v3::RouteConfiguration& route_config,
    Server::Configuration::ServerFactoryContext& factory_context,
    ProtobufMessage::ValidationVisitor& validator) {
  auto provider = manager_.addStaticProvider([&factory_context, &validator, &route_config, this]() {
    ConfigTraitsImpl config_traits(validator);
    return std::make_unique<StaticRouteConfigProviderImpl>(route_config, config_traits,
                                                           factory_context, manager_);
  });
  ASSERT(dynamic_cast<RouteConfigProvider*>(provider.get()));
  return RouteConfigProviderPtr(static_cast<RouteConfigProvider*>(provider.release()));
}

} // namespace Router
} // namespace Envoy
