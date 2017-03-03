#pragma once

#include <cstring>

#include "envoy/server/hot_restart.h"

#include "common/common/thread.h"

namespace Server {

/**
 * Implementation of HotRestart that does not perform a hot restart. Used for config validation
 * runs, where we don't want to disturb another running instance.
 */
class ValidationHotRestart : public HotRestart {
public:
  Thread::BasicLockable& accessLogLock() { return access_log_lock_; }
  Thread::BasicLockable& logLock() { return log_lock_; }

  // Server::HotRestart
  void drainParentListeners() {}
  int duplicateParentListenSocket(const std::string&) { return -1; }
  void getParentStats(GetParentStatsInfo& info) { memset(&info, 0, sizeof(info)); }
  void initialize(Event::Dispatcher&, Server::Instance&) {}
  void shutdownParentAdmin(ShutdownParentAdminInfo&) {}
  void terminateParent() {}
  void shutdown() {}
  std::string version() { return ""; }

private:
  Thread::MutexBasicLockable access_log_lock_;
  Thread::MutexBasicLockable log_lock_;
};

} // Server
