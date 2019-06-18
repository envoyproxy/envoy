#pragma once

#include "common/stats/stat_merger.h"

#include "server/hot_restarting_base.h"

namespace Envoy {
namespace Server {

/**
 * The child half of hot restarting. Issues requests and commands to the parent.
 */
class HotRestartingChild : HotRestartingBase, Logger::Loggable<Logger::Id::main> {
public:
  HotRestartingChild(int base_id, int restart_epoch);

  int duplicateParentListenSocket(const std::string& address);
  std::unique_ptr<envoy::HotRestartMessage> getParentStats();
  void drainParentListeners();
  void sendParentAdminShutdownRequest(time_t& original_start_time);
  void sendParentTerminateRequest();
  void mergeParentStats(Stats::Store& stats_store,
                        const envoy::HotRestartMessage::Reply::Stats& stats_proto);

private:
  const int restart_epoch_;
  bool parent_terminated_{};
  sockaddr_un parent_address_;
  std::unique_ptr<Stats::StatMerger> stat_merger_{};
};

} // namespace Server
} // namespace Envoy
