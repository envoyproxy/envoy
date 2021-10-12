#pragma once

#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.validate.h"
#include "envoy/singleton/instance.h"

#include "source/common/router/rds/config_factory.h"
#include "source/common/router/rds/route_config_provider_manager_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/rds.h"
#include "source/extensions/filters/network/thrift_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RouteConfigProviderManagerImpl
    : public RouteConfigProviderManager,
      public Singleton::Instance,
      public Envoy::Router::Rds::RouteConfigProviderManagerImpl<
          envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config>,
      public Envoy::Router::Rds::ConfigFactory<
          envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration, Config> {
public:
  RouteConfigProviderManagerImpl(Server::Admin& admin);

  // RouteConfigProviderManager
  RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const envoy::extensions::filters::network::thrift_proxy::v3::Trds& trds,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager) override;

  RouteConfigProviderPtr createStaticRouteConfigProvider(
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& route_config,
      Server::Configuration::ServerFactoryContext& factory_context) override;

private:
  std::shared_ptr<const Config>
  createConfig(const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& rc)
      const override;
  std::shared_ptr<const Config> createConfig() const override;
};

using RouteConfigProviderManagerImplPtr = std::unique_ptr<RouteConfigProviderManagerImpl>;
using RouteConfigProviderManagerImplSharedPtr = std::shared_ptr<RouteConfigProviderManagerImpl>;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
