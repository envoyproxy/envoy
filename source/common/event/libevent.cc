#include "common/event/libevent.h"

#include "common/common/assert.h"

#include "event2/thread.h"

namespace Event {
namespace Libevent {

void Global::initialize() {
  evthread_use_pthreads();

  // Ignore SIGPIPE and allow errors to propagate through error codes.
  signal(SIGPIPE, SIG_IGN);
}

} // Libevent
} // Event
