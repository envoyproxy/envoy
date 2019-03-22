#pragma once

// NOLINT(namespace-envoy)
//
// This file is part of the QUICHE platform implementation, and is not to be
// consumed or referenced directly by other Envoy code. It serves purely as a
// porting layer for QUICHE.

#include <stddef.h>
#include <stdint.h>

#include <unordered_map>
#include <unordered_set>

#include "extensions/quic_listeners/quiche/platform/quiche_epoll_impl.h"

namespace quiche {

// This fake one only lies about the time but lets fd events operate normally.
class FakeTimeEpollServer : public EpollServer {
public:
  FakeTimeEpollServer();
  ~FakeTimeEpollServer() override;

  // Replaces the net::EpollServer NowInUsec.
  int64_t NowInUsec() const override;

  void set_now_in_usec(int64_t nius) { now_in_usec_ = nius; }

  // Advances the virtual 'now' by advancement_usec.
  void AdvanceBy(int64_t advancement_usec) { set_now_in_usec(NowInUsec() + advancement_usec); }

  // Advances the virtual 'now' by advancement_usec, and
  // calls WaitForEventAndExecteCallbacks.
  // Note that the WaitForEventsAndExecuteCallbacks invocation
  // may cause NowInUs to advance beyond what was specified here.
  // If that is not desired, use the AdvanceByExactly calls.
  void AdvanceByAndWaitForEventsAndExecuteCallbacks(int64_t advancement_usec) {
    AdvanceBy(advancement_usec);
    WaitForEventsAndExecuteCallbacks();
  }

private:
  int64_t now_in_usec_;
};

class FakeEpollServer : public FakeTimeEpollServer {
public: // type definitions
  using EventQueue = std::unordered_multimap<int64_t, struct epoll_event>;

  FakeEpollServer();
  ~FakeEpollServer() override;

  // time_in_usec is the time at which the event specified
  // by 'ee' will be delivered. Note that it -is- possible
  // to add an event for a time which has already been passed..
  // .. upon the next time that the callbacks are invoked,
  // all events which are in the 'past' will be delivered.
  void AddEvent(int64_t time_in_usec, const struct epoll_event& ee) {
    event_queue_.insert(std::make_pair(time_in_usec, ee));
  }

  // Advances the virtual 'now' by advancement_usec,
  // and ensure that the next invocation of
  // WaitForEventsAndExecuteCallbacks goes no farther than
  // advancement_usec from the current time.
  void AdvanceByExactly(int64_t advancement_usec) {
    until_in_usec_ = NowInUsec() + advancement_usec;
    set_now_in_usec(NowInUsec() + advancement_usec);
  }

  // As above, except calls WaitForEventsAndExecuteCallbacks.
  void AdvanceByExactlyAndCallCallbacks(int64_t advancement_usec) {
    AdvanceByExactly(advancement_usec);
    WaitForEventsAndExecuteCallbacks();
  }

  std::unordered_set<AlarmCB*>::size_type NumberOfAlarms() const { return all_alarms_.size(); }

protected: // functions
  // These functions do nothing here, as we're not actually
  // using the epoll_* syscalls.
  void DelFD(int /*fd*/) const override {}
  void AddFD(int /*fd*/, int /*event_mask*/) const override {}
  void ModFD(int /*fd*/, int /*event_mask*/) const override {}

  // Replaces the epoll_server's epoll_wait_impl.
  int epoll_wait_impl(int epfd, struct epoll_event* events, int max_events,
                      int timeout_in_ms) override;
  void SetNonblocking(int /*fd*/) override {}

private: // members
  EventQueue event_queue_;
  int64_t until_in_usec_;
};

} // namespace quiche
