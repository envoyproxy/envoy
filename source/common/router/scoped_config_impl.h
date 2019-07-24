#pragma once

#include <typeinfo>

#include "envoy/api/v2/srds.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/router/router.h"
#include "envoy/router/scopes.h"
#include "envoy/thread_local/thread_local.h"

#include "common/protobuf/utility.h"
#include "common/router/config_impl.h"
#include "common/router/scoped_config_manager.h"

namespace Envoy {
namespace Router {

using envoy::config::filter::network::http_connection_manager::v2::ScopedRoutes;

/**
 * Scope key fragment base class.
 */
class ScopeKeyFragmentBase {
public:
  bool operator!=(const ScopeKeyFragmentBase& other) const { return !(*this == other); }

  bool operator==(const ScopeKeyFragmentBase& other) const {
    if (typeid(*this) == typeid(other)) {
      return equals(other);
    }
    return false;
  }
  virtual ~ScopeKeyFragmentBase() = default;

private:
  // Returns true if the two fragments equal else false.
  virtual bool equals(const ScopeKeyFragmentBase&) const PURE;
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
    fragments_.emplace_back(std::move(fragment));
  }

  bool operator!=(const ScopeKey& other) const;

  bool operator==(const ScopeKey& other) const;

private:
  std::vector<std::unique_ptr<ScopeKeyFragmentBase>> fragments_;
};

// String fragment.
class StringKeyFragment : public ScopeKeyFragmentBase {
public:
  explicit StringKeyFragment(absl::string_view value) : value_(value) {}

private:
  bool equals(const ScopeKeyFragmentBase& other) const override {
    return value_ == static_cast<const StringKeyFragment&>(other).value_;
  }

  const std::string value_;
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
  virtual std::unique_ptr<ScopeKey> computeScopeKey(const Http::HeaderMap& headers) const PURE;

protected:
  const ScopedRoutes::ScopeKeyBuilder config_;
};

class ScopeKeyBuilderImpl : public ScopeKeyBuilderBase {
public:
  explicit ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder&& config);

  std::unique_ptr<ScopeKey> computeScopeKey(const Http::HeaderMap& headers) const override;

private:
  std::vector<std::unique_ptr<FragmentBuilderBase>> fragment_builders_;
};

/**
 * TODO(AndresGuedez): implement scoped routing logic.
 *
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
