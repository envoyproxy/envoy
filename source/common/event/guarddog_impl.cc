#include "common/common/assert.h"
#include "common/event/guarddog_impl.h"
#include "common/event/watchdog_impl.h"

namespace Event {

GuardDogImpl::GuardDogImpl(Stats::Store& stats_store, const Server::Configuration::Main& config,
                           SystemTimeSource& tsource)
    : time_source_(tsource), miss_timeout_(config.wdMissTimeout()),
      megamiss_timeout_(config.wdMegaMissTimeout()), kill_timeout_(config.wdKillTimeout()),
      multi_kill_timeout_(config.wdMultiKillTimeout()),
      loop_interval_(
          std::min({miss_timeout_, megamiss_timeout_,
                    kill_enabled() ? kill_timeout_ : std::min(miss_timeout_, megamiss_timeout_),
                    multikill_enabled() ? multi_kill_timeout_
                                        : std::min(miss_timeout_, megamiss_timeout_)})),
      watchdog_miss_counter_(stats_store.counter("server.watchdog_miss")),
      watchdog_megamiss_counter_(stats_store.counter("server.watchdog_mega_miss")),
      run_thread_(true) {
  start();
}

GuardDogImpl::~GuardDogImpl() { exit(); }

void GuardDogImpl::threadRoutine() {
  do {
    auto now = time_source_.currentSystemTime();
    bool seen_one_multi_timeout(false);
    std::lock_guard<std::mutex> guard(wd_lock_);
    for (auto watched_dog : watched_dogs_) {
      auto delta = now - watched_dog->lastTouchTime();
      if (delta > miss_timeout_) {
        watchdog_miss_counter_.inc();
      }
      if (delta > megamiss_timeout_) {
        watchdog_megamiss_counter_.inc();
      }
      if (kill_enabled() && delta > kill_timeout_) {
        PANIC("GuardDog: one thread stuck for more than watchdog_kill_timeout");
      }
      if (multikill_enabled() && delta > multi_kill_timeout_) {
        if (seen_one_multi_timeout) {
          PANIC("GuardDog: multiple threads stuck for more than watchdog_multikill_timeout");
        } else {
          seen_one_multi_timeout = true;
        }
      }
    }
  } while (waitOrDetectStop());
}

WatchDogSharedPtr GuardDogImpl::getWatchDog(int32_t thread_id) {
  // Timer started by WatchDog will try to fire at 3/4 of the interval of the
  // minimum timeout specified.
  auto wd_interval = loop_interval_ * 3 / 4;
  WatchDogSharedPtr new_watchdog =
      std::make_shared<WatchDogImpl>(thread_id, time_source_, wd_interval);
  {
    std::lock_guard<std::mutex> guard(wd_lock_);
    watched_dogs_.push_back(new_watchdog);
  }
  new_watchdog->touch();
  return new_watchdog;
}

void GuardDogImpl::stopWatching(WatchDogSharedPtr wd) {
  std::lock_guard<std::mutex> guard(wd_lock_);
  auto found_wd = std::find(watched_dogs_.begin(), watched_dogs_.end(), wd);
  if (found_wd != watched_dogs_.end()) {
    watched_dogs_.erase(found_wd);
  }
}

bool GuardDogImpl::waitOrDetectStop() {
  force_checked_event_.notify_all();
  std::lock_guard<std::mutex> guard(exit_lock_);
  // Spurious wakeups are OK without explicit handling.  We'll just check
  // earlier than strictly required for that round.
  exit_event_.wait_for(exit_lock_, std::chrono::milliseconds(loop_interval_));
  return run_thread_;
}

void GuardDogImpl::start() {
  run_thread_ = true;
  thread_.reset(new Thread::Thread([this]() -> void { threadRoutine(); }));
}

void GuardDogImpl::exit() {
  {
    std::lock_guard<std::mutex> guard(exit_lock_);
    run_thread_ = false;
    exit_event_.notify_all();
  }
  if (thread_) {
    thread_->join();
    thread_.reset();
  }
}

} // Event
