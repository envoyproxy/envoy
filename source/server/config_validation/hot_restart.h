#pragma once

#include <cstring>

#include "envoy/server/hot_restart.h"

#include "common/common/thread.h"

namespace Envoy {
namespace Server {

/**
 * Implementation of HotRestart that does not perform a hot restart. Used for config validation
 * runs, where we don't want to disturb another running instance.
 */
class ValidationHotRestart : public HotRestart {
public:
  // Server::HotRestart
  void drainParentListeners() {}
  int duplicateParentListenSocket(const std::string&) { return -1; }
  void getParentStats(GetParentStatsInfo& info) { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) {}
  void terminateParent() {}
  void shutdown() {}
  std::string version() { return ""; }
};

} // Server
} // Envoy
