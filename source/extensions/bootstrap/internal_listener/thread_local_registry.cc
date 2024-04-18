#include "source/extensions/bootstrap/internal_listener/thread_local_registry.h"

#include "source/extensions/bootstrap/internal_listener/active_internal_listener.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

Network::InternalListenerPtr
ThreadLocalRegistryImpl::createActiveInternalListener(Network::ConnectionHandler& conn_handler,
                                                      Network::ListenerConfig& config,
                                                      Event::Dispatcher& dispatcher) {
  return std::make_unique<ActiveInternalListener>(conn_handler, dispatcher, config);
}

} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
