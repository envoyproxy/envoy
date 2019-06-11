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

class ScopeKey {
public:
  ScopeKey() = default;
  ScopeKey(ScopeKey&& other) = default;
  void addFragment(std::unique_ptr<ScopeKeyFragmentBase>&& part) {
    contains_null_fragment_ = contains_null_fragment_ || (part == nullptr);
    fragments_.emplace_back(std::move(part));
  }

  bool operator!=(const ScopeKey& other) const { return !(*this == other); }

  bool operator==(const ScopeKey& other) const {
    if (this->fragments_.size() != other.fragments_.size()) {
      return false;
    }
    if (this->fragments_.size() == 0 || other.fragments_.size() == 0) {
      // An empty key equals to nothing, "NULL" != "NULL".
      return false;
    }
    // A "NULL" fragment value equals to nothing.
    if (this->contains_null_fragment_ || other.contains_null_fragment_) {
      return false;
    }
    return std::equal(this->fragments_.begin(), this->fragments_.end(), other.fragments_.begin(),
                      [](const std::unique_ptr<ScopeKeyFragmentBase>& left,
                         const std::unique_ptr<ScopeKeyFragmentBase>& right) -> bool {
                        // Both should be non-NULL now.
                        return *left == *right;
                      });
  }

private:
  bool contains_null_fragment_{false};
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

  std::string value_;
};

/**
 * Base class for fragment builders.
 */
class FragmentBuilderBase {
public:
  explicit FragmentBuilderBase(ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config)
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
  explicit HeaderValueExtractorImpl(ScopedRoutes::ScopeKeyBuilder::FragmentBuilder config)
      : FragmentBuilderBase(config),
        header_value_extractor_config_(config_.header_value_extractor()) {
    ASSERT(config_.type_case() ==
               ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::kHeaderValueExtractor,
           "header_value_extractor is not set.");
    if (header_value_extractor_config_.extract_type_case() ==
        ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::kIndex) {
      if (header_value_extractor_config_.index() != 0 &&
          header_value_extractor_config_.element_separator().length() == 0) {
        throw ProtoValidationException("when element separator is set to an empty string, index "
                                       "should be set to 0 in HeaderValueExtractor.",
                                       header_value_extractor_config_);
      }
    }
    if (header_value_extractor_config_.extract_type_case() ==
        ScopedRoutes::ScopeKeyBuilder::FragmentBuilder::HeaderValueExtractor::
            EXTRACT_TYPE_NOT_SET) {
      throw ProtoValidationException("HeaderValueExtractor extract_type not set.",
                                     header_value_extractor_config_);
    }
  }

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
  explicit ScopeKeyBuilderBase(ScopedRoutes::ScopeKeyBuilder config) : config_(std::move(config)) {}
  virtual ~ScopeKeyBuilderBase() = default;

  virtual const ScopeKey computeScopeKey(const Http::HeaderMap& headers) const PURE;

protected:
  ScopedRoutes::ScopeKeyBuilder config_;
};

class ScopeKeyBuilderImpl : public ScopeKeyBuilderBase {
public:
  explicit ScopeKeyBuilderImpl(ScopedRoutes::ScopeKeyBuilder config);

  const ScopeKey computeScopeKey(const Http::HeaderMap& headers) const override;

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
