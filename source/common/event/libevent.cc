#include "common/event/libevent.h"

#include <signal.h>

#include "common/common/assert.h"

#include "event2/thread.h"

namespace Envoy {
namespace Event {
namespace Libevent {

bool Global::initialized_ = false;

void Global::initialize() {
  evthread_use_pthreads();

  // Ignore SIGPIPE and allow errors to propagate through error codes.
  signal(SIGPIPE, SIG_IGN);
  initialized_ = true;
}

} // namespace Libevent
} // namespace Event
} // namespace Envoy
