#include "source/common/router/context_impl.h"

#include "source/common/config/utility.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stats/utility.h"
#include "source/common/stats/well_known_stat_names.h"

namespace Envoy {
namespace Router {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : stat_names_(symbol_table), route_stat_names_(symbol_table),
      virtual_cluster_stat_names_(symbol_table),
      generic_conn_pool_factory_(Envoy::Config::Utility::getFactoryByName<GenericConnPoolFactory>(
          "envoy.filters.connection_pools.http.generic")) {}

namespace {

// vhost.[<virtual host name>.]route.(<route_stat_prefix>.)*: the virtual host and the route stat
// prefix are tags of the stats, attached by a stack-local helper so a route needs no dedicated
// sub-scope. The names are joined from the pre-encoded StatNames rather than passed through the
// string-based APIs, which sanitize their input and would silently rename the stats of names
// containing ':'.
RouteStats createRouteStats(Stats::Scope& scope, const RouteStatNames& stat_names,
                            Stats::StatName vhost_stat_name, Stats::StatName route_stat_name) {
  Stats::SymbolTable& symbol_table = scope.symbolTable();
  const Stats::WellKnownStatNames& well_known = symbol_table.wellKnownStatNames();
  const Stats::StatNameTag tags[] = {{well_known.tag_virtual_host_, vhost_stat_name},
                                     {well_known.tag_route_, route_stat_name}};
  const Stats::StatNameJoiner base_prefix({well_known.vhost_, well_known.route_}, symbol_table);
  const Stats::StatNameJoiner prefix(
      {well_known.vhost_, vhost_stat_name, well_known.route_, route_stat_name}, symbol_table);
  Stats::LiteScopeHelper helper(scope, base_prefix.statName(), tags, prefix.statName());
  return {stat_names, helper};
}

} // namespace

RouteStatsContextImpl::RouteStatsContextImpl(Stats::ScopeSharedPtr scope,
                                             const RouteStatNames& route_stat_names,
                                             const Stats::StatName& vhost_stat_name,
                                             const std::string& stat_prefix)
    : route_stat_name_storage_(stat_prefix, scope->symbolTable()), scope_(std::move(scope)),
      route_stat_name_(route_stat_name_storage_.statName()),
      route_stats_(createRouteStats(*scope_, route_stat_names, vhost_stat_name, route_stat_name_)) {
}

} // namespace Router
} // namespace Envoy
