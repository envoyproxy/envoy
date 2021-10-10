#include "source/extensions/filters/network/thrift_proxy/router/rds_impl.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include "envoy/router/rds/route_config_update_receiver.h"

#include "source/common/router/rds/rds_route_config_provider_impl.h"
#include "source/common/router/rds/rds_route_config_subscription.h"
#include "source/common/router/rds/route_config_update_receiver_impl.h"
#include "source/common/router/rds/static_route_config_provider_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

using RouteConfigUpdatePtr = std::unique_ptr<Envoy::Router::Rds::RouteConfigUpdateReceiver<
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>>;
using RouteConfigUpdateReceiverImpl = Envoy::Router::Rds::RouteConfigUpdateReceiverImpl<
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>;

using RdsRouteConfigSubscription = Envoy::Router::Rds::RdsRouteConfigSubscription<
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>;
using RdsRouteConfigSubscriptionSharedPtr = std::shared_ptr<RdsRouteConfigSubscription>;

using StaticRouteConfigProviderImpl = Envoy::Router::Rds::StaticRouteConfigProviderImpl<
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>;
using RdsRouteConfigProviderImpl = Envoy::Router::Rds::RdsRouteConfigProviderImpl<
    envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>;
using RdsRouteConfigProviderImplSharedPtr = std::shared_ptr<RdsRouteConfigProviderImpl>;

RouteConfigProviderManagerImpl::RouteConfigProviderManagerImpl(Server::Admin& admin)
    : Envoy::Router::Rds::RouteConfigProviderManagerImpl<
          envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>(
          admin) {}

RouteConfigProviderSharedPtr RouteConfigProviderManagerImpl::createRdsRouteConfigProvider(
    const envoy::extensions::filters::network::thrift_proxy::v3::Rds& rds,
    Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
    Init::Manager& init_manager) {
  // RdsRouteConfigSubscriptions are unique based on their serialized RDS config.
  const uint64_t manager_identifier = MessageUtil::hash(rds);

  auto existing_provider =
      reuseDynamicProvider(manager_identifier, init_manager, rds.route_config_name());

  if (!existing_provider) {
    // std::make_shared does not work for classes with private constructors. There are ways
    // around it. However, since this is not a performance critical path we err on the side
    // of simplicity.

    RouteConfigUpdatePtr config_update(new RouteConfigUpdateReceiverImpl(factory_context, *this));
    RdsRouteConfigSubscriptionSharedPtr subscription(new RdsRouteConfigSubscription(
        std::move(config_update), rds.config_source(), rds.route_config_name(), manager_identifier,
        factory_context, stat_prefix, *this));
    RdsRouteConfigProviderImplSharedPtr new_provider{
        new RdsRouteConfigProviderImpl(std::move(subscription), factory_context)};
    insertDynamicProvider(manager_identifier, new_provider,
                          &new_provider->subscription().initTarget(), init_manager);
    return new_provider;
  } else {
    return existing_provider;
  }
}

RouteConfigProviderPtr RouteConfigProviderManagerImpl::createStaticRouteConfigProvider(
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& route_config,
    Server::Configuration::ServerFactoryContext& factory_context) {
  auto initial_config = createConfig(route_config);
  auto provider = std::make_unique<StaticRouteConfigProviderImpl>(initial_config, route_config,
                                                                  factory_context, *this);
  insertStaticProvider(provider.get());
  return provider;
}

std::shared_ptr<const Config> RouteConfigProviderManagerImpl::createConfig(
    const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& rc) const {
  return std::make_shared<const ConfigImpl>(rc);
}

std::shared_ptr<const Config> RouteConfigProviderManagerImpl::createConfig() const {
  return std::make_shared<const NullConfigImpl>();
}

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
