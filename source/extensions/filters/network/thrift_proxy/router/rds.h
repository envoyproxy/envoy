#pragma once

#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/route.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/init/manager.h"
#include "envoy/rds/route_config_provider.h"
#include "envoy/server/factory_context.h"

#include "source/extensions/filters/network/thrift_proxy/router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

/**
 * The RouteConfigProviderManager exposes the ability to get a RouteConfigProvider. This interface
 * is exposed to the Server's FactoryContext in order to allow ThriftProxy to get
 * RouteConfigProviders.
 */
class RouteConfigProviderManager {
public:
  virtual ~RouteConfigProviderManager() = default;

  /**
   * Get a RouteConfigProviderPtr for a route from RDS. Ownership of the RouteConfigProvider is the
   * ThriftProxy who calls this function. The RouteConfigProviderManager holds raw
   * pointers to the RouteConfigProviders. Clean up of the pointers happen from the destructor of
   * the RouteConfigProvider. This method creates a RouteConfigProvider which may share the
   * underlying RDS subscription with the same (route_config_name, cluster).
   * @param trds supplies the proto configuration of an RDS-configured RouteConfigProvider.
   * @param factory_context is the context to use for the route config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param init_manager the Init::Manager used to coordinate initialization of a the underlying RDS
   * subscription.
   */
  virtual Rds::RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const envoy::extensions::filters::network::thrift_proxy::v3::Trds& trds,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager) PURE;

  /**
   * Get a RouteConfigSharedPtr for a statically defined route. Ownership is as described for
   * createRdsRouteConfigProvider above. This method always create a new RouteConfigProvider.
   * @param route_config supplies the RouteConfiguration for this route
   * @param factory_context is the context to use for the route config provider.
   */
  virtual Rds::RouteConfigProviderPtr createStaticRouteConfigProvider(
      const envoy::extensions::filters::network::thrift_proxy::v3::RouteConfiguration& route_config,
      Server::Configuration::ServerFactoryContext& factory_context) PURE;
};

using RouteConfigProviderManagerPtr = std::unique_ptr<RouteConfigProviderManager>;
using RouteConfigProviderManagerSharedPtr = std::shared_ptr<RouteConfigProviderManager>;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
