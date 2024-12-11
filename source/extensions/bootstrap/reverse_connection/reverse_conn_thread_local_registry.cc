#include "source/extensions/bootstrap/reverse_connection/reverse_conn_thread_local_registry.h"

#include "source/extensions/bootstrap/reverse_connection/reverse_connection_handler_impl.h"
#include "source/extensions/bootstrap/reverse_connection/reverse_connection_manager_impl.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

RCThreadLocalRegistry::RCThreadLocalRegistry(Event::Dispatcher& dispatcher, Stats::Scope& scope,
                                             std::string stat_prefix) {
  ENVOY_LOG_MISC(debug, "RCThreadLocalRegistry: creating RCManager and RCHandler for worker: {}",
                 dispatcher.name());
  rc_manager_ = std::make_shared<ReverseConnectionManagerImpl>(dispatcher);
  rc_handler_ = std::make_shared<ReverseConnectionHandlerImpl>(&dispatcher);
  // Pass a root scope, i.e., "reverse_connection" so that RCManager and RCHandler stats are
  // organized under the same scope.
  Stats::ScopeSharedPtr root_scope = scope.createScope(stat_prefix);
  rc_manager_->initializeStats(*root_scope);
  rc_handler_->initializeStats(*root_scope);
}

Network::ReverseConnectionManager& RCThreadLocalRegistry::getRCManager() { return *rc_manager_; }

Network::ReverseConnectionHandler& RCThreadLocalRegistry::getRCHandler() { return *rc_handler_; }
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
