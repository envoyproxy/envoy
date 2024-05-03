#pragma once

#include <memory>
#include <string>

#include "envoy/config/config_provider_manager.h"
#include "envoy/config/route/v3/route.pb.h"
#include "envoy/config/typed_config.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"
#include "envoy/router/rds.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/filter_config.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Router {

/**
 * The RouteConfigProviderManager exposes the ability to get a RouteConfigProvider. This interface
 * is exposed to the Server's FactoryContext in order to allow HttpConnectionManagers to get
 * RouteConfigProviders.
 */
class RouteConfigProviderManager {
public:
  virtual ~RouteConfigProviderManager() = default;

  /**
   * Get a RouteConfigProviderPtr for a route from RDS. Ownership of the RouteConfigProvider is the
   * HttpConnectionManagers who calls this function. The RouteConfigProviderManager holds raw
   * pointers to the RouteConfigProviders. Clean up of the pointers happen from the destructor of
   * the RouteConfigProvider. This method creates a RouteConfigProvider which may share the
   * underlying RDS subscription with the same (route_config_name, cluster).
   * @param rds supplies the proto configuration of an RDS-configured RouteConfigProvider.
   * @param factory_context is the context to use for the route config provider.
   * @param stat_prefix supplies the stat_prefix to use for the provider stats.
   * @param init_manager the Init::Manager used to coordinate initialization of a the underlying RDS
   * subscription.
   */
  virtual RouteConfigProviderSharedPtr createRdsRouteConfigProvider(
      const envoy::extensions::filters::network::http_connection_manager::v3::Rds& rds,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Init::Manager& init_manager) PURE;

  /**
   * Get a RouteConfigSharedPtr for a statically defined route. Ownership is as described for
   * getRdsRouteConfigProvider above. This method always create a new RouteConfigProvider.
   * @param route_config supplies the RouteConfiguration for this route
   * @param factory_context is the context to use for the route config provider.
   * @param validator is the message validator for route config.
   */
  virtual RouteConfigProviderPtr
  createStaticRouteConfigProvider(const envoy::config::route::v3::RouteConfiguration& route_config,
                                  Server::Configuration::ServerFactoryContext& factory_context,
                                  ProtobufMessage::ValidationVisitor& validator) PURE;
};

using RouteConfigProviderManagerPtr = std::unique_ptr<RouteConfigProviderManager>;
using RouteConfigProviderManagerSharedPtr = std::shared_ptr<RouteConfigProviderManager>;

// This factory exists to avoid direct-linking the SRDS libraries into Envoy so
// they can be compiled or substituted out.
class SrdsFactory : public Envoy::Config::UntypedFactory {
public:
  // UntypedFactory
  virtual std::string category() const override { return "envoy.srds_factory"; }
  virtual std::unique_ptr<Envoy::Config::ConfigProviderManager>
  createScopedRoutesConfigProviderManager(
      Server::Configuration::ServerFactoryContext& factory_context,
      Router::RouteConfigProviderManager& route_config_provider_manager) PURE;
  // If enabled in the HttpConnectionManager config, returns a ConfigProvider for scoped routing
  // configuration.
  virtual Envoy::Config::ConfigProviderPtr createConfigProvider(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          config,
      Server::Configuration::ServerFactoryContext& factory_context, const std::string& stat_prefix,
      Envoy::Config::ConfigProviderManager& scoped_routes_config_provider_manager) PURE;

  // If enabled in the HttpConnectionManager config, returns a ConfigProvider for scoped routing
  // configuration.
  virtual ScopeKeyBuilderPtr createScopeKeyBuilder(
      const envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
          config) PURE;
};

} // namespace Router
} // namespace Envoy
