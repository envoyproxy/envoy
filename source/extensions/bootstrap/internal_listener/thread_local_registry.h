#pragma once

#include "envoy/network/connection_handler.h"
#include "envoy/thread_local/thread_local_object.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace InternalListener {

// The registry is constructed and accessed on each silo.
class ThreadLocalRegistryImpl : public ThreadLocal::ThreadLocalObject,
                                public Network::LocalInternalListenerRegistry {
public:
  ThreadLocalRegistryImpl() = default;

  // Network::LocalInternalListenerRegistry
  void
  setInternalListenerManager(Network::InternalListenerManager& internal_listener_manager) override {
    manager_ = &internal_listener_manager;
  }

  Network::InternalListenerManagerOptRef getInternalListenerManager() override {
    if (manager_ == nullptr) {
      // The internal listener manager is published to this registry when the first internal
      // listener is added through LDS. Return null prior to this moment.
      return {};
    }
    return {*manager_};
  }

  Network::InternalListenerPtr
  createActiveInternalListener(Network::ConnectionHandler& conn_handler,
                               Network::ListenerConfig& config,
                               Event::Dispatcher& dispatcher) override;

private:
  // The typical instance is the ``ConnectionHandlerImpl`` on the same thread.
  Network::InternalListenerManager* manager_{nullptr};
};
} // namespace InternalListener
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
