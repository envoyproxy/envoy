#pragma once

#include "envoy/common/pure.h"

namespace Envoy {

/**
 * Hooks in the server to allow for integration testing. The real server just uses an empty
 * implementation defined below.
 */
class TestHooks {
public:
  virtual ~TestHooks() {}

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
 * Empty implementation of TestHooks.
 */
class DefaultTestHooks : public TestHooks {
public:
  // TestHooks
  void onWorkerListenerAdded() override {}
  void onWorkerListenerRemoved() override {}
  void onRuntimeCreated() override {}
};

} // namespace Envoy
