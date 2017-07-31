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

#include "exe/shared_memory.h"

namespace Envoy {
namespace Server {

/**
 * No-op implementation of HotRestart.
 */
class HotRestartImpl : public HotRestart, public Stats::RawStatDataAllocator {
public:
  HotRestartImpl(Options& options);

  Thread::BasicLockable& logLock() { return log_lock_; }
  Thread::BasicLockable& accessLogLock() { return access_log_lock_; }

  // Server::HotRestart
  void drainParentListeners() override {}
  int duplicateParentListenSocket(const std::string&) override { return -1; }
  void getParentStats(GetParentStatsInfo& info) override { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) override {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) override {}
  void terminateParent() override {}
  void shutdown() override {}
  std::string version() override { return "disabled"; }

  // RawStatDataAllocator
  Stats::RawStatData* alloc(const std::string& name) override;
  void free(Stats::RawStatData& data) override;

private:
  SharedMemory& shmem_;
  ProcessSharedMutex log_lock_;
  ProcessSharedMutex access_log_lock_;
  ProcessSharedMutex stat_lock_;
};

} // namespace Server
} // namespace Envoy
