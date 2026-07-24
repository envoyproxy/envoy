#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/filter_chain/v3/filter_chain.pb.h"
#include "envoy/http/filter.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

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

#define COMMON_FILTER_CHAIN_STATS(COUNTER) COUNTER(pass_through)

struct FilterChainStats {
  COMMON_FILTER_CHAIN_STATS(GENERATE_COUNTER_STRUCT)
};

using FilterFactory = Filter::FilterConfigProviderPtr<Filter::HttpFilterFactoryCb>;
using FilterFactoriesVector = std::vector<FilterFactory>;

/**
 * Configuration for a single filter chain.
 */
class FilterChain {
public:
  FilterChain(const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
              Server::Configuration::ServerFactoryContext& context, const std::string& stats_prefix,
              absl::Status& creation_status);
  FilterChain(const envoy::extensions::filters::http::filter_chain::v3::FilterChain& proto_config,
              Server::Configuration::FactoryContext& context, const std::string& stats_prefix,
              absl::Status& creation_status);

  absl::Span<const FilterFactory> filterFactories() const { return filter_factories_; }
  bool hasFilter(absl::string_view filter_name) const { return filters_.contains(filter_name); }

private:
  FilterFactoriesVector filter_factories_;
  absl::flat_hash_set<std::string> filters_;
};

using FilterChainConstSharedPtr = std::shared_ptr<const FilterChain>;

/**
 * Per-route configuration for the filter chain filter.
 */
class FilterChainPerRouteConfig : public Router::RouteSpecificFilterConfig {
public:
  FilterChainPerRouteConfig(const FilterChainConfigProtoPerRoute& proto_config,
                            Server::Configuration::ServerFactoryContext& context,
                            const std::string& stats_prefix, absl::Status& creation_status);

  OptRef<const FilterChain> filterChain() const { return makeOptRefFromPtr(filter_chain_.get()); }

private:
  FilterChainConstSharedPtr filter_chain_;
};

using FilterChainPerRouteConfigConstSharedPtr = std::shared_ptr<const FilterChainPerRouteConfig>;

/**
 * Filter-level configuration for the filter chain filter.
 */
class FilterChainConfig {
public:
  FilterChainConfig(const FilterChainConfigProto& proto_config,
                    Server::Configuration::FactoryContext& context, const std::string& stats_prefix,
                    absl::Status& creation_status);

  OptRef<const FilterChain> filterChain() const {
    return makeOptRefFromPtr(default_filter_chain_.get());
  }

  FilterChainStats& stats() { return stats_; }

private:
  static FilterChainStats createStats(const std::string& stats_prefix, Stats::Scope& scope) {
    const std::string final_prefix = fmt::format("{}filter_chain.", stats_prefix);
    return {COMMON_FILTER_CHAIN_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
  }

  FilterChainConstSharedPtr default_filter_chain_;
  FilterChainStats stats_;
};

using FilterChainConfigSharedPtr = std::shared_ptr<FilterChainConfig>;

} // namespace FilterChain
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
