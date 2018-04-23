#pragma once

#include <string>

#include "envoy/server/hot_restart.h"

#include "common/common/thread.h"
#include "common/stats/stats_impl.h"

#include "server/hot_restart_impl.h"

namespace Envoy {
namespace Server {

/**
 * No-op implementation of HotRestart.
 */
class HotRestartNopImpl : public Server::HotRestart {
public:
  HotRestartNopImpl(Options& options)
      : stats_set_options_(blockMemHashOptions(options.maxStats())) {
    uint32_t num_bytes = BlockMemoryHashSet<Stats::RawStatData>::numBytes(stats_set_options_);
    memory_.reset(new uint8_t[num_bytes]);
    memset(memory_.get(), 0, num_bytes);
    stats_allocator_ = std::make_unique<Stats::BlockRawStatDataAllocator>(
        stats_set_options_, true, memory_.get(), stat_lock_);
  }

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
  Stats::RawStatDataAllocator& statsAllocator() override { return *stats_allocator_; }

private:
  BlockMemoryHashSetOptions stats_set_options_;
  std::unique_ptr<uint8_t[]> memory_;
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable stat_lock_;
  Thread::MutexBasicLockable access_log_lock_;
  std::unique_ptr<Stats::BlockRawStatDataAllocator> stats_allocator_;
};

} // namespace Server
} // namespace Envoy
