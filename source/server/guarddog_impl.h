#pragma once

#include <chrono>
#include <vector>

#include "envoy/server/configuration.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/watchdog.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "common/common/lock_guard.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Server {

/**
 * This feature performs deadlock detection stats collection & enforcement.
 *
 * It launches a thread that scans at an interval the minimum of the configured
 * intervals. If it finds starved threads or suspected deadlocks it will take
 * the appropriate action depending on the config parameters described below.
 *
 * Thread lifetime is tied to GuardDog object lifetime (RAII style).
 */
class GuardDogImpl : public GuardDog {
public:
  /**
   * @param stats_scope Statistics scope to write watchdog_miss and
   * watchdog_mega_miss events into.
   * @param config Configuration object.
   *
   * See the configuration documentation for details on the timeout settings.
   */
  GuardDogImpl(Stats::Scope& stats_scope, const Server::Configuration::Main& config,
               MonotonicTimeSource& tsource);
  ~GuardDogImpl();

  /**
   * Exposed for testing purposes only (but harmless to call):
   */
  int loopIntervalForTest() const { return loop_interval_.count(); }
  void forceCheckForTest() {
    exit_event_.notifyAll();
    Thread::LockGuard guard(exit_lock_);
    force_checked_event_.wait(exit_lock_);
  }

  // Server::GuardDog
  WatchDogSharedPtr createWatchDog(int32_t thread_id) override;
  void stopWatching(WatchDogSharedPtr wd) override;

private:
  void threadRoutine();
  /**
   * @return True if we should continue, false if signalled to stop.
   */
  bool waitOrDetectStop();
  void start() EXCLUSIVE_LOCKS_REQUIRED(exit_lock_);
  void stop();
  // Per the C++ standard it is OK to use these in ctor initializer as long as
  // it is after kill and multikill timeout values are initialized.
  bool killEnabled() const { return kill_timeout_ > std::chrono::milliseconds(0); }
  bool multikillEnabled() const { return multi_kill_timeout_ > std::chrono::milliseconds(0); }

  struct WatchedDog {
    WatchDogSharedPtr dog_;
    absl::optional<MonotonicTime> last_alert_time_;
    bool miss_alerted_{};
    bool megamiss_alerted_{};
  };

  MonotonicTimeSource& time_source_;
  const std::chrono::milliseconds miss_timeout_;
  const std::chrono::milliseconds megamiss_timeout_;
  const std::chrono::milliseconds kill_timeout_;
  const std::chrono::milliseconds multi_kill_timeout_;
  const std::chrono::milliseconds loop_interval_;
  Stats::Counter& watchdog_miss_counter_;
  Stats::Counter& watchdog_megamiss_counter_;
  std::vector<WatchedDog> watched_dogs_ GUARDED_BY(wd_lock_);
  Thread::MutexBasicLockable wd_lock_;
  Thread::ThreadPtr thread_;
  Thread::MutexBasicLockable exit_lock_;
  Thread::CondVar exit_event_;
  bool run_thread_ GUARDED_BY(exit_lock_);
  Thread::CondVar force_checked_event_;
};

} // namespace Server
} // namespace Envoy
