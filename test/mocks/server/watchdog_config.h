#pragma once

#include <chrono>

#include "envoy/server/configuration.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockWatchdog : public Watchdog {
public:
  MockWatchdog() : MockWatchdog(0, 0, 0, 0, 0.0, {}) {}
  MockWatchdog(int miss, int megamiss, int kill, int multikill, double multikill_threshold,
               const std::vector<std::string> action_protos);
  ~MockWatchdog() override = default;

  MOCK_METHOD(std::chrono::milliseconds, missTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, megaMissTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, killTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, multiKillTimeout, (), (const));
  MOCK_METHOD(double, multiKillThreshold, (), (const));
  MOCK_METHOD(Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>,
              actions, (), (const));

  std::chrono::milliseconds miss_;
  std::chrono::milliseconds megamiss_;
  std::chrono::milliseconds kill_;
  std::chrono::milliseconds multikill_;
  double multikill_threshold_;
  Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction> actions_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
