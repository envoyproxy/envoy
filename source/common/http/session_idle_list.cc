#include "source/common/http/session_idle_list.h"

#include <algorithm>
#include <cstddef>

#include "envoy/common/time.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/http/session_idle_list_interface.h"

#include "absl/time/time.h"

namespace Envoy {
namespace Http {

void SessionIdleList::AddSession(IdleSessionInterface& session) {
  idle_sessions_.AddSessionToList(dispatcher_.approximateMonotonicTime(), session);
}

void SessionIdleList::RemoveSession(IdleSessionInterface& session) {
  idle_sessions_.RemoveSessionFromList(session);
}

void SessionIdleList::MaybeTerminateIdleSessions(bool is_saturated) {
  const size_t max_sessions_to_terminate =
      std::min(MaxSessionsToTerminateInOneRound(is_saturated), idle_sessions_.size());
  size_t num_terminated;
  for (num_terminated = 0; num_terminated < max_sessions_to_terminate; ++num_terminated) {
    IdleSessionInterface& next_session = idle_sessions_.next_session_to_terminate();
    absl::Duration time_since_enqueue = absl::FromChrono(
        dispatcher_.approximateMonotonicTime() - idle_sessions_.GetEnqueueTime(next_session));
    // If the resource pressure is scaling but not saturated, we should respect
    // the min_time_before_termination_allowed_ and only terminate connections
    // that have been idle longer than the threshold.
    if (!is_saturated && time_since_enqueue < min_time_before_termination_allowed_) {
      break;
    }
    next_session.TerminateIdleSession();
    // The class implementing IdleSessionInterface may or may not remove itself
    // from the idle list. We remove it here to be sure.
    idle_sessions_.RemoveSessionFromList(next_session);
  }
  ENVOY_LOG(debug, "Terminated {} idle sessions.", num_terminated);
};

size_t SessionIdleList::MaxSessionsToTerminateInOneRound(bool is_saturated) const {
  return is_saturated ? max_sessions_to_terminate_in_one_round_when_saturated_
                      : max_sessions_to_terminate_in_one_round_;
};

absl::Duration SessionIdleList::MinTimeBeforeTerminationAllowed() const {
  return min_time_before_termination_allowed_;
};

void SessionIdleList::IdleSessions::AddSessionToList(MonotonicTime enqueue_time,
                                                     IdleSessionInterface& session) {
  if (map_.find(&session) != map_.end()) {
    IS_ENVOY_BUG("Session is already on the idle list.");
    return;
  }

  auto [iter, added] = set_.emplace(SessionInfo(session, enqueue_time));
  if (added) {
    map_.emplace(&session, *iter);
  } else {
    ENVOY_BUG(added, "Attempt to add session which is already in the idle set.");
  }
}

void SessionIdleList::IdleSessions::RemoveSessionFromList(IdleSessionInterface& session) {
  auto it = map_.find(&session);
  if (it != map_.end()) {
    set_.erase(it->second);
    map_.erase(it);
  }
}

MonotonicTime SessionIdleList::IdleSessions::GetEnqueueTime(IdleSessionInterface& session) const {
  auto it = map_.find(&session);
  if (it != map_.end()) {
    return it->second.enqueue_time;
  }
  IS_ENVOY_BUG("Attempt to get enqueue time for session which is not in the idle set.");
  return MonotonicTime{};
}

} // namespace Http
} // namespace Envoy
