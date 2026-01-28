#include "source/common/http/session_idle_list.h"

#include <algorithm>
#include <cstddef>

#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "envoy/common/time.h"
#include "source/common/common/assert.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Http {

void SessionIdleList::AddSession(IdleSessionInterface& session) {
  idle_sessions_.AddSessionToList(dispatcher_.approximateMonotonicTime(), session);
}

void SessionIdleList::RemoveSession(IdleSessionInterface& session) {
  idle_sessions_.RemoveSessionFromList(session);
}

void SessionIdleList::MaybeTerminateIdleSessions() {
  const int max_sessions_to_terminate = MaxSessionsToTerminateInOneRound();
  size_t num_terminated;
  for (num_terminated = 0;
       num_terminated <
       std::min(idle_sessions_.size(), static_cast<size_t>(max_sessions_to_terminate));
       ++num_terminated) {
    IdleSessionInterface& next_session = idle_sessions_.next_session_to_terminate();
    absl::Duration time_since_enqueue = absl::FromChrono(
        dispatcher_.approximateMonotonicTime() - idle_sessions_.GetEnqueueTime(next_session));
    if (!ignore_min_time_before_termination_allowed_ &&
        time_since_enqueue < min_time_before_termination_allowed_) {
      break;
    }
    next_session.TerminateIdleSession();
    idle_sessions_.RemoveSessionFromList(next_session);
  }
  ENVOY_LOG(debug, "Terminated {} idle sessions.", num_terminated);
};

void SessionIdleList::IdleSessions::AddSessionToList(MonotonicTime enqueue_time,
                                                     IdleSessionInterface& session) {
  if (map_.find(&session) != map_.end()) {
    ENVOY_LOG(debug, "Session {} is already on the idle list.", static_cast<void*>(&session));
    ResetSessionEnqueueTime(enqueue_time, session);
    return;
  }

  auto [iter, added] = set_.emplace(SessionInfo(session, enqueue_time));
  if (!added) {
    IS_ENVOY_BUG(absl::StrFormat("Attempt to add session %p which is already in the idle set.",
                                 static_cast<void*>(&session)));
  }
  map_[&session] = *iter;
}

void SessionIdleList::IdleSessions::RemoveSessionFromList(IdleSessionInterface& session) {
  typename IdleSessionMap::iterator it = map_.find(&session);
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
  return MonotonicTime{}; // Should not happen if session is in list
}

void SessionIdleList::IdleSessions::ResetSessionEnqueueTime(MonotonicTime enqueue_time,
                                                            IdleSessionInterface& session) {
  this->RemoveSessionFromList(session);
  this->AddSessionToList(enqueue_time, session);
}

} // namespace Http
} // namespace Envoy