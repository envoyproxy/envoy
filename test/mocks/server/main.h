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
  MockMain() = default;
  ~MockMain() override = default;

  MOCK_METHOD(Upstream::ClusterManager*, clusterManager, ());
  MOCK_METHOD(std::list<Stats::SinkPtr>&, statsSinks, ());
  MOCK_METHOD(std::chrono::milliseconds, statsFlushInterval, (), (const));
  MOCK_METHOD(const Watchdog&, mainThreadWatchdogConfig, (), (const));
  MOCK_METHOD(const Watchdog&, workerWatchdogConfig, (), (const));
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
