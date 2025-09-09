#pragma once

#include <memory>
#include <typeinfo>

#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/router/rds.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/hash.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"

#include "absl/numeric/int128.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Router {

using envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes;

/**
 * Base class for fragment builders.
 */
class FragmentBuilderBase {
public:
  explicit FragmentBuilderBase(ScopedRoutes::ScopeKeyBuilder::FragmentBuilder&& config)
      : config_(std::move(config)) {}
  virtual ~FragmentBuilderBase() = default;

  // Returns a fragment if the fragment rule applies, a nullptr indicates no fragment could be
  // generated from the headers.
  virtual std::unique_ptr<ScopeKeyFragmentBase>
  computeFragment(const Http::HeaderMap& headers) const PURE;

protected:
  const ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config_;
};

class HeaderValueExtractorImpl : public FragmentBuilderBase {
public:
  explicit HeaderValueExtractorImpl(ScopedRoutes::ScopeKeyBuilder::FragmentBuilder&& config);

  std::unique_ptr<ScopeKeyFragmentBase>
  computeFragment(const Http::HeaderMap& headers) const override;

private:
  const ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor&
      header_value_extractor_config_;
};

/**
 * Base class for ScopeKeyBuilder implementations.
 */
class ScopeKeyBuilderBase : public ScopeKeyBuilder {
public:
  explicit ScopeKeyBuilderBase(ScopedRoutes::ScopeKeyBuilder&& config)
      : config_(std::move(config)) {}

protected:
  const ScopedRoutes::ScopeKeyBuilder config_;
};

class ScopeKeyBuilderImpl : public ScopeKeyBuilderBase {
public:
  explicit ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder&& config);

  ScopeKeyPtr computeScopeKey(const Http::HeaderMap& headers) const override;

private:
  std::vector<std::unique_ptr<FragmentBuilderBase>> fragment_builders_;
};

// ScopedRouteConfiguration and corresponding RouteConfigProvider.
class ScopedRouteInfo {
public:
  ScopedRouteInfo(envoy::config::route::v3::ScopedRouteConfiguration&& config_proto,
                  ConfigConstSharedPtr route_config);

  const ConfigConstSharedPtr& routeConfig() const { return route_config_; }
  const ScopeKey& scopeKey() const { return scope_key_; }
  const envoy::config::route::v3::ScopedRouteConfiguration& configProto() const {
    return config_proto_;
  }
  const std::string& scopeName() const { return config_proto_.name(); }
  uint64_t configHash() const { return config_hash_; }

private:
  const envoy::config::route::v3::ScopedRouteConfiguration config_proto_;
  ScopeKey scope_key_;
  const ConfigConstSharedPtr route_config_;
  const uint64_t config_hash_;
};
using ScopedRouteInfoConstSharedPtr = std::shared_ptr<const ScopedRouteInfo>;
// Ordered map for consistent config dumping.
using ScopedRouteMap = std::map<std::string, ScopedRouteInfoConstSharedPtr>;

/**
 * Each Envoy worker is assigned an instance of this type. When config updates are received,
 * addOrUpdateRoutingScope() and removeRoutingScope() are called to update the set of scoped routes.
 *
 * ConnectionManagerImpl::refreshCachedRoute() will call getRouterConfig() to obtain the
 * Router::ConfigConstSharedPtr to use for route selection.
 */
class ScopedConfigImpl : public ScopedConfig {
public:
  ScopedConfigImpl() = default;

  ScopedConfigImpl(const std::vector<ScopedRouteInfoConstSharedPtr>& scoped_route_infos) {
    addOrUpdateRoutingScopes(scoped_route_infos);
  }

  void
  addOrUpdateRoutingScopes(const std::vector<ScopedRouteInfoConstSharedPtr>& scoped_route_infos);

  void removeRoutingScopes(const std::vector<std::string>& scope_names);

  // Envoy::Router::ScopedConfig
  Router::ConfigConstSharedPtr getRouteConfig(const ScopeKeyPtr& scope_key) const override;

private:
  // From scope name to cached ScopedRouteInfo.
  absl::flat_hash_map<std::string, ScopedRouteInfoConstSharedPtr> scoped_route_info_by_name_;
  // Hash by ScopeKey hash to lookup in constant time.
  absl::flat_hash_map<uint64_t, ScopedRouteInfoConstSharedPtr> scoped_route_info_by_key_;
};

/**
 * A NULL implementation of the scoped routing configuration.
 */
class NullScopedConfigImpl : public ScopedConfig {
public:
  Router::ConfigConstSharedPtr getRouteConfig(const ScopeKeyPtr&) const override {
    return std::make_shared<const NullConfigImpl>();
  }
};

} // namespace Router
} // namespace Envoy
