#include "common/event/scaled_range_timer_manager.h"

#include <chrono>
#include <cmath>
#include <memory>

#include "envoy/event/range_timer.h"
#include "envoy/event/timer.h"

#include "common/common/assert.h"
#include "common/common/scope_tracker.h"

namespace Envoy {
namespace Event {

/**
 * Implementation of RangeTimer that can be scaled by the backing manager object.
 *
 * Instances of this class exist in one of 3 states:
 *  - disabled: not enabled
 *  - waiting-for-min: enabled, min timeout not elapsed
 *  - scaling-max: enabled, min timeout elapsed, max timeout not elapsed
 */
class ScaledRangeTimerManager::RangeTimerImpl final : public RangeTimer {
public:
  RangeTimerImpl(TimerCb callback, ScaledRangeTimerManager& manager)
      : manager_(manager), callback_(std::move(callback)),
        min_duration_timer_(manager.dispatcher_.createTimer([this] { onPendingTimerComplete(); })) {
  }

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

  void enableTimer(const std::chrono::milliseconds& min_ms, const std::chrono::milliseconds& max_ms,
                   const ScopeTrackedObject* scope) override {
    disableTimer();
    scope_ = scope;
    ENVOY_LOG_MISC(trace, "enableTimer called on {} for ({}ms, {}ms)", static_cast<void*>(this),
                   min_ms.count(), max_ms.count());
    if (min_ms <= std::chrono::milliseconds::zero()) {
      auto handle = manager_.activateTimer(max_ms, *this);
      state_.emplace<ScalingMax>(handle);
    } else {
      state_.emplace<WaitingForMin>(max_ms - min_ms);
      min_duration_timer_->enableTimer(min_ms);
    }
  }

  bool enabled() override { return !absl::holds_alternative<Inactive>(state_); }

  void trigger() {
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
    ScalingMax(ScaledRangeTimerManager::ScalingTimerHandle handle) : handle_(handle) {}

    // A handle that can be used to disable the timer.
    ScaledRangeTimerManager::ScalingTimerHandle handle_;
  };

  void onPendingTimerComplete() {
    ENVOY_LOG_MISC(info, "pending complete for {}", static_cast<void*>(this));
    ASSERT(absl::holds_alternative<WaitingForMin>(state_));
    WaitingForMin& waiting = absl::get<WaitingForMin>(state_);

    if (waiting.scalable_duration_ < std::chrono::milliseconds::zero()) {
      trigger();
    } else {
      state_.emplace<ScalingMax>(manager_.activateTimer(waiting.scalable_duration_, *this));
    }
  }

  ScaledRangeTimerManager& manager_;
  const TimerCb callback_;
  const TimerPtr min_duration_timer_;

  absl::variant<Inactive, WaitingForMin, ScalingMax> state_;
  const ScopeTrackedObject* scope_;
};

ScaledRangeTimerManager::ScaledRangeTimerManager(Dispatcher& dispatcher)
    : dispatcher_(dispatcher), scale_factor_(1.0) {}

RangeTimerPtr ScaledRangeTimerManager::createTimer(TimerCb callback) {
  return std::make_unique<RangeTimerImpl>(callback, *this);
}

void ScaledRangeTimerManager::setScaleFactor(double scale_factor) {
  const MonotonicTime now = dispatcher_.approximateMonotonicTime();
  scale_factor_ = DurationScaleFactor(scale_factor);
  for (auto& queue : queues_) {
    resetQueueTimer(*queue, now);
  }
}

ScaledRangeTimerManager::Queue::Item::Item(RangeTimerImpl& timer, MonotonicTime active_time)
    : timer_(timer), active_time_(active_time) {}

ScaledRangeTimerManager::Queue::Queue(std::chrono::milliseconds duration,
                                      ScaledRangeTimerManager& manager, Dispatcher& dispatcher)
    : duration_(duration),
      timer_(dispatcher.createTimer([this, &manager] { manager.onQueueTimerFired(*this); })) {}

ScaledRangeTimerManager::ScalingTimerHandle::ScalingTimerHandle(Queue& queue,
                                                                Queue::Iterator iterator)
    : queue_(queue), iterator_(iterator) {}

ScaledRangeTimerManager::DurationScaleFactor::DurationScaleFactor(double value)
    : value_(std::max(0.0, std::min(value, 1.0))) {}

ScaledRangeTimerManager::ScalingTimerHandle
ScaledRangeTimerManager::activateTimer(std::chrono::milliseconds duration,
                                       RangeTimerImpl& range_timer) {
  ASSERT(dispatcher_.isThreadSafe());
  auto it = queues_.find(duration);
  if (it == queues_.end()) {
    auto queue = std::make_unique<Queue>(duration, *this, dispatcher_);
    it = queues_.emplace(std::move(queue)).first;
  }
  Queue& queue = **it;

  queue.range_timers_.emplace_back(range_timer, dispatcher_.approximateMonotonicTime());
  if (queue.range_timers_.size() == 1) {
    resetQueueTimer(queue, dispatcher_.approximateMonotonicTime());
  }

  return ScalingTimerHandle(queue, --queue.range_timers_.end());
}

void ScaledRangeTimerManager::removeTimer(ScalingTimerHandle handle) {
  ASSERT(dispatcher_.isThreadSafe());
  const bool was_front = handle.queue_.range_timers_.begin() == handle.iterator_;
  handle.queue_.range_timers_.erase(handle.iterator_);
  if (handle.queue_.range_timers_.empty()) {
    queues_.erase(handle.queue_);
  } else {
    if (was_front) {
      resetQueueTimer(handle.queue_, dispatcher_.approximateMonotonicTime());
    }
  }
}

void ScaledRangeTimerManager::resetQueueTimer(Queue& queue, MonotonicTime now) {
  ASSERT(!queue.range_timers_.empty());
  const MonotonicTime trigger_time =
      queue.range_timers_.front().active_time_ +
      std::chrono::duration_cast<MonotonicTime::duration>(queue.duration_ * scale_factor_.value());
  if (trigger_time < now) {
    queue.timer_->enableTimer(std::chrono::milliseconds::zero());
  } else {
    queue.timer_->enableTimer(
        std::chrono::duration_cast<std::chrono::milliseconds>(trigger_time - now));
  }
}

void ScaledRangeTimerManager::onQueueTimerFired(Queue& queue) {
  ASSERT(!queue.range_timers_.empty());
  auto item = std::move(queue.range_timers_.front());
  queue.range_timers_.pop_front();
  item.timer_.trigger();

  if (queue.range_timers_.empty()) {
    queues_.erase(queue);
  } else {
    resetQueueTimer(queue, dispatcher_.approximateMonotonicTime());
  }
}

} // namespace Event
} // namespace Envoy
