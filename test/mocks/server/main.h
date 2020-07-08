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
  MockMain() : MockMain(0, 0, 0, 0) {}
  MockMain(int wd_miss, int wd_megamiss, int wd_kill, int wd_multikill);
  ~MockMain() override;

  MOCK_METHOD(Upstream::ClusterManager*, clusterManager, ());
  MOCK_METHOD(std::list<Stats::SinkPtr>&, statsSinks, ());
  MOCK_METHOD(std::chrono::milliseconds, statsFlushInterval, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMissTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMegaMissTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdKillTimeout, (), (const));
  MOCK_METHOD(std::chrono::milliseconds, wdMultiKillTimeout, (), (const));

  std::chrono::milliseconds wd_miss_;
  std::chrono::milliseconds wd_megamiss_;
  std::chrono::milliseconds wd_kill_;
  std::chrono::milliseconds wd_multikill_;
};
} // namespace Configuration
} // namespace Server
} // namespace Envoy
