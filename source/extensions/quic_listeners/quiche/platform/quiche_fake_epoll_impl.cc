// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include "extensions/quic_listeners/quiche/platform/quiche_fake_epoll_impl.h"

namespace quiche {

FakeTimeEpollServer::FakeTimeEpollServer() : now_in_usec_(0) {}

FakeTimeEpollServer::~FakeTimeEpollServer() = default;

int64_t FakeTimeEpollServer::NowInUsec() const { return now_in_usec_; }

FakeEpollServer::FakeEpollServer() : until_in_usec_(-1) {}

FakeEpollServer::~FakeEpollServer() = default;

int FakeEpollServer::epoll_wait_impl(int /*epfd*/, struct epoll_event* events, int max_events,
                                     int timeout_in_ms) {
  int num_events = 0;
  while (!event_queue_.empty() && num_events < max_events &&
         event_queue_.begin()->first <= NowInUsec() &&
         ((until_in_usec_ == -1) || (event_queue_.begin()->first < until_in_usec_))) {
    int64_t event_time_in_usec = event_queue_.begin()->first;
    events[num_events] = event_queue_.begin()->second;
    if (event_time_in_usec > NowInUsec()) {
      set_now_in_usec(event_time_in_usec);
    }
    event_queue_.erase(event_queue_.begin());
    ++num_events;
  }
  if (num_events == 0) {      // then we'd have waited 'till the timeout.
    if (until_in_usec_ < 0) { // then we don't care what the final time is.
      if (timeout_in_ms > 0) {
        AdvanceBy(timeout_in_ms * 1000);
      }
    } else { // except we assume that we don't wait for the timeout
      // period if until_in_usec_ is a positive number.
      set_now_in_usec(until_in_usec_);
      // And reset until_in_usec_ to signal no waiting (as
      // the AdvanceByExactly* stuff is meant to be one-shot,
      // as are all similar net::EpollServer functions)
      until_in_usec_ = -1;
    }
  }
  QUIC_LOG_IF_IMPL(FATAL, until_in_usec_ >= 0 && until_in_usec_ >= NowInUsec());
  return num_events;
}

} // namespace quiche
