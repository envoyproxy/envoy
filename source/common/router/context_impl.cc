#include "common/router/context_impl.h"

namespace Envoy {
namespace Router {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table) : stat_names_(symbol_table) {}

} // namespace Router
} // namespace Envoy
