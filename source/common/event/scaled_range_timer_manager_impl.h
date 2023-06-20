#pragma once

#include <chrono>
#include <stack>

#include "envoy/event/dispatcher.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/event/timer.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Event {

/**
 * Implementation class for ScaledRangeTimerManager. Internally, this uses a set of queues to track
 * timers. When an enabled timer reaches its min duration, it adds a tracker object to the queue
 * corresponding to the duration (max - min). Each queue tracks timers of only a single duration,
 * and uses a real Timer object to schedule the expiration of the first timer in the queue. The
 * expectation is that the number of (max - min) values used to enable timers is small, so the
 * number of queues is tightly bounded. The queue-based implementation depends on that expectation
 * for efficient operation.
 */
class ScaledRangeTimerManagerImpl : public ScaledRangeTimerManager {
public:
  // Takes a Dispatcher and a map from timer type to scaled minimum value.
  ScaledRangeTimerManagerImpl(Dispatcher& dispatcher,
                              const ScaledTimerTypeMapConstSharedPtr& timer_minimums = nullptr);
  ~ScaledRangeTimerManagerImpl() override;

  // ScaledRangeTimerManager impl
  TimerPtr createTimer(ScaledTimerMinimum minimum, TimerCb callback) override;
  TimerPtr createTimer(ScaledTimerType timer_type, TimerCb callback) override;
  void setScaleFactor(UnitFloat scale_factor) override;

private:
  class RangeTimerImpl;

  // A queue object that maintains a list of timers with the same (max - min) values.
  struct Queue {
    struct Item {
      Item(RangeTimerImpl& timer, MonotonicTime active_time);
      // The timer owned by the caller being kept in the queue.
      RangeTimerImpl& timer_;
      // The time at which the timer became active (when its min duration expired).
      MonotonicTime active_time_;
    };

    // Typedef for convenience.
    using Iterator = std::list<Item>::iterator;

    Queue(std::chrono::milliseconds duration, ScaledRangeTimerManagerImpl& manager,
          Dispatcher& dispatcher);

    // The (max - min) value for all timers in range_timers_.
    const std::chrono::milliseconds duration_;

    // The list of active timers in this queue. This is implemented as a
    // std::list so that the iterators held in ScalingTimerHandle instances are
    // not invalidated by removal or insertion of other timers. The timers in
    // the list are in sorted order by active_time_ because they are only
    // inserted at the end of the list, and the time is monotonically increasing.
    std::list<Item> range_timers_;

    // A real Timer that tracks the expiration time of the first timer in the queue. This gets
    // adjusted
    //   1) at queue creation time
    //   2) on expiration
    //   3) when the scale factor changes
    const TimerPtr timer_;

    // A flag indicating whether the queue is currently processing timers. Used to guard against
    // queue deletion during timer processing.
    bool processing_timers_{false};
  };

  /**
   * An object passed back to RangeTimerImpl that can be used to remove it from its queue.
   */
  struct ScalingTimerHandle {
    ScalingTimerHandle(Queue& queue, Queue::Iterator iterator);
    Queue& queue_;
    Queue::Iterator iterator_;
  };

  struct Hash {
    // Magic declaration to allow heterogeneous lookup.
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    size_t operator()(const std::chrono::milliseconds duration) const {
      return hash_(duration.count());
    }
    size_t operator()(const Queue& queue) const { return (*this)(queue.duration_); }
    size_t operator()(const std::unique_ptr<Queue>& queue) const { return (*this)(*queue); }
    std::hash<std::chrono::milliseconds::rep> hash_;
  };

  struct Eq {
    // Magic declaration to allow heterogeneous lookup.
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    bool operator()(const std::unique_ptr<Queue>& lhs, std::chrono::milliseconds rhs) const {
      return lhs->duration_ == rhs;
    }
    bool operator()(const std::unique_ptr<Queue>& lhs, const Queue& rhs) const {
      return (*this)(lhs, rhs.duration_);
    }
    bool operator()(const std::unique_ptr<Queue>& lhs, const std::unique_ptr<Queue>& rhs) const {
      return (*this)(lhs, *rhs);
    }
  };

  static MonotonicTime computeTriggerTime(const Queue::Item& item,
                                          std::chrono::milliseconds duration,
                                          UnitFloat scale_factor);

  ScalingTimerHandle activateTimer(std::chrono::milliseconds duration, RangeTimerImpl& timer);

  void removeTimer(ScalingTimerHandle handle);

  void resetQueueTimer(Queue& queue, MonotonicTime now);

  void onQueueTimerFired(Queue& queue);

  Dispatcher& dispatcher_;
  const ScaledTimerTypeMapConstSharedPtr timer_minimums_;
  UnitFloat scale_factor_;
  absl::flat_hash_set<std::unique_ptr<Queue>, Hash, Eq> queues_;
};

} // namespace Event
} // namespace Envoy
