#pragma once

#include <string>

#include "envoy/server/hot_restart.h"

#include "common/common/thread.h"
#include "common/stats/heap_stat_data.h"

namespace Envoy {
namespace Server {

/**
 * No-op implementation of HotRestart.
 */
class HotRestartNopImpl : public Server::HotRestart {
public:
  HotRestartNopImpl() {}

  // Server::HotRestart
  void drainParentListeners() override {}
  int duplicateParentListenSocket(const std::string&) override { return -1; }
  void getParentStats(GetParentStatsInfo& info) override { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) override {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) override {}
  void terminateParent() override {}
  void shutdown() override {}
  std::string version() override { return "disabled"; }
  Thread::BasicLockable& logLock() override { return log_lock_; }
  Thread::BasicLockable& accessLogLock() override { return access_log_lock_; }
  Stats::StatDataAllocator& statsAllocator() override { return stats_allocator_; }

private:
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable access_log_lock_;
  Stats::HeapStatDataAllocator stats_allocator_;
};

} // namespace Server
} // namespace Envoy
