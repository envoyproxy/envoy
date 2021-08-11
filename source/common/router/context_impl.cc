#include "source/common/router/context_impl.h"

namespace Envoy {
namespace Router {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : stat_names_(symbol_table), virtual_cluster_stat_names_(symbol_table) {}

} // namespace Router
} // namespace Envoy
