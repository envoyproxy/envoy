#pragma once

#include "server/hot_restarting_base.h"

namespace Envoy {
namespace Server {

/**
 * The child half of hot restarting. Issues requests and commands to the parent.
 */
class HotRestartingChild : HotRestartingBase {
public:
  HotRestartingChild(int base_id, int restart_epoch);

  int duplicateParentListenSocket(const std::string& address);
  void getParentStats(HotRestart::GetParentStatsInfo& info);
  void drainParentListeners();
  void shutdownParentAdmin(HotRestart::ShutdownParentAdminInfo& info);
  void terminateParent();

private:
  const int restart_epoch_;
  bool parent_terminated_{};
  sockaddr_un parent_address_;
};

} // namespace Server
} // namespace Envoy
