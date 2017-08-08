#pragma once

#include <string>

#include "envoy/server/hot_restart.h"

namespace Envoy {
namespace Server {

/**
 * No-op implementation of HotRestart.
 */
class HotRestartNopImpl : public Server::HotRestart {
public:
  HotRestartNopImpl(){};

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
