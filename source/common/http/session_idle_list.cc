#include "source/common/http/session_idle_list.h"

#include "source/common/common/logger.h"

#include "absl/log/check.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Http {

void SessionIdleList::AddSession(IdleSessionInterface* session) {
  if (session != nullptr) {
    idle_sessions_.AddSessionToList(absl::FromChrono(time_system_.systemTime()), session);
  }
}

void SessionIdleList::RemoveSession(IdleSessionInterface* session) {
  if (session != nullptr) {
    idle_sessions_.RemoveSessionFromList(session);
  }
}

void SessionIdleList::MaybeTerminateIdleSessions() {
  const int max_sessions_to_terminate = MaxSessionsToTerminateInOneRound();
  int num_terminated;
  for (num_terminated = 0; num_terminated < max_sessions_to_terminate; ++num_terminated) {
    auto next_session = idle_sessions_.next_session_to_terminate();
    absl::Duration time_since_enqueue =
        absl::FromChrono(time_system_.systemTime()) - idle_sessions_.GetEnqueueTime(next_session);
    if (time_since_enqueue < min_time_before_purge_allowed_) {
      break;
    }
    idle_sessions_.RemoveSessionFromList(next_session);
    next_session->TerminateIdleSession();
  }
  ENVOY_LOG(info, "Terminated {} idle sessions.", num_terminated);
};

void SessionIdleList::IdleSessions::AddSessionToList(absl::Time enqueue_time,
                                                     IdleSessionInterface* session) {
  DCHECK(session != nullptr);
  if (map_.find(session) != map_.end()) {
    ENVOY_LOG(info, "Session {} is already on the idle list.", static_cast<void*>(session));
    ResetSessionEnqueueTime(enqueue_time, session);
    return;
  }

  auto [iter, added] = set_.emplace(SessionInfo(session, enqueue_time));
  if (!added) {
    ENVOY_LOG(info, "Attempt to add session {} which is already in the idle set.",
              static_cast<void*>(session));
  }
  map_[session] = *iter;
  ++size_;
}

void SessionIdleList::IdleSessions::RemoveSessionFromList(IdleSessionInterface* session) {
  DCHECK(session != nullptr);
  typename IdleSessionMap::iterator it = map_.find(session);
  if (it != map_.end()) {
    set_.erase(it->second);
    map_.erase(it);
    --size_;
  } else {
    ENVOY_LOG(info, "Session {} is not on the idle list.", static_cast<void*>(session));
  }
}

absl::Time SessionIdleList::IdleSessions::GetEnqueueTime(IdleSessionInterface* session) const {
  DCHECK(session != nullptr);
  auto it = map_.find(session);
  if (it != map_.end()) {
    return it->second.enqueue_time;
  }
  return absl::UnixEpoch(); // Should not happen if session is in list
}

void SessionIdleList::IdleSessions::ResetSessionEnqueueTime(absl::Time enqueue_time,
                                                            IdleSessionInterface* session) {
  this->RemoveSessionFromList(session);
  this->AddSessionToList(enqueue_time, session);
}

} // namespace Http
} // namespace Envoy
