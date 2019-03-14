#include "common/event/libevent_scheduler.h"

#include "common/common/assert.h"
#include "common/event/timer_impl.h"

namespace Envoy {
namespace Event {

LibeventScheduler::LibeventScheduler() : libevent_(event_base_new()) {
  // The dispatcher won't work as expected if libevent hasn't been configured to use threads.
  RELEASE_ASSERT(Libevent::Global::initialized(), "");
}

TimerPtr LibeventScheduler::createTimer(const TimerCb& cb) {
  return std::make_unique<TimerImpl>(libevent_, cb);
};

void LibeventScheduler::nonBlockingLoop() { event_base_loop(libevent_.get(), EVLOOP_NONBLOCK); }

void LibeventScheduler::blockingLoop() { event_base_loop(libevent_.get(), 0); }

void LibeventScheduler::loopExit() { event_base_loopexit(libevent_.get(), nullptr); }

} // namespace Event
} // namespace Envoy
