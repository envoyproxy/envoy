#include "test_engine_builder.h"

#include "absl/synchronization/notification.h"

namespace Envoy {

Platform::EngineSharedPtr
TestEngineBuilder::createEngine(std::unique_ptr<envoy::config::bootstrap::v3::Bootstrap> config) {
  absl::Notification engine_running;
  setOverrideConfig(std::move(config));
  Platform::EngineSharedPtr engine = addLogLevel(Platform::LogLevel::debug)
                                         .setOnEngineRunning([&]() { engine_running.Notify(); })
                                         .build();
  engine_running.WaitForNotification();
  return engine;
}

} // namespace Envoy
