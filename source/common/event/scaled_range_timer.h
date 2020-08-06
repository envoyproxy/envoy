#include <chrono>

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
  ScaledRangeTimerManager(
      Dispatcher& dispatcher, float scale_factor,
      std::chrono::milliseconds minimum_duration = std::chrono::milliseconds(100));

  /**
   * Creates a new range timer backed by the manager. The returned timer will be subject to the
   * current and future scale factor values set on the manager. All returned timers must be deleted
   * before the manager.
   */
  RangeTimerPtr createTimer(TimerCb callback);

  /**
   * Sets the scale factor for all timers created through this manager.
   */
  void setScaleFactor(float scale_factor);

protected:
  class RangeTimerImpl;

  struct BucketedEnabledTimer {
    BucketedEnabledTimer(RangeTimerImpl& timer, MonotonicTime latest_trigger_time)
        : timer(timer), latest_trigger_time(latest_trigger_time) {}
    RangeTimerImpl& timer;
    MonotonicTime latest_trigger_time;
  };

  using BucketHandle = int;
  using BucketEnabledList = std::list<BucketedEnabledTimer>;

  void enqueuePendingTimer(RangeTimerImpl& timer);
  void disablePendingTimer(RangeTimerImpl& timer);

  BucketEnabledList::iterator add(RangeTimerImpl& timer, BucketHandle bucket_handle);
  void disableActiveTimer(BucketHandle bucket_handle,
                          const BucketEnabledList::iterator& bucket_position);

  BucketHandle getBucketForDuration(std::chrono::milliseconds duration) const;
  std::chrono::milliseconds getBucketDuration(BucketHandle bucket_index) const;

private:
  struct Bucket {
    TimerPtr timer;
    BucketEnabledList scaled_timers;
    void updateTimer(ScaledRangeTimerManager& manager, bool scale_factor_changed);
  };

  class DurationScaleFactor {
  public:
    DurationScaleFactor(float value);
    float value() const;

  private:
    float value_;
  };

  static constexpr int kBucketScaleFactor = 2;

  Bucket& getOrCreateBucket(BucketHandle handle);
  void onBucketTimer(int index);

  Dispatcher& dispatcher_;
  const std::chrono::milliseconds minimum_duration_;

  DurationScaleFactor scale_factor_;
  std::vector<Bucket> buckets_;
};

} // namespace Event
} // namespace Envoy
