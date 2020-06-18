#pragma once

#include <memory>
#include <typeinfo>

#include "envoy/config/route/v3/scoped_route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/router/rds.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/hash.h"
#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"

#include "absl/numeric/int128.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Router {

using envoy::extensions::filters::network::http_connection_manager::v3::ScopedRoutes;

/**
 * Scope key fragment base class.
 */
class ScopeKeyFragmentBase {
public:
  bool operator!=(const ScopeKeyFragmentBase& other) const { return !(*this == other); }

  bool operator==(const ScopeKeyFragmentBase& other) const {
    if (typeid(*this) == typeid(other)) {
      return hash() == other.hash();
    }
    return false;
  }
  virtual ~ScopeKeyFragmentBase() = default;

  // Hash of the fragment.
  virtual uint64_t hash() const PURE;
};

/**
 *  Scope Key is composed of non-null fragments.
 **/
class ScopeKey {
public:
  ScopeKey() = default;
  ScopeKey(ScopeKey&& other) = default;

  // Scopekey is not copy-assignable and copy-constructible as it contains unique_ptr inside itself.
  ScopeKey(const ScopeKey&) = delete;
  ScopeKey operator=(const ScopeKey&) = delete;

  // Caller should guarantee the fragment is not nullptr.
  void addFragment(std::unique_ptr<ScopeKeyFragmentBase>&& fragment) {
    ASSERT(fragment != nullptr, "null fragment not allowed in ScopeKey.");
    updateHash(*fragment);
    fragments_.emplace_back(std::move(fragment));
  }

  uint64_t hash() const { return hash_; }
  bool operator!=(const ScopeKey& other) const;
  bool operator==(const ScopeKey& other) const;

private:
  // Update the key's hash with the new fragment hash.
  void updateHash(const ScopeKeyFragmentBase& fragment) {
    std::stringbuf buffer;
    buffer.sputn(reinterpret_cast<const char*>(&hash_), sizeof(hash_));
    const auto& fragment_hash = fragment.hash();
    buffer.sputn(reinterpret_cast<const char*>(&fragment_hash), sizeof(fragment_hash));
    hash_ = HashUtil::xxHash64(buffer.str());
  }

  uint64_t hash_{0};
  std::vector<std::unique_ptr<ScopeKeyFragmentBase>> fragments_;
};

using ScopeKeyPtr = std::unique_ptr<ScopeKey>;

// String fragment.
class StringKeyFragment : public ScopeKeyFragmentBase {
public:
  explicit StringKeyFragment(absl::string_view value)
      : value_(value), hash_(HashUtil::xxHash64(value_)) {}

  uint64_t hash() const override { return hash_; }

private:
  const std::string value_;
  const uint64_t hash_;
};

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
class ScopeKeyBuilderBase {
public:
  explicit ScopeKeyBuilderBase(ScopedRoutes::ScopeKeyBuilder&& config)
      : config_(std::move(config)) {}
  virtual ~ScopeKeyBuilderBase() = default;

  // Computes scope key for given headers, returns nullptr if a key can't be computed.
  virtual ScopeKeyPtr computeScopeKey(const Http::HeaderMap& headers) const PURE;

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
                  ConfigConstSharedPtr&& route_config);

  const ConfigConstSharedPtr& routeConfig() const { return route_config_; }
  const ScopeKey& scopeKey() const { return scope_key_; }
  const envoy::config::route::v3::ScopedRouteConfiguration& configProto() const {
    return config_proto_;
  }
  const std::string& scopeName() const { return config_proto_.name(); }

private:
  envoy::config::route::v3::ScopedRouteConfiguration config_proto_;
  ScopeKey scope_key_;
  ConfigConstSharedPtr route_config_;
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
  ScopedConfigImpl(ScopedRoutes::ScopeKeyBuilder&& scope_key_builder)
      : scope_key_builder_(std::move(scope_key_builder)) {}

  void addOrUpdateRoutingScope(const ScopedRouteInfoConstSharedPtr& scoped_route_info);
  void removeRoutingScope(const std::string& scope_name);

  // Envoy::Router::ScopedConfig
  Router::ConfigConstSharedPtr getRouteConfig(const Http::HeaderMap& headers) const override;

private:
  ScopeKeyBuilderImpl scope_key_builder_;
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
  Router::ConfigConstSharedPtr getRouteConfig(const Http::HeaderMap&) const override {
    return std::make_shared<const NullConfigImpl>();
  }
};

} // namespace Router
} // namespace Envoy
