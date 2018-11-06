#include "server/guarddog_impl.h"

#include <chrono>
#include <memory>

#include "envoy/stats/scope.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/lock_guard.h"

#include "server/watchdog_impl.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {
namespace Server {

GuardDogImpl::GuardDogImpl(Stats::Scope& stats_scope, const Server::Configuration::Main& config,
                           Event::TimeSystem& time_system)
    : time_system_(time_system), miss_timeout_(config.wdMissTimeout()),
      megamiss_timeout_(config.wdMegaMissTimeout()), kill_timeout_(config.wdKillTimeout()),
      multi_kill_timeout_(config.wdMultiKillTimeout()),
      loop_interval_([&]() -> std::chrono::milliseconds {
        // The loop interval is simply the minimum of all specified intervals,
        // but we must account for the 0=disabled case. This lambda takes care
        // of that and returns a value that initializes the const loop interval.
        const auto min_of_nonfatal = std::min(miss_timeout_, megamiss_timeout_);
        return std::min({killEnabled() ? kill_timeout_ : min_of_nonfatal,
                         multikillEnabled() ? multi_kill_timeout_ : min_of_nonfatal,
                         min_of_nonfatal});
      }()),
      watchdog_miss_counter_(stats_scope.counter("server.watchdog_miss")),
      watchdog_megamiss_counter_(stats_scope.counter("server.watchdog_mega_miss")),
      run_thread_(true) {
  start();
}

GuardDogImpl::~GuardDogImpl() { stop(); }

void GuardDogImpl::threadRoutine() {
  do {
    const auto now = time_system_.monotonicTime();
    bool seen_one_multi_timeout(false);
    Thread::LockGuard guard(wd_lock_);
    for (auto& watched_dog : watched_dogs_) {
      const auto ltt = watched_dog.dog_->lastTouchTime();
      const auto delta = now - ltt;
      if (watched_dog.last_alert_time_ && watched_dog.last_alert_time_.value() < ltt) {
        watched_dog.miss_alerted_ = false;
        watched_dog.megamiss_alerted_ = false;
      }
      if (delta > miss_timeout_) {
        if (!watched_dog.miss_alerted_) {
          watchdog_miss_counter_.inc();
          watched_dog.last_alert_time_ = ltt;
          watched_dog.miss_alerted_ = true;
        }
      }
      if (delta > megamiss_timeout_) {
        if (!watched_dog.megamiss_alerted_) {
          watchdog_megamiss_counter_.inc();
          watched_dog.last_alert_time_ = ltt;
          watched_dog.megamiss_alerted_ = true;
        }
      }
      if (killEnabled() && delta > kill_timeout_) {
        PANIC(fmt::format("GuardDog: one thread ({}) stuck for more than watchdog_kill_timeout",
                          watched_dog.dog_->threadId()));
      }
      if (multikillEnabled() && delta > multi_kill_timeout_) {
        if (seen_one_multi_timeout) {

          PANIC(fmt::format(
              "GuardDog: multiple threads ({},...) stuck for more than watchdog_multikill_timeout",
              watched_dog.dog_->threadId()));
        } else {
          seen_one_multi_timeout = true;
        }
      }
    }
  } while (waitOrDetectStop());
}

WatchDogSharedPtr GuardDogImpl::createWatchDog(int32_t thread_id) {
  // Timer started by WatchDog will try to fire at 1/2 of the interval of the
  // minimum timeout specified. loop_interval_ is const so all shared state
  // accessed out of the locked section below is const (time_system_ has no
  // state).
  auto wd_interval = loop_interval_ / 2;
  WatchDogSharedPtr new_watchdog =
      std::make_shared<WatchDogImpl>(thread_id, time_system_, wd_interval);
  WatchedDog watched_dog;
  watched_dog.dog_ = new_watchdog;
  {
    Thread::LockGuard guard(wd_lock_);
    watched_dogs_.push_back(watched_dog);
  }
  new_watchdog->touch();
  return new_watchdog;
}

void GuardDogImpl::stopWatching(WatchDogSharedPtr wd) {
  Thread::LockGuard guard(wd_lock_);
  auto found_wd = std::find_if(watched_dogs_.begin(), watched_dogs_.end(),
                               [&wd](const WatchedDog& d) -> bool { return d.dog_ == wd; });
  if (found_wd != watched_dogs_.end()) {
    watched_dogs_.erase(found_wd);
  } else {
    ASSERT(false);
  }
}

bool GuardDogImpl::waitOrDetectStop() {
  force_checked_event_.notifyAll();
  Thread::LockGuard guard(exit_lock_);
  // Spurious wakeups are OK without explicit handling. We'll just check
  // earlier than strictly required for that round.

  // Preferably, we should be calling
  //   time_system_.waitFor(exit_lock_, exit_event_, loop_interval_);
  // here, but that makes GuardDogMissTest.* very flaky. The reason that
  // directly calling condvar waitFor works is that it doesn't advance
  // simulated time, which the test is carefully controlling.
  //
  // One alternative approach that would be easier to test is to use a private
  // dispatcher and a TimerCB to execute the loop body of threadRoutine(). In
  // this manner, the same dynamics would occur in production, with added
  // overhead from libevent, But then the unit-test would purely control the
  // advancement of time, and thus be more robust. Another variation would be
  // to run this watchdog on the main-thread dispatcher, though such an approach
  // could not detect when the main-thread was stuck.
  exit_event_.waitFor(exit_lock_, loop_interval_); // NO_CHECK_FORMAT(real_time)

  return run_thread_;
}

void GuardDogImpl::start() {
  run_thread_ = true;
  thread_ = std::make_unique<Thread::Thread>([this]() -> void { threadRoutine(); });
}

void GuardDogImpl::stop() {
  {
    Thread::LockGuard guard(exit_lock_);
    run_thread_ = false;
    exit_event_.notifyAll();
  }
  if (thread_) {
    thread_->join();
    thread_.reset();
  }
}

} // namespace Server
} // namespace Envoy
