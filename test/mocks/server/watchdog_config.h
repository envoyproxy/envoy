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

  MOCK_METHOD(std::chrono::milliseconds, missTimeout, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, megaMissTimeout, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, killTimeout, (), (const, override));
  MOCK_METHOD(std::chrono::milliseconds, multiKillTimeout, (), (const, override));
  MOCK_METHOD(double, multiKillThreshold, (), (const, override));
  MOCK_METHOD(Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>,
              actions, (), (const, override));

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
