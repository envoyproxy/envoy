#pragma once

#include <chrono>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/event/timer.h"
#include "envoy/server/configuration.h"
#include "envoy/server/guarddog.h"
#include "envoy/server/guarddog_config.h"
#include "envoy/server/watchdog.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"
#include "source/common/event/libevent.h"
#include "source/server/watchdog_impl.h"

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
   * Defines a test interlock hook to enable tests to synchronize the guard-dog
   * execution so they can probe current counter values. The default
   * implementation that runs in production has empty methods, which are
   * overridden in the implementation used during tests.
   */
  class TestInterlockHook {
  public:
    virtual ~TestInterlockHook() = default;

    /**
     * Called from GuardDogImpl to indicate that it has evaluated all watch-dogs up to a particular
     * point in time. Called while the GuardDog mutex is held.
     */
    virtual void signalFromImpl() {}

    /**
     * Called from GuardDog tests to block until the implementation has reached the desired
     * condition. Called while the GuardDog mutex is held.
     * @param mutex The GuardDog's mutex for use by Thread::CondVar::wait.
     */
    virtual void waitFromTest(Thread::MutexBasicLockable& /*mutex*/) {}
  };

  /**
   * @param stats_scope Statistics scope to write watchdog_miss and
   * watchdog_mega_miss events into.
   * @param config Configuration object.
   * @param api API object.
   * @param test_interlock a hook for enabling interlock with unit tests.
   *
   * See the configuration documentation for details on the timeout settings.
   */
  GuardDogImpl(Stats::Scope& stats_scope, const Server::Configuration::Watchdog& config,
               Api::Api& api, absl::string_view name,
               std::unique_ptr<TestInterlockHook>&& test_interlock);
  GuardDogImpl(Stats::Scope& stats_scope, const Server::Configuration::Watchdog& config,
               Api::Api& api, absl::string_view name);
  ~GuardDogImpl() override;

  /**
   * Exposed for testing purposes only (but harmless to call):
   */
  const std::chrono::milliseconds loopIntervalForTest() const { return loop_interval_; }

  /**
   * Test hook to force a step() to catch up with the current watchdog state and simulated time.
   * This is inlined so that it does not need to be present in the production binary.
   */
  void forceCheckForTest() {
    Thread::LockGuard guard(mutex_);
    dispatcher_->post([this]() { loop_timer_->enableTimer(std::chrono::milliseconds(0)); });
    test_interlock_hook_->waitFromTest(mutex_);
  }

  // Server::GuardDog
  WatchDogSharedPtr createWatchDog(Thread::ThreadId thread_id, const std::string& thread_name,
                                   Event::Dispatcher& dispatcher) override;
  void stopWatching(WatchDogSharedPtr wd) override;

private:
  void start(Api::Api& api);
  void step();
  void stop();
  // Per the C++ standard it is OK to use these in ctor initializer as long as
  // it is after kill and multikill timeout values are initialized.
  bool killEnabled() const { return kill_timeout_ > std::chrono::milliseconds(0); }
  bool multikillEnabled() const { return multi_kill_timeout_ > std::chrono::milliseconds(0); }

  using WatchDogAction = envoy::config::bootstrap::v3::Watchdog::WatchdogAction;
  // Helper function to invoke all the GuardDogActions registered for an Event.
  void invokeGuardDogActions(
      WatchDogAction::WatchdogEvent event,
      std::vector<std::pair<Thread::ThreadId, MonotonicTime>> thread_last_checkin_pairs,
      MonotonicTime now);

  using WatchDogImplSharedPtr = std::shared_ptr<WatchDogImpl>;
  struct WatchedDog {
    WatchedDog(Stats::Scope& stats_scope, const std::string& thread_name,
               const WatchDogImplSharedPtr& watch_dog);

    const WatchDogImplSharedPtr dog_;
    MonotonicTime last_checkin_;
    absl::optional<MonotonicTime> last_alert_time_;
    bool miss_alerted_{};
    bool megamiss_alerted_{};
    Stats::Counter& miss_counter_;
    Stats::Counter& megamiss_counter_;
  };
  using WatchedDogPtr = std::unique_ptr<WatchedDog>;

  std::unique_ptr<TestInterlockHook> test_interlock_hook_;
  Stats::Scope& stats_scope_;
  TimeSource& time_source_;
  const std::chrono::milliseconds miss_timeout_;
  const std::chrono::milliseconds megamiss_timeout_;
  const std::chrono::milliseconds kill_timeout_;
  const std::chrono::milliseconds multi_kill_timeout_;
  const double multi_kill_fraction_;
  const std::chrono::milliseconds loop_interval_;
  Stats::Counter& watchdog_miss_counter_;
  Stats::Counter& watchdog_megamiss_counter_;
  std::vector<WatchedDogPtr> watched_dogs_ ABSL_GUARDED_BY(wd_lock_);
  Thread::MutexBasicLockable wd_lock_;
  Thread::ThreadPtr thread_;
  Event::DispatcherPtr dispatcher_;
  Event::TimerPtr loop_timer_;
  using EventToActionsMap = absl::flat_hash_map<WatchDogAction::WatchdogEvent,
                                                std::vector<Configuration::GuardDogActionPtr>>;
  EventToActionsMap events_to_actions_;
  Thread::MutexBasicLockable mutex_;
  bool run_thread_ ABSL_GUARDED_BY(mutex_);
};

} // namespace Server
} // namespace Envoy
