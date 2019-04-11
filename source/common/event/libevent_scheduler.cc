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

void LibeventScheduler::run(Dispatcher::RunType mode) {
  int flag = 0;
  switch (mode) {
  case Dispatcher::RunType::NonBlock:
    flag = EVLOOP_NONBLOCK;
#ifdef WIN32
    // On Windows, EVLOOP_NONBLOCK will cause the libevent event_base_loop to run forever.
    // This is because libevent only supports level triggering on Windows, and so the write
    // event callbacks will trigger every time through the loop. Adding EVLOOP_ONCE ensures the
    // loop will run at most once
    flag |= EVLOOP_NONBLOCK | EVLOOP_ONCE;
#endif
    break;
  case Dispatcher::RunType::Block:
    // The default flags have 'block' behavior. See
    // http://www.wangafu.net/~nickm/libevent-book/Ref3_eventloop.html
    break;
  case Dispatcher::RunType::RunUntilExit:
    flag = EVLOOP_NO_EXIT_ON_EMPTY;
    break;
  }
  event_base_loop(libevent_.get(), flag);
}

void LibeventScheduler::loopExit() { event_base_loopexit(libevent_.get(), nullptr); }

} // namespace Event
} // namespace Envoy
