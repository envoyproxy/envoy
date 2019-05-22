#pragma once

#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"
#include "envoy/thread_local/thread_local.h"

#include "common/router/config_impl.h"
#include "common/router/scoped_config_manager.h"

namespace Envoy {
namespace Router {

/**
 * TODO(AndresGuedez): implement scoped routing logic.
 *
 * Each Envoy worker is assigned an instance of this type. When config updates are received,
 * addOrUpdateRoutingScope() and removeRoutingScope() are called to update the set of scoped routes.
 *
 * ConnectionManagerImpl::refreshCachedRoute() will call getRouterConfig() to obtain the
 * Router::ConfigConstSharedPtr to use for route selection.
 */
class ThreadLocalScopedConfigImpl : public ScopedConfig, public ThreadLocal::ThreadLocalObject {
public:
  ThreadLocalScopedConfigImpl(
      envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder
          scope_key_builder)
      : scope_key_builder_(std::move(scope_key_builder)) {}

  void addOrUpdateRoutingScope(const ScopedRouteInfoConstSharedPtr& scoped_route_info);
  void removeRoutingScope(const std::string& scope_name);

  // Envoy::Router::ScopedConfig
  Router::ConfigConstSharedPtr getRouteConfig(const Http::HeaderMap& headers) const override;

private:
  const envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes::ScopeKeyBuilder
      scope_key_builder_;
};

/**
 * A NULL implementation of the scoped routing configuration.
 */
class NullScopedConfigImpl : public ScopedConfig {
public:
  Router::ConfigConstSharedPtr getRouteConfig(const Http::HeaderMap&) const override {
    return std::make_shared<const NullConfigImpl>();
  }
};

} // namespace Router
} // namespace Envoy
