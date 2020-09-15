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
  ScaledRangeTimerManager(Dispatcher& dispatcher, double scale_factor);

  /**
   * Creates a new range timer backed by the manager. The returned timer will be subject to the
   * current and future scale factor values set on the manager. All returned timers must be deleted
   * before the manager.
   */
  RangeTimerPtr createTimer(TimerCb callback);

  /**
   * Sets the scale factor for all timers created through this manager.
   */
  void setScaleFactor(double scale_factor);

protected:
  class RangeTimerImpl;
  struct Queue {
    struct Item {
      Item(RangeTimerImpl& timer, MonotonicTime active_time);
      RangeTimerImpl& timer;
      MonotonicTime active_time;
    };

    using Iterator = std::list<Item>::iterator;

    Queue(std::chrono::milliseconds duration, ScaledRangeTimerManager& manager,
          Dispatcher& dispatcher);

    const std::chrono::milliseconds duration;
    std::list<Item> range_timers;
    const TimerPtr timer;
  };

  struct ScalingTimerHandle {
    ScalingTimerHandle(Queue& queue, Queue::Iterator iterator);
    Queue& queue;
    Queue::Iterator iterator;
  };

  ScalingTimerHandle activateTimer(std::chrono::milliseconds duration, RangeTimerImpl& timer);
  void removeTimer(ScalingTimerHandle handle);

private:
  class DurationScaleFactor {
  public:
    DurationScaleFactor(double value);
    double value() const;

  private:
    double value_;
  };

  struct Hash {
    // Magic declaration to allow heterogeneous lookup.
    using is_transparent = void;

    size_t operator()(const std::chrono::milliseconds duration) const {
      return hash(duration.count());
    }
    size_t operator()(const Queue& queue) const { return (*this)(queue.duration); }
    size_t operator()(const std::unique_ptr<Queue>& queue) const { return (*this)(*queue); }
    std::hash<std::chrono::milliseconds::rep> hash;
  };

  struct Eq {
    // Magic declaration to allow heterogeneous lookup.
    using is_transparent = void;

    bool operator()(const std::unique_ptr<Queue>& lhs, std::chrono::milliseconds rhs) const {
      return lhs->duration == rhs;
    }
    bool operator()(const std::unique_ptr<Queue>& lhs, const Queue& rhs) const {
      return (*this)(lhs, rhs.duration);
    }
    bool operator()(const std::unique_ptr<Queue>& lhs, const std::unique_ptr<Queue>& rhs) const {
      return (*this)(lhs, *rhs);
    }
  };

  void resetQueueTimer(Queue& queue, MonotonicTime now);

  void onQueueTimerFired(Queue& queue);

  Dispatcher& dispatcher_;
  DurationScaleFactor scale_factor_;
  absl::flat_hash_set<std::unique_ptr<Queue>, Hash, Eq> queues_;
};

} // namespace Event
} // namespace Envoy