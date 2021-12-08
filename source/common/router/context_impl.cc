#include "source/common/router/context_impl.h"

#include "source/common/config/utility.h"

namespace Envoy {
namespace Router {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : stat_names_(symbol_table), virtual_cluster_stat_names_(symbol_table),
      generic_conn_pool_factory_(Envoy::Config::Utility::getFactoryByName<GenericConnPoolFactory>(
          "envoy.filters.connection_pools.http.generic")) {}

} // namespace Router
} // namespace Envoy
