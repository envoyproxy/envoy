#pragma once

#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Hooks in the server to allow for integration testing. The real server just uses an empty
 * implementation defined below.
 */
class ListenerHooks {
public:
  virtual ~ListenerHooks() = default;

  /**
   * Called when a worker has added a listener and it is listening.
   */
  virtual void onWorkerListenerAdded() PURE;

  /**
   * Called when a worker has removed a listener and it is no longer listening.
   */
  virtual void onWorkerListenerRemoved() PURE;

  /**
   * Called when the Runtime::ScopedLoaderSingleton is created by the server.
   */
  virtual void onRuntimeCreated() PURE;
};

/**
 * Empty implementation of ListenerHooks.
 */
class DefaultListenerHooks : public ListenerHooks {
public:
  // ListenerHooks
  void onWorkerListenerAdded() override {}
  void onWorkerListenerRemoved() override {}
  void onRuntimeCreated() override {}
};

} // namespace Envoy
