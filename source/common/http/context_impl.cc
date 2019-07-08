#include "common/http/context_impl.h"

namespace Envoy {
namespace Http {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : tracer_(&null_tracer_), code_stats_(symbol_table) {}

} // namespace Http
} // namespace Envoy
