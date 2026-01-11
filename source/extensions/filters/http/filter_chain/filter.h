#pragma once

#include <memory>
#include <vector>

#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/http/filter.h"

#include "source/common/http/filter_chain_helper.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FilterChain {

using FilterChainConfigProto =
    envoy::extensions::filters::http::filter_chain::v3::FilterChainConfig;
using FilterChainConfigProtoPerRoute =
    envoy::extensions::filters::http::filter_chain::v3::FilterChainConfigPerRoute;

/**
 * Configuration for a single filter chain.
 */
class FilterChain {
public:
  FilterChain(const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
              Server::Configuration::ServerFactoryContext& context,
              const std::string& stats_prefix);
  FilterChain(const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
              Server::Configuration::FactoryContext& context, const std::string& stats_prefix);

  const Http::FilterChainUtility::FilterFactoriesList& filterFactories() const {
    return filter_factories_;
  }

private:
  Http::FilterChainUtility::FilterFactoriesList filter_factories_;
};

using FilterChainConstSharedPtr = std::shared_ptr<const FilterChain>;

/**
 * Per-route configuration for the filter chain filter.
 */
class FilterChainPerRouteConfig : public Router::RouteSpecificFilterConfig {
public:
  FilterChainPerRouteConfig(const FilterChainConfigProtoPerRoute& proto_config,
                            Server::Configuration::ServerFactoryContext& context,
                            const std::string& stats_prefix);

  OptRef<const FilterChain> filterChain() const { return makeOptRefFromPtr(filter_chain_.get()); }
  absl::string_view filterChainName() const { return filter_chain_name_; }

private:
  FilterChainConstSharedPtr filter_chain_;
  std::string filter_chain_name_;
};

using FilterChainPerRouteConfigConstSharedPtr = std::shared_ptr<const FilterChainPerRouteConfig>;

/**
 * Filter-level configuration for the filter chain filter.
 */
class FilterChainConfig {
public:
  FilterChainConfig(const FilterChainConfigProto& proto_config,
                    Server::Configuration::FactoryContext& context,
                    const std::string& stats_prefix);

  OptRef<const FilterChain> filterChain(absl::string_view name) const {
    if (auto it = named_filter_chains_.find(name); it != named_filter_chains_.end()) {
      return makeOptRefFromPtr(it->second.get());
    }
    return makeOptRefFromPtr(default_filter_chain_.get());
  }

private:
  FilterChainConstSharedPtr default_filter_chain_;
  absl::flat_hash_map<std::string, FilterChainConstSharedPtr> named_filter_chains_;
};

using FilterChainConfigSharedPtr = std::shared_ptr<FilterChainConfig>;

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
