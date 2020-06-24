#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "common/event/scaled_timer.h"
#include <algorithm>
#include <chrono>

namespace Envoy {
namespace Event {

class ScaledTimerManager::ScaledTimerImpl final : public Timer {
public:
  struct ActiveTimer {
    const MonotonicTime start;
    const std::chrono::microseconds delay;
    const ScopeTrackedObject* object;
  };

  ScaledTimerImpl(ScaledTimerManager& manager, TimerCb callback);

  ~ScaledTimerImpl() override;

  /**
   * Run the timer callback on the given dispatcher.
   */
  void dispatch(Dispatcher& dispatcher);
  const absl::optional<ActiveTimer>& active() const;

  // Timer impl:
  void enableTimer(const std::chrono::milliseconds& delay,
                   const ScopeTrackedObject* object) override;

  void enableHRTimer(const std::chrono::microseconds& delay,
                     const ScopeTrackedObject* object) override;

  bool enabled() override { return active_.has_value(); }

  void disableTimer() override;

private:
  ScaledTimerManager& manager_;
  const TimerCb callback_;
  absl::optional<ActiveTimer> active_;
};

ScaledTimerManager::ScaledTimerImpl::ScaledTimerImpl(ScaledTimerManager& manager, TimerCb callback)
    : manager_(manager), callback_(std::move(callback)) {}

ScaledTimerManager::ScaledTimerImpl::~ScaledTimerImpl() {
  if (active_.has_value()) {
    manager_.descheduleTimer(this);
  }
}

void ScaledTimerManager::ScaledTimerImpl::enableTimer(const std::chrono::milliseconds& delay,
                                                      const ScopeTrackedObject* object) {
  if (active_.has_value()) {
    disableTimer();
  }
  active_.emplace(ActiveTimer{
      .start = manager_.dispatcher_.timeSource().monotonicTime(),
      .delay = delay,
      .object = object,
  });
  manager_.scheduleTimer(this);
}

void ScaledTimerManager::ScaledTimerImpl::enableHRTimer(const std::chrono::microseconds& delay,
                                                        const ScopeTrackedObject* object) {
  if (active_.has_value()) {
    disableTimer();
  }
  active_.emplace(ActiveTimer{
      .start = manager_.dispatcher_.timeSource().monotonicTime(),
      .delay = delay,
      .object = object,
  });
  manager_.scheduleTimer(this);
}

void ScaledTimerManager::ScaledTimerImpl::disableTimer() {
  manager_.descheduleTimer(this);
  active_.reset();
}

void ScaledTimerManager::ScaledTimerImpl::dispatch(Dispatcher& dispatcher) {
  ASSERT(active_.has_value());
  if (active_->object == nullptr) {
    callback_();
  } else {
    ScopeTrackerScopeState scope(active_->object, dispatcher);
    callback_();
  }
  active_.reset();
}

const absl::optional<ScaledTimerManager::ScaledTimerImpl::ActiveTimer>&
ScaledTimerManager::ScaledTimerImpl::active() const {
  return active_;
}

ScaledTimerManager::ScaledTimerManager(Dispatcher& dispatcher)
    : dispatcher_(dispatcher),
      dispatch_timer_(dispatcher.createTimer([this] { dispatchCallback(); })), timers_(1.0) {}

Event::TimerPtr ScaledTimerManager::createTimer(TimerCb callback) {
  return std::make_unique<ScaledTimerImpl>(*this, std::move(callback));
}

void ScaledTimerManager::setDurationScaleFactor(double scale_factor) {
  timers_.setScaleFactor(scale_factor);
  internalScheduleTimer();
}

ScaledTimerManager::ScaledTimerList::ScaledTimerList(double scale_factor)
    : timers_(Compare{.scale_factor = scale_factor}) {}

void ScaledTimerManager::ScaledTimerList::addActive(ScaledTimerImpl* timer) {
  ASSERT(timer->active().has_value());
  timers_.insert(timer);
}

void ScaledTimerManager::ScaledTimerList::removeActive(ScaledTimerImpl* timer) {
  ASSERT(timer->active().has_value());
  timers_.erase(timer);
}

void ScaledTimerManager::ScaledTimerList::setScaleFactor(double scale_factor) {
  std::set<ScaledTimerImpl*, Compare> reordered(timers_.begin(), timers_.end(),
                                                Compare{.scale_factor = scale_factor});
  std::swap(reordered, timers_);
}

absl::optional<ScaledTimerManager::ScaledTimerImpl*>
ScaledTimerManager::ScaledTimerList::pop(MonotonicTime now) {
  if (timers_.empty()) {
    return absl::nullopt;
  }

  auto* timer = *timers_.begin();
  if (timers_.key_comp().targetTime(timer) <= now) {
    timers_.erase(timers_.begin());
    return timer;
  }
  return absl::nullopt;
}

absl::optional<MonotonicTime> ScaledTimerManager::ScaledTimerList::firstScheduleTime() const {
  if (timers_.empty()) {
    return absl::nullopt;
  }
  return timers_.key_comp().targetTime(*timers_.begin());
}

MonotonicTime
ScaledTimerManager::ScaledTimerList::Compare::targetTime(const ScaledTimerImpl* timer) const {
  ASSERT(timer->active().has_value());

  auto scaled_delay = timer->active()->delay * scale_factor;
  return timer->active()->start +
         std::chrono::duration_cast<std::chrono::microseconds>(scaled_delay);
}

bool ScaledTimerManager::ScaledTimerList::Compare::operator()(const ScaledTimerImpl* lhs,
                                                              const ScaledTimerImpl* rhs) const {
  return targetTime(lhs) < targetTime(rhs);
}

void ScaledTimerManager::scheduleTimer(ScaledTimerImpl* timer) {
  ASSERT(timer->active().has_value());

  timers_.addActive(timer);
  internalScheduleTimer();
}

void ScaledTimerManager::descheduleTimer(ScaledTimerImpl* timer) {
  ASSERT(timer->active().has_value());
  timers_.removeActive(timer);
  internalScheduleTimer();
}

void ScaledTimerManager::dispatchCallback() {
  // Find all timers that are ready to be handled and dispatch them.
  const auto now = dispatcher_.timeSource().monotonicTime();
  for (auto timer = timers_.pop(now); timer.has_value(); timer = timers_.pop(now)) {
    timer.value()->dispatch(dispatcher_);
  }

  internalScheduleTimer();
}

void ScaledTimerManager::internalScheduleTimer() {
  auto next_time = timers_.firstScheduleTime();
  if (!next_time.has_value()) {
    dispatch_timer_->disableTimer();
    return;
  }
  const MonotonicTime now = dispatcher_.timeSource().monotonicTime();
  const auto target_delay = std::chrono::duration_cast<std::chrono::microseconds>(*next_time - now);

  if (target_delay > std::chrono::microseconds::zero()) {
    dispatch_timer_->enableHRTimer(target_delay);
  } else {
    dispatch_timer_->enableHRTimer(std::chrono::microseconds::zero());
  }
}
} // namespace Event
} // namespace Envoy
