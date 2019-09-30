#pragma once

#include "envoy/config/filter/http/ext_authz/v2/ext_authz.pb.h"
#include "envoy/config/filter/http/ext_authz/v2/ext_authz.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExtAuthz {

/**
 * All stats for the Ext Authz filter. @see stats_macros.h
 */

// clang-format off
#define ALL_EXT_AUTHZ_FILTER_STATS(COUNTER)
  COUNTER(ok)              \
  COUNTER(denied)					 \
  COUNTER(error)           \
// clang-format on

/**
 * Wrapper struct for extz_auth filter stats. @see stats_macros.h
 */
struct ExtAuthzFilterStats {
	ALL_EXT_AUTHZ_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};


/**
 * Config registration for the external authorization filter. @see NamedHttpFilterConfigFactory.
 */
class ExtAuthzFilterConfig
    : public Common::FactoryBase<envoy::config::filter::http::ext_authz::v2::ExtAuthz,
                                 envoy::config::filter::http::ext_authz::v2::ExtAuthzPerRoute> {
public:
  ExtAuthzFilterConfig() : FactoryBase(HttpFilterNames::get().ExtAuthorization),
  //todo fix this
  stats_(generateStats("TBD", nullptr)) {}
	ExtAuthzFilterStats& stats() { return stats_; }

private:
  static constexpr uint64_t DefaultTimeout = 200;
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::http::ext_authz::v2::ExtAuthz& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::config::filter::http::ext_authz::v2::ExtAuthzPerRoute& proto_config,
      Server::Configuration::FactoryContext& context) override;

	ExtAuthzFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
		const std::string final_prefix = prefix + "ext_authz.";
		return {ALL_EXT_AUTHZ_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
	}

	// The stats for the filter.
	ExtAuthzFilterStats stats_;
};

} // namespace ExtAuthz
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
