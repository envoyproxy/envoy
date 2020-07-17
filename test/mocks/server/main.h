#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <string>

#include "envoy/server/configuration.h"
#include "envoy/server/overload_manager.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Server {
namespace Configuration {
class MockMain : public Main {
public:
  // TODO(kbaichoo): modify to inject WD actions.
  MockMain() : MockMain(0, 0, 0, 0, 0.0, {}) {}
  MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill,
           double wd_multikill_threshold,
           const std::vector<std::string> wd_action_protos);
  ~MockMain() override;

  MOCK_METHOD(Upstream::ClusterManager*, clusterManager, ());
  MOCK_METHOD(std::list<Stats::SinkPtr>&, statsSinks, ());
  MOCK_METHOD(std::chrono::milliseconds, statsFlushInterval, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMissTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMegaMissTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdKillTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMultiKillTimeout, (), (const));
  MOCK_METHOD(double, wdMultiKillThreshold, (), (const));
  MOCK_METHOD(Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction>,
              wdActions, (), (const));

  std::chrono::milliseconds wd_miss_;
  std::chrono::milliseconds wd_megamiss_;
  std::chrono::milliseconds wd_kill_;
  std::chrono::milliseconds wd_multikill_;
  double wd_multikill_threshold_;
  Protobuf::RepeatedPtrField<envoy::config::bootstrap::v3::Watchdog::WatchdogAction> wd_actions_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
