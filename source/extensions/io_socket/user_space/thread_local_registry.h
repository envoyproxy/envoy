#pragma once

#include "envoy/network/listener.h"
#include "envoy/thread_local/thread_local_object.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

class ThreadLocalRegistryImpl : public ThreadLocal::ThreadLocalObject,
                                public Network::LocalInternalListenerRegistry {
public:
  ThreadLocalRegistryImpl() = default;

  void
  setInternalListenerManager(Network::InternalListenerManager& internal_listener_manager) override {
    manager_ = &internal_listener_manager;
  }

  Network::InternalListenerManagerOptRef getInternalListenerManager() override {
    if (manager_ == nullptr) {
      return Network::InternalListenerManagerOptRef();
    }
    return Network::InternalListenerManagerOptRef(*manager_);
  }

private:
  Network::InternalListenerManager* manager_{nullptr};
};
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
