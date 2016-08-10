#include "libevent.h"

#include "common/common/assert.h"

#include "event2/thread.h"

namespace Event {
namespace Libevent {

const int Global::DNS_SIGNAL_ID = SIGRTMIN;

void Global::initialize() {
  evthread_use_pthreads();

  // Ignore SIGPIPE and allow errors to propagate through error codes.
  signal(SIGPIPE, SIG_IGN);

  // Block the DNS signal so we can use it with signalfd().
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, DNS_SIGNAL_ID);

  int rc = pthread_sigmask(SIG_BLOCK, &mask, nullptr);
  RELEASE_ASSERT(-1 != rc);
  UNREFERENCED_PARAMETER(rc);
}

} // Libevent
} // Event
