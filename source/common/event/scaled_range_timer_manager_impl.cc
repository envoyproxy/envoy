#include "common/event/scaled_range_timer_manager_impl.h"

#include <chrono>
#include <cmath>
#include <memory>

#include "envoy/event/timer.h"

#include "common/common/assert.h"
#include "common/common/scope_tracker.h"

namespace Envoy {
namespace Event {

/**
 * Implementation of Timer that can be scaled by the backing manager object.
 *
 * Instances of this class exist in one of 3 states:
 *  - inactive: not enabled
 *  - waiting-for-min: enabled, min timeout not elapsed
 *  - scaling-max: enabled, min timeout elapsed, max timeout not elapsed
 *
 * The allowed state transitions are:
 *  - inactive -> waiting-for-min
 *  - waiting-for-min -> scaling-max | inactive
 *  - scaling-max -> inactive
 *
 * Some methods combine multiple state transitions; enableTimer(0, max) on a
 * timer in the scaling-max state will logically execute the transition sequence
 * [scaling-max -> inactive -> waiting-for-min -> scaling-max] in a single
 * method call. The waiting-for-min transitions are elided for efficiency.
 */
class ScaledRangeTimerManagerImpl::RangeTimerImpl final : public Timer {
public:
  RangeTimerImpl(ScaledTimerMinimum minimum, TimerCb callback, ScaledRangeTimerManagerImpl& manager)
      : minimum_(minimum), manager_(manager), callback_(std::move(callback)),
        min_duration_timer_(manager.dispatcher_.createTimer([this] { onMinTimerComplete(); })) {}

  ~RangeTimerImpl() override { disableTimer(); }

  void disableTimer() override {
    struct Dispatch {
      Dispatch(RangeTimerImpl& timer) : timer_(timer) {}
      RangeTimerImpl& timer_;
      void operator()(const Inactive&) {}
      void operator()(const WaitingForMin&) { timer_.min_duration_timer_->disableTimer(); }
      void operator()(ScalingMax& active) { timer_.manager_.removeTimer(active.handle_); }
    };
    absl::visit(Dispatch(*this), state_);
    state_.emplace<Inactive>();
    scope_ = nullptr;
  }

  void enableTimer(const std::chrono::milliseconds max_ms,
                   const ScopeTrackedObject* scope) override {
    disableTimer();
    scope_ = scope;
    const std::chrono::milliseconds min_ms = std::min(minimum_.computeMinimum(max_ms), max_ms);
    ENVOY_LOG_MISC(trace, "enableTimer called on {} for {}ms, min is {}ms",
                   static_cast<void*>(this), max_ms.count(), min_ms.count());
    if (min_ms <= std::chrono::milliseconds::zero()) {
      // If the duration spread (max - min) is zero, skip over the waiting-for-min and straight to
      // the scaling-max state.
      auto handle = manager_.activateTimer(max_ms, *this);
      state_.emplace<ScalingMax>(handle);
    } else {
      state_.emplace<WaitingForMin>(max_ms - min_ms);
      min_duration_timer_->enableTimer(std::min(max_ms, min_ms));
    }
  }

  void enableHRTimer(std::chrono::microseconds us,
                     const ScopeTrackedObject* object = nullptr) override {
    enableTimer(std::chrono::duration_cast<std::chrono::milliseconds>(us), object);
  }

  bool enabled() override { return !absl::holds_alternative<Inactive>(state_); }

  void trigger() {
    ASSERT(manager_.dispatcher_.isThreadSafe());
    ASSERT(!absl::holds_alternative<Inactive>(state_));
    ENVOY_LOG_MISC(trace, "RangeTimerImpl triggered: {}", static_cast<void*>(this));
    state_.emplace<Inactive>();
    if (scope_ == nullptr) {
      callback_();
    } else {
      ScopeTrackerScopeState scope(scope_, manager_.dispatcher_);
      scope_ = nullptr;
      callback_();
    }
  }

private:
  struct Inactive {};

  struct WaitingForMin {
    WaitingForMin(std::chrono::milliseconds scalable_duration)
        : scalable_duration_(scalable_duration) {}

    // The amount of time between this enabled timer's max and min, which should
    // be scaled by the current scale factor.
    const std::chrono::milliseconds scalable_duration_;
  };

  struct ScalingMax {
    ScalingMax(ScaledRangeTimerManagerImpl::ScalingTimerHandle handle) : handle_(handle) {}

    // A handle that can be used to disable the timer.
    ScaledRangeTimerManagerImpl::ScalingTimerHandle handle_;
  };

  /**
   * This is called when the min timer expires, on the dispatcher for the manager. It registers with
   * the manager so the duration can be scaled, unless the duration is zero in which case it just
   * triggers the callback right away.
   */
  void onMinTimerComplete() {
    ASSERT(manager_.dispatcher_.isThreadSafe());
    ENVOY_LOG_MISC(info, "min timer complete for {}", static_cast<void*>(this));
    ASSERT(absl::holds_alternative<WaitingForMin>(state_));
    const WaitingForMin& waiting = absl::get<WaitingForMin>(state_);

    // This
    if (waiting.scalable_duration_ < std::chrono::milliseconds::zero()) {
      trigger();
    } else {
      state_.emplace<ScalingMax>(manager_.activateTimer(waiting.scalable_duration_, *this));
    }
  }

  const ScaledTimerMinimum minimum_;
  ScaledRangeTimerManagerImpl& manager_;
  const TimerCb callback_;
  const TimerPtr min_duration_timer_;

  absl::variant<Inactive, WaitingForMin, ScalingMax> state_;
  const ScopeTrackedObject* scope_;
};

ScaledRangeTimerManagerImpl::ScaledRangeTimerManagerImpl(Dispatcher& dispatcher)
    : dispatcher_(dispatcher), scale_factor_(1.0) {}

ScaledRangeTimerManagerImpl::~ScaledRangeTimerManagerImpl() {
  // Scaled timers created by the manager shouldn't outlive it. This is
  // necessary but not sufficient to guarantee that.
  ASSERT(queues_.empty());
}

TimerPtr ScaledRangeTimerManagerImpl::createTimer(ScaledTimerMinimum minimum, TimerCb callback) {
  return std::make_unique<RangeTimerImpl>(minimum, callback, *this);
}

void ScaledRangeTimerManagerImpl::setScaleFactor(double scale_factor) {
  const MonotonicTime now = dispatcher_.approximateMonotonicTime();
  scale_factor_ = DurationScaleFactor(scale_factor);
  for (auto& queue : queues_) {
    resetQueueTimer(*queue, now);
  }
}

ScaledRangeTimerManagerImpl::Queue::Item::Item(RangeTimerImpl& timer, MonotonicTime active_time)
    : timer_(timer), active_time_(active_time) {}

ScaledRangeTimerManagerImpl::Queue::Queue(std::chrono::milliseconds duration,
                                          ScaledRangeTimerManagerImpl& manager,
                                          Dispatcher& dispatcher)
    : duration_(duration),
      timer_(dispatcher.createTimer([this, &manager] { manager.onQueueTimerFired(*this); })) {}

ScaledRangeTimerManagerImpl::ScalingTimerHandle::ScalingTimerHandle(Queue& queue,
                                                                    Queue::Iterator iterator)
    : queue_(queue), iterator_(iterator) {}

ScaledRangeTimerManagerImpl::DurationScaleFactor::DurationScaleFactor(double value)
    : value_(std::max(0.0, std::min(value, 1.0))) {}

MonotonicTime ScaledRangeTimerManagerImpl::computeTriggerTime(const Queue::Item& item,
                                                              std::chrono::milliseconds duration,
                                                              DurationScaleFactor scale_factor) {
  return item.active_time_ +
         std::chrono::duration_cast<MonotonicTime::duration>(duration * scale_factor.value());
}

ScaledRangeTimerManagerImpl::ScalingTimerHandle
ScaledRangeTimerManagerImpl::activateTimer(std::chrono::milliseconds duration,
                                           RangeTimerImpl& range_timer) {
  // Ensure this is being called on the same dispatcher.
  ASSERT(dispatcher_.isThreadSafe());

  // Find the matching queue for the (max - min) duration of the range timer; if there isn't one,
  // create it.
  auto it = queues_.find(duration);
  if (it == queues_.end()) {
    auto queue = std::make_unique<Queue>(duration, *this, dispatcher_);
    it = queues_.emplace(std::move(queue)).first;
  }
  Queue& queue = **it;

  // Put the timer at the back of the queue. Since the timer has the same maximum duration as all
  // the other timers in the queue, and since the activation times are monotonic, the queue stays in
  // sorted order.
  queue.range_timers_.emplace_back(range_timer, dispatcher_.approximateMonotonicTime());
  if (queue.range_timers_.size() == 1) {
    resetQueueTimer(queue, dispatcher_.approximateMonotonicTime());
  }

  return ScalingTimerHandle(queue, --queue.range_timers_.end());
}

void ScaledRangeTimerManagerImpl::removeTimer(ScalingTimerHandle handle) {
  // Ensure this is being called on the same dispatcher.
  ASSERT(dispatcher_.isThreadSafe());

  const bool was_front = handle.queue_.range_timers_.begin() == handle.iterator_;
  handle.queue_.range_timers_.erase(handle.iterator_);
  // Don't keep around empty queues
  if (handle.queue_.range_timers_.empty()) {
    queues_.erase(handle.queue_);
    return;
  }

  // The queue's timer tracks the expiration time of the first range timer, so it only needs
  // adjusting if the first timer is the one that was removed.
  if (was_front) {
    resetQueueTimer(handle.queue_, dispatcher_.approximateMonotonicTime());
  }
}

void ScaledRangeTimerManagerImpl::resetQueueTimer(Queue& queue, MonotonicTime now) {
  ASSERT(!queue.range_timers_.empty());
  const MonotonicTime trigger_time =
      computeTriggerTime(queue.range_timers_.front(), queue.duration_, scale_factor_);
  if (trigger_time < now) {
    queue.timer_->enableTimer(std::chrono::milliseconds::zero());
  } else {
    queue.timer_->enableTimer(
        std::chrono::duration_cast<std::chrono::milliseconds>(trigger_time - now));
  }
}

void ScaledRangeTimerManagerImpl::onQueueTimerFired(Queue& queue) {
  auto& timers = queue.range_timers_;
  ASSERT(!timers.empty());
  const MonotonicTime now = dispatcher_.approximateMonotonicTime();

  // Pop and trigger timers until the one at the front isn't supposed to have expired yet (given the
  // current scale factor).
  while (!timers.empty() &&
         computeTriggerTime(timers.front(), queue.duration_, scale_factor_) <= now) {
    auto item = std::move(queue.range_timers_.front());
    queue.range_timers_.pop_front();
    item.timer_.trigger();
  }

  if (queue.range_timers_.empty()) {
    // Maintain the invariant that queues are never empty.
    queues_.erase(queue);
  } else {
    resetQueueTimer(queue, now);
  }
}

} // namespace Event
} // namespace Envoy
