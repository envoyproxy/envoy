#include "source/common/router/context_impl.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Router {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : stat_names_(symbol_table), route_stat_names_(symbol_table),
      virtual_cluster_stat_names_(symbol_table),
      generic_conn_pool_factory_(Envoy::Config::Utility::getFactoryByName<GenericConnPoolFactory>(
          "envoy.filters.connection_pools.http.generic")) {}

RouteStatsContextImpl::RouteStatsContextImpl(Stats::Scope& scope,
                                             const RouteStatNames& route_stat_names,
                                             const Stats::StatName& vhost_stat_name,
                                             const std::string& stat_prefix)
    : route_stat_name_storage_(stat_prefix, scope.symbolTable()),
      route_stats_scope_(Stats::Utility::scopeFromStatNames(
          scope, {route_stat_names.vhost_, vhost_stat_name, route_stat_names.route_,
                  route_stat_name_storage_.statName()})),
      route_stat_name_(route_stat_name_storage_.statName()),
      route_stats_(route_stat_names, *route_stats_scope_) {}

} // namespace Router
} // namespace Envoy
