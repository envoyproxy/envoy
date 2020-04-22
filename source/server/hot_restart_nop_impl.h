#pragma once

#include <string>

#include "envoy/server/hot_restart.h"

#include "common/common/thread.h"
#include "common/stats/allocator_impl.h"

namespace Envoy {
namespace Server {

/**
 * No-op implementation of HotRestart.
 */
class HotRestartNopImpl : public Server::HotRestart {
public:
  // Server::HotRestart
  void drainParentListeners() override {}
  int duplicateParentListenSocket(const std::string&) override { return -1; }
  void initialize(Event::Dispatcher&, Server::Instance&) override {}
  void sendParentAdminShutdownRequest(time_t&) override {}
  void sendParentTerminateRequest() override {}
  ServerStatsFromParent mergeParentStatsIfAny(Stats::StoreRoot&) override { return {}; }
  void shutdown() override {}
  std::string version() override { return "disabled"; }
  Thread::BasicLockable& logLock() override { return log_lock_; }
  Thread::BasicLockable& accessLogLock() override { return access_log_lock_; }

private:
  Thread::MutexBasicLockable log_lock_;
  Thread::MutexBasicLockable access_log_lock_;
};

} // namespace Server
} // namespace Envoy
