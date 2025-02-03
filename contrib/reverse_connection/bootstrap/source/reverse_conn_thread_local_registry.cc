#include "contrib/reverse_connection/bootstrap/source/reverse_conn_thread_local_registry.h"
#include "contrib/reverse_connection/reverse_connection_listener_config/source/active_reverse_connection_listener.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_handler.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager_impl.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_initiator.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

RCThreadLocalRegistry::RCThreadLocalRegistry(Event::Dispatcher& dispatcher, Stats::Scope& scope,
                                             std::string stat_prefix, Upstream::ClusterManager& cluster_manager) {
  ENVOY_LOG_MISC(debug, "RCThreadLocalRegistry: creating RCManager and RCHandler for worker: {}",
                 dispatcher.name());
  rc_manager_ = std::make_shared<ReverseConnectionManagerImpl>(dispatcher, cluster_manager);
  rc_handler_ = std::make_shared<ReverseConnectionHandler>(&dispatcher);
  // Pass a root scope, i.e., "reverse_connection" so that RCManager and RCHandler stats are
  // organized under the same scope.
  Stats::ScopeSharedPtr root_scope = scope.createScope(stat_prefix);
  rc_manager_->initializeStats(*root_scope);
  rc_handler_->initializeStats(*root_scope);
}

Network::ReverseConnectionListenerPtr RCThreadLocalRegistry::createActiveReverseConnectionListener(Network::ConnectionHandler& conn_handler,
                                                      Event::Dispatcher& dispatcher,
                                                      Network::ListenerConfig& config) {
  return std::make_unique<Envoy::Extensions::ReverseConnection::ActiveReverseConnectionListener>(conn_handler, dispatcher, config, *this);
}

ReverseConnectionManager& RCThreadLocalRegistry::getRCManager() {
  ASSERT(rc_manager_ != nullptr, "ReverseConnectionManager is null");
  return *rc_manager_; 
}

ReverseConnectionHandler& RCThreadLocalRegistry::getRCHandler() {
  ASSERT(rc_handler_ != nullptr, "ReverseConnectionHandler is null");
  return *rc_handler_; 
}
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
