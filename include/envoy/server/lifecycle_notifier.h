#pragma once

#include <functional>

namespace Envoy {
namespace Server {

class ServerLifecycleNotifier {
public:
  virtual ~ServerLifecycleNotifier() {}

  /**
   * Stages of the envoy server instance lifecycle.
   */
  enum class Stage {
    /**
     * The server instance main thread has entered the dispatcher loop.
     */
    Startup,

    /**
     * The server instance is being shutdown and the dispatcher is about the exit.
     */
    ShutdownExit
  };

  /**
   * Callback invoked when the server reaches a certain lifecycle stage.
   */
  using StageCallback = std::function<void()>;

  /**
   * Register a callback function that will be invoked on the main thread when
   * the specified stage is reached.
   */
  virtual void registerCallback(Stage stage, StageCallback callback) PURE;
};

} // namespace Server
} // namespace Envoy
