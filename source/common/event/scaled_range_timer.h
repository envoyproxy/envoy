#include <chrono>

#include "envoy/event/dispatcher.h"
#include "envoy/event/range_timer.h"
#include "envoy/event/timer.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Event {

class ScaledRangeTimerManager {
public:
  ScaledRangeTimerManager(
      Dispatcher& dispatcher, float scale_factor,
      std::chrono::milliseconds minimum_duration = std::chrono::milliseconds(100));

  RangeTimerPtr createTimer(TimerCb callback);

  void setScaleFactor(float scale_factor);

protected:
  class RangeTimerImpl;

  struct BucketedEnabledTimer {
    BucketedEnabledTimer(RangeTimerImpl& timer, MonotonicTime latest_trigger_time)
        : timer(timer), latest_trigger_time(latest_trigger_time) {}
    RangeTimerImpl& timer;
    MonotonicTime latest_trigger_time;
  };

  using BucketEnabledList = std::list<BucketedEnabledTimer>;

  void enqueuePendingTimer(RangeTimerImpl& timer);
  void disablePendingTimer(RangeTimerImpl& timer);

  BucketEnabledList::iterator add(RangeTimerImpl& timer, MonotonicTime::duration max_duration);
  void disableActiveTimer(MonotonicTime::duration max_duration,
                          const BucketEnabledList::iterator& bucket_position);

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
  static MonotonicTime::duration getBucketedDuration(MonotonicTime::duration duration);

  Bucket& getOrCreateBucket(MonotonicTime::duration max_duration);
  void onBucketTimer(int index);

  Dispatcher& dispatcher_;
  const std::chrono::milliseconds minimum_duration_;

  DurationScaleFactor scale_factor_;
  std::vector<Bucket> buckets_;
};

} // namespace Event
} // namespace Envoy
