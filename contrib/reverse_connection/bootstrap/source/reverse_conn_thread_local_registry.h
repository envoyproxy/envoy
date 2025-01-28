#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/thread_local/thread_local_object.h"


namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class ReverseConnectionManager;
class ReverseConnectionHandler;

class RCThreadLocalRegistry : public ThreadLocal::ThreadLocalObject,
                              public Network::LocalRevConnRegistry,
                              public Logger::Loggable<Logger::Id::main> {
public:
  RCThreadLocalRegistry(Event::Dispatcher& dispatcher, Stats::Scope& scope,
                        std::string stat_prefix, Upstream::ClusterManager& cluster_manager);
  ~RCThreadLocalRegistry() = default;

  Network::ReverseConnectionListenerPtr createActiveReverseConnectionListener(Network::ConnectionHandler& conn_handler,
                                                      Event::Dispatcher& dispatcher,
                                                      Network::ListenerConfig& config) override;

  ReverseConnectionManager& getRCManager();
  ReverseConnectionHandler& getRCHandler();
  

private:
  std::shared_ptr<ReverseConnectionManager> rc_manager_;
  std::shared_ptr<ReverseConnectionHandler> rc_handler_;
};
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
