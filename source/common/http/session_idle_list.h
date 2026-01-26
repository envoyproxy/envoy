#pragma once

#include <compare>
#include <cstddef>
#include <tuple>

#include "absl/base/dynamic_annotations.h"
#include "absl/container/btree_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/time/time.h"
#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Http {

// Interface for an idle session that can be terminated due to overload.
class IdleSessionInterface {
 public:
  virtual ~IdleSessionInterface() = default;

  // Terminates the idle session. This is called by the SessionIdleList when
  // the system is overloaded.
  virtual void TerminateIdleSession() = 0;
};

// This class manages a list of idle sessions.
class SessionIdleList : public Logger::Loggable<Logger::Id::http> {
 public:
  explicit SessionIdleList(Event::Dispatcher& dispatcher)
      : dispatcher_(dispatcher) {};
  virtual ~SessionIdleList() = default;

  // Adds a session to the idle list.
  virtual void AddSession(IdleSessionInterface& session);

  // Removes a session from the idle list.
  virtual void RemoveSession(IdleSessionInterface& session);

  // Terminates idle sessions if they are eligible for termination. This is
  // called by the worker thread when the system is overloaded.
  virtual void MaybeTerminateIdleSessions();

  // Sets the minimum time before a session can be terminated.
  void set_min_time_before_purge_allowed(
      absl::Duration min_time_before_purge_allowed) {
    min_time_before_purge_allowed_ = min_time_before_purge_allowed;
  };

  void set_max_sessions_to_terminate_in_one_round(
      int max_sessions_to_terminate_in_one_round) {
    max_sessions_to_terminate_in_one_round_ =
        max_sessions_to_terminate_in_one_round;
  }

  // If this is > 0 then we do not terminate more than that many
  // sessions in a single attempt. This prevents us from doing too
  // much work in a single round. We want a small constant for this.
  int MaxSessionsToTerminateInOneRound() const {
    return ignore_min_time_before_purge_allowed_
               ? max_sessions_to_terminate_in_one_round_ * 10
               : max_sessions_to_terminate_in_one_round_;
  };

  // Returns the minimum time before a session can be terminated.
  absl::Duration MinTimeBeforePurgeAllowed() const {
    return min_time_before_purge_allowed_;
  };

  // Sets whether to ignore the minimum time before a session can be terminated.
  void set_ignore_min_time_before_purge_allowed(bool ignore) {
    ignore_min_time_before_purge_allowed_ = ignore;
  };

  // Returns whether to ignore the minimum time before a session can be
  // terminated.
  bool ignore_min_time_before_purge_allowed() const {
    return ignore_min_time_before_purge_allowed_;
  }

 protected:
  class IdleSessions {
    struct SessionInfo {
      SessionInfo() = default;
      SessionInfo(IdleSessionInterface& _session, MonotonicTime _enqueue_time)
          : session(&_session), enqueue_time(_enqueue_time) {}
      IdleSessionInterface* session = nullptr;
      // The time at which this session was added.
      MonotonicTime enqueue_time{};

      // Sort by enqueue time. Used by `IdleSessionSet` for session order.
      friend std::strong_ordering operator<=>(const SessionInfo& lhs,
                                              const SessionInfo& rhs) {
        return std::forward_as_tuple(lhs.enqueue_time, lhs.session) <=>
               std::forward_as_tuple(rhs.enqueue_time, rhs.session);
      }
    };

    using IdleSessionSet = absl::btree_set<SessionInfo>;
    using IdleSessionMap =
        absl::node_hash_map<IdleSessionInterface*, SessionInfo>;

   public:
    typedef typename IdleSessionSet::const_iterator idle_session_iterator;

    IdleSessions() {
      ABSL_ANNOTATE_BENIGN_RACE(
          &size_,
          "Envoy developers are OK with racy reads from size_ "
          "when processing stats, and don't want to incur "
          "the cost of atomic operations in the serving path");
    }

    // This type is neither copyable nor movable.
    IdleSessions(const IdleSessions&) = delete;
    IdleSessions& operator=(const IdleSessions&) = delete;

    idle_session_iterator begin() const { return set_.begin(); }
    idle_session_iterator end() const { return set_.end(); }

    void ResetSessionEnqueueTime(MonotonicTime enqueue_time,
                                 IdleSessionInterface& session);
    IdleSessionInterface& next_session_to_terminate() {
      return *set_.begin()->session;
    }

    void AddSessionToList(MonotonicTime enqueue_time,
                          IdleSessionInterface& session);

    void RemoveSessionFromList(IdleSessionInterface& session);

    // Get the time at which the session was added to the idle list.
    MonotonicTime GetEnqueueTime(IdleSessionInterface& session) const;

    // Returns true if the session is in the map. For testing only.
    bool ContainsForTest(IdleSessionInterface& session) const {
      return map_.contains(&session);
    }

    size_t size() const { return size_; }

   private:
    // Set of sessions, ordered by enqueue time.
    IdleSessionSet set_;

    // Map of session to its info in the set.
    IdleSessionMap map_;

    size_t size_ = 0;
  };

  IdleSessions* idle_sessions() { return &idle_sessions_; }

 private:
  Event::Dispatcher& dispatcher_;
  // The sessions currently tracked.
  IdleSessions idle_sessions_;
  absl::Duration min_time_before_purge_allowed_ = absl::Minutes(1);
  bool ignore_min_time_before_purge_allowed_ = false;
  int max_sessions_to_terminate_in_one_round_ = 5;
};

}  // namespace Http
}  // namespace Envoy