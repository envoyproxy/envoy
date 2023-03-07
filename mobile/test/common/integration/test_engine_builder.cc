#include "test_engine_builder.h"

#include "absl/synchronization/notification.h"

namespace Envoy {

Platform::EngineSharedPtr TestEngineBuilder::createEngine(std::string config) {
  absl::Notification engine_running;
  Platform::EngineSharedPtr engine = setOverrideConfigForTests(std::move(config))
                                         .addLogLevel(Platform::LogLevel::debug)
                                         .setOnEngineRunning([&]() { engine_running.Notify(); })
                                         .build();
  engine_running.WaitForNotification();
  return engine;
}

void TestEngineBuilder::setOverrideConfig(std::string config) {
  setOverrideConfigForTests(std::move(config));
}

} // namespace Envoy
