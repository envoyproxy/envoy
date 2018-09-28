#include "common/event/real_time_system.h"

#include <chrono>

#include "common/common/assert.h"
#include "common/event/event_impl_base.h"
#include "common/event/timer_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {
namespace {

class RealScheduler : public Scheduler {
public:
  RealScheduler(Libevent::BasePtr& libevent) : libevent_(libevent) {}
  TimerPtr createTimer(const TimerCb& cb) override {
    return std::make_unique<TimerImpl>(libevent_, cb);
  };

private:
  Libevent::BasePtr& libevent_;
};

} // namespace

SchedulerPtr RealTimeSystem::createScheduler(Libevent::BasePtr& libevent) {
  return std::make_unique<RealScheduler>(libevent);
}

} // namespace Event
} // namespace Envoy
