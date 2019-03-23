#pragma once

#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"

#include "common/router/config_impl.h"
#include "common/router/thread_local_scoped_config.h"

namespace Envoy {
namespace Router {

// TODO(AndresGuedez):
class ScopedConfigManager {
public:
  using ScopedRouteMap = std::map<std::string, ScopedRouteInfoConstSharedPtr>;

  // Adds/updates a routing scope specified via the Scoped RDS API. This scope will be added to the
  // set of scopes matched against the scope keys built for each HTTP request.
  ScopedRouteInfoConstSharedPtr
  addOrUpdateRoutingScope(const envoy::api::v2::ScopedRouteConfiguration& scoped_route_config,
                          const std::string& version_info);

  // Removes a routing scope from the set of scopes matched against each HTTP request.
  bool removeRoutingScope(const std::string& scope_name);

  const ScopedRouteMap& scopedRouteMap() const { return scoped_route_map_; }

private:
  ScopedRouteMap scoped_route_map_;
};

/**
 * The implementation of scoped routing configuration logic.
 * NOTE: This is not yet implemented and simply mimics the behavior of the
 * NullScopedConfigImpl.
 *
 * TODO(AndresGuedez): implement scoped routing logic.
 */
class ThreadLocalScopedConfigImpl : public ScopedConfig, public ThreadLocalScopedConfig {
public:
  ThreadLocalScopedConfigImpl(const envoy::config::filter::network::http_connection_manager::v2::
                                  ScopedRoutes::ScopeKeyBuilder& scope_key_builder)
      : scope_key_builder_(scope_key_builder) {}

  // Envoy::Router::ThreadLocalScopedConfig
  void addOrUpdateRoutingScope(ScopedRouteInfoConstSharedPtr scoped_route_info) override;
  void removeRoutingScope(const std::string& scope_name) override;

  // Envoy::Router::ScopedConfig
  Router::ConfigConstSharedPtr getRouterConfig(const Http::HeaderMap& headers) const override;

private:
  const envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder
      scope_key_builder_;
};

/**
 * An empty implementation of the scoped routing configuration.
 */
class NullScopedConfigImpl : public ScopedConfig {
public:
  Router::ConfigConstSharedPtr getRouterConfig(const Http::HeaderMap&) const override {
    return std::make_shared<const NullConfigImpl>();
  }
};

} // namespace Router
} // namespace Envoy
