#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"

#include "common/common/assert.h"
#include "common/stats/stats_impl.h"


namespace Envoy {
namespace Server {

/**
 * No-op implementation of HotRestart.
 */
class HotRestartNopImpl : public HotRestart {
public:
  HotRestartNopImpl() { };

  void drainParentListeners() override {}
  int duplicateParentListenSocket(const std::string&) override { return -1; }
  void getParentStats(GetParentStatsInfo& info) override { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) override {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) override {}
  void terminateParent() override {}
  void shutdown() override {}
  std::string version() override { return "disabled"; }
};

} // namespace Server
} // namespace Envoy
