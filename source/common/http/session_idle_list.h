#pragma once

#include <compare>
#include <cstddef>
#include <tuple>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

#include "source/common/common/logger.h"
#include "source/common/http/session_idle_list_interface.h"

#include "absl/container/btree_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/time/time.h"

namespace Envoy {
namespace Http {

class TestSessionIdleList;

constexpr size_t kMaxSessionsToTerminateInOneRound = 5;
constexpr size_t kMaxSessionsToTerminateInOneRoundWhenSaturated = 50;

// This class manages a list of idle sessions.
class SessionIdleList : public SessionIdleListInterface, public Logger::Loggable<Logger::Id::http> {
public:
  explicit SessionIdleList(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {};
  ~SessionIdleList() override = default;

  // Adds a session to the idle list.
  void AddSession(IdleSessionInterface& session) override;

  // Removes a session from the idle list.
  void RemoveSession(IdleSessionInterface& session) override;

  // Terminates idle sessions if they are eligible for termination. This is
  // called by the worker thread when the system is overloaded.
  void MaybeTerminateIdleSessions(bool is_saturated) override;

  // Sets the minimum time before a session can be terminated.
  void set_min_time_before_termination_allowed(absl::Duration min_time_before_termination_allowed) {
    min_time_before_termination_allowed_ = min_time_before_termination_allowed;
  };

  void set_max_sessions_to_terminate_in_one_round(int max_sessions_to_terminate_in_one_round) {
    max_sessions_to_terminate_in_one_round_ = max_sessions_to_terminate_in_one_round;
  }

  void set_max_sessions_to_terminate_in_one_round_when_saturated(
      int max_sessions_to_terminate_in_one_round_when_saturated) {
    max_sessions_to_terminate_in_one_round_when_saturated_ =
        max_sessions_to_terminate_in_one_round_when_saturated;
  }

  // Sets whether to ignore the minimum time before a session can be terminated.
  void set_ignore_min_time_before_termination_allowed(bool ignore) {
    ignore_min_time_before_termination_allowed_ = ignore;
  };

private:
  friend class TestSessionIdleList;

  class IdleSessions {
    struct SessionInfo {
      SessionInfo() = default;
      SessionInfo(IdleSessionInterface& session, MonotonicTime enqueue_time)
          : session(&session), enqueue_time(enqueue_time) {}
      IdleSessionInterface* session = nullptr;
      // The time at which this session was added.
      MonotonicTime enqueue_time;

      // Sort by enqueue time. Used by `IdleSessionSet` for session order.
      friend std::strong_ordering operator<=>(const SessionInfo& lhs, const SessionInfo& rhs) {
        return std::forward_as_tuple(lhs.enqueue_time, lhs.session) <=>
               std::forward_as_tuple(rhs.enqueue_time, rhs.session);
      }
    };

    using IdleSessionSet = absl::btree_set<SessionInfo>;
    using IdleSessionMap = absl::node_hash_map<IdleSessionInterface*, SessionInfo>;

  public:
    IdleSessions() = default;

    // This type is neither copyable nor movable.
    IdleSessions(const IdleSessions&) = delete;
    IdleSessions& operator=(const IdleSessions&) = delete;

    IdleSessionInterface& next_session_to_terminate() { return *set_.begin()->session; }

    void AddSessionToList(MonotonicTime enqueue_time, IdleSessionInterface& session);

    void RemoveSessionFromList(IdleSessionInterface& session);

    // Get the time at which the session was added to the idle list.
    MonotonicTime GetEnqueueTime(IdleSessionInterface& session) const;

    // Returns true if the session is in the map. For testing only.
    bool ContainsForTest(IdleSessionInterface& session) const { return map_.contains(&session); }

    size_t size() const { return set_.size(); }

  private:
    // Set of sessions, ordered by enqueue time.
    IdleSessionSet set_;

    // Alternate representation of the contents of the IdleSessionInterface list
    // to allow O(1) lookup which is required for efficient removal.
    IdleSessionMap map_;
  };

  const IdleSessions* idle_sessions() const { return &idle_sessions_; }

  // If this is > 0 then we do not terminate more than that many
  // sessions in a single attempt. This prevents us from doing too
  // much work in a single round. We want a small constant for this.
  size_t MaxSessionsToTerminateInOneRound(bool is_saturated) const;

  // Returns the minimum time before a session can be terminated.
  absl::Duration MinTimeBeforeTerminationAllowed() const;

  Event::Dispatcher& dispatcher_;
  // The sessions currently tracked.
  IdleSessions idle_sessions_;
  absl::Duration min_time_before_termination_allowed_ = absl::Minutes(1);
  bool ignore_min_time_before_termination_allowed_ = false;
  size_t max_sessions_to_terminate_in_one_round_ = kMaxSessionsToTerminateInOneRound;
  size_t max_sessions_to_terminate_in_one_round_when_saturated_ =
      kMaxSessionsToTerminateInOneRoundWhenSaturated;
};

} // namespace Http
} // namespace Envoy
