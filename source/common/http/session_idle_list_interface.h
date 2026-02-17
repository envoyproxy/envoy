#pragma once

namespace Envoy {
namespace Http {

// Interface for an idle session that can be terminated due to overload.
class IdleSessionInterface {
public:
  virtual ~IdleSessionInterface() = default;

  // Terminates the idle session. This is called by the SessionIdleList when
  // the system is overloaded and the session is eligible for termination.
  virtual void TerminateIdleSession() = 0;
};

// Interface for managing a list of idle sessions.
class SessionIdleListInterface {
public:
  virtual ~SessionIdleListInterface() = default;

  // Adds a session to the idle list.
  virtual void AddSession(IdleSessionInterface& session) = 0;

  // Removes a session from the idle list.
  virtual void RemoveSession(IdleSessionInterface& session) = 0;

  // Terminates idle sessions if they are eligible for termination. This is
  // called by the worker thread when the system is overloaded.
  virtual void MaybeTerminateIdleSessions(bool is_saturated) = 0;
};

} // namespace Http
} // namespace Envoy
