#include <chrono>
#include <stack>

#include "envoy/event/dispatcher.h"
#include "envoy/event/range_timer.h"
#include "envoy/event/timer.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Event {

/**
 * Class for creating RangeTimer objects that can be adjusted towards either the minimum or maximum
 * of their range by the owner of the manager object.
 */
class ScaledRangeTimerManager {
public:
  explicit ScaledRangeTimerManager(Dispatcher& dispatcher);
  ~ScaledRangeTimerManager();

  /**
   * Creates a new range timer backed by the manager. The returned timer will be subject to the
   * current and future scale factor values set on the manager. All returned timers must be deleted
   * before the manager.
   */
  RangeTimerPtr createTimer(TimerCb callback);

  /**
   * Sets the scale factor for all timers created through this manager. The value should be between
   * 0 and 1, inclusive.
   */
  void setScaleFactor(double scale_factor);

private:
  class RangeTimerImpl;
  struct Queue {
    struct Item {
      Item(RangeTimerImpl& timer, MonotonicTime active_time);
      // The timer owned by the caller being kept in the queue.
      RangeTimerImpl& timer_;
      // The time at which the timer became active (when its min duration expired).
      MonotonicTime active_time_;
    };

    using Iterator = std::list<Item>::iterator;

    Queue(std::chrono::milliseconds duration, ScaledRangeTimerManager& manager,
          Dispatcher& dispatcher);

    const std::chrono::milliseconds duration_;
    // The list of active timers in this queue. This is implemented as a
    // std::list so that the iterators held in ScalingTimerHandle instances are
    // not invalidated by removal or insertion of other timers. The timers in
    // the list are in sorted order by active_time_ because they are only
    // inserted at the end of the list, and the time is monotonically increasing.
    std::list<Item> range_timers_;
    const TimerPtr timer_;
  };

  struct ScalingTimerHandle {
    ScalingTimerHandle(Queue& queue, Queue::Iterator iterator);
    Queue& queue_;
    Queue::Iterator iterator_;
  };

  // A simple wrapper around a float that ensures value() is sane (in the range [0, 1]).
  class DurationScaleFactor {
  public:
    DurationScaleFactor(double value);
    double value() const { return value_; }

  private:
    double value_;
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
                                          DurationScaleFactor scale_factor);

  ScalingTimerHandle activateTimer(std::chrono::milliseconds duration, RangeTimerImpl& timer);

  void removeTimer(ScalingTimerHandle handle);

  void resetQueueTimer(Queue& queue, MonotonicTime now);

  void onQueueTimerFired(Queue& queue);

  Dispatcher& dispatcher_;
  DurationScaleFactor scale_factor_;
  absl::flat_hash_set<std::unique_ptr<Queue>, Hash, Eq> queues_;
};

} // namespace Event
} // namespace Envoy