#pragma once

#include "envoy/event/guarddog.h"
#include "envoy/event/watchdog.h"
#include "envoy/server/configuration.h"
#include "envoy/stats/stats.h"

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"

namespace Event {

/** This feature performs deadlock detection stats collection&enforcement.
 *
 * It launches a thread that scans at an interval the minimum of the configured
 * intervals.  If it finds starved threads or suspected deadlocks it will take
 * the appropriate action depending on the config parameters described below.
 *
 * Thread lifetime is tied to GuardDog object lifetime (RAII style).
 */
class GuardDogImpl : public GuardDog {
public:
  /**
   * @param stats_store Statistics store to write watchdog_miss and
   * watchdog_mega_miss events into.
   * @param config Configuration object.
   *
   * Configuration:
   *   1) Miss timeout: Record a "miss" counter increment after this long has
   *   passed.  Old default: 100ms.
   *   2) Megamiss timeout: Record a "megamiss" counter increment after this
   *   long has passed.  Old setting: 1 second.
   *   3) Kill timeout: If any thread is stuck for longer than this kill the
   *   process.  0 to disable kill.
   *   4) Multi kill timeout: If two or more threads are stuck for longer than
   *   this kill the process. 0 to disable.
   *
   */
  GuardDogImpl(Stats::Store& stats_store, const Server::Configuration::Main& config,
               SystemTimeSource& tsource);
  ~GuardDogImpl();

  WatchDogSharedPtr getWatchDog(int32_t thread_id) override;
  void stopWatching(WatchDogSharedPtr wd) override;

  /**
   * Exposed for testing purposes only (but harmless to call):
   */
  int loopInterval() const { return loop_interval_.count(); }
  void force_check() {
    exit_event_.notify_all();
    std::lock_guard<std::mutex> guard(exit_lock_);
    force_checked_event_.wait(exit_lock_);
  }

private:
  void threadRoutine();
  /**
   * @return True if we should continue, false if signalled to stop.
   */
  bool waitOrDetectStop();
  void start();
  void exit();
  // Per the C++ standard it is OK to use these in ctor initializer as long as
  // it is after kill and multikill timeout values are initialized.
  bool kill_enabled() const { return kill_timeout_ > std::chrono::milliseconds(0); }
  bool multikill_enabled() const { return multi_kill_timeout_ > std::chrono::milliseconds(0); }

  SystemTimeSource& time_source_;
  const std::chrono::milliseconds miss_timeout_;
  const std::chrono::milliseconds megamiss_timeout_;
  const std::chrono::milliseconds kill_timeout_;
  const std::chrono::milliseconds multi_kill_timeout_;
  const std::chrono::milliseconds loop_interval_;
  Stats::Counter& watchdog_miss_counter_;
  Stats::Counter& watchdog_megamiss_counter_;
  std::vector<WatchDogSharedPtr> watched_dogs_;
  std::mutex wd_lock_;
  Thread::ThreadPtr thread_;
  std::mutex exit_lock_;
  std::condition_variable_any exit_event_;
  bool run_thread_;
  std::condition_variable_any force_checked_event_;
};

} // Event
