#pragma once

#include <memory>
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

#define COMMON_FILTER_CHAIN_STATS(COUNTER)                                                         \
  COUNTER(no_route)                                                                                \
  COUNTER(no_route_filter_config)                                                                  \
  COUNTER(use_route_filter_chain)                                                                  \
  COUNTER(use_default_filter_chain)                                                                \
  COUNTER(pass_through)

struct FilterChainStats {
  COMMON_FILTER_CHAIN_STATS(GENERATE_COUNTER_STRUCT)
};

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
                            const std::string& stats_prefix, absl::Status& creation_status);

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
