#pragma once

namespace Envoy {
/**
 * Hooks in the server to allow for integration testing. The real server just uses an empty
 * implementation defined below.
 */
class TestHooks {
public:
  virtual ~TestHooks() {}

  /**
   * Called when the server is initialized and about to start event loops.
   */
  virtual void onServerInitialized() PURE;
};

/**
 * Empty implementation of TestHooks.
 */
class DefaultTestHooks : public TestHooks {
public:
  // TestHooks
  void onServerInitialized() override {}
};
} // namespace Envoy
