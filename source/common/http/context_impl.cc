#include "common/http/context_impl.h"

namespace Envoy {
namespace Http {

ContextImpl::ContextImpl(Stats::SymbolTable& symbol_table)
    : code_stats_(symbol_table), user_agent_context_(symbol_table),
      async_client_stat_prefix_("http.async-client", symbol_table) {}

} // namespace Http
} // namespace Envoy
