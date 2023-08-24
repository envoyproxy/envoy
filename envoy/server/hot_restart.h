#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/allocator.h"
#include "envoy/stats/store.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Server {

class Instance;

/**
 * Abstracts functionality required to "hot" (live) restart the server including code and
 * configuration. Right now this interface assumes a UNIX like socket interface for fd passing
 * but it could be relatively easily swapped with something else if necessary.
 */
class HotRestart {
public:
  struct ServerStatsFromParent {
    uint64_t parent_memory_allocated_ = 0;
    uint64_t parent_connections_ = 0;
  };

  struct AdminShutdownResponse {
    time_t original_start_time_;
    bool enable_reuse_port_default_;
  };

  virtual ~HotRestart() = default;

  /**
   * Shutdown listeners in the parent process if applicable. Listeners will begin draining to
   * clear out old connections.
   */
  virtual void drainParentListeners() PURE;

  /**
   * Retrieve a listening socket on the specified address from the parent process. The socket will
   * be duplicated across process boundaries.
   * @param address supplies the address of the socket to duplicate, e.g. tcp://127.0.0.1:5000.
   * @param worker_index supplies the socket/worker index to fetch. When using reuse_port sockets
   *        each socket is fetched individually to ensure no connection loss.
   * @return int the fd or -1 if there is no bound listen port in the parent.
   */
  virtual int duplicateParentListenSocket(const std::string& address, uint32_t worker_index) PURE;

  /**
   * Initialize the parent logic of our restarter. Meant to be called after initialization of a
   * new child has begun. The hot restart implementation needs to be created early to deal with
   * shared memory, logging, etc. so late initialization of needed interfaces is done here.
   */
  virtual void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) PURE;

  /**
   * Shutdown admin processing in the parent process if applicable. This allows admin processing
   * to start up in the new process.
   * @return response if the parent is alive.
   */
  virtual absl::optional<AdminShutdownResponse> sendParentAdminShutdownRequest() PURE;

  /**
   * Tell our parent process to gracefully terminate itself.
   */
  virtual void sendParentTerminateRequest() PURE;

  /**
   * Retrieve stats from our parent process and merges them into stats_store, taking into account
   * the stats values we've already seen transferred.
   * Skips all of the above and returns 0s if there is not currently a parent.
   * @param stats_store the store whose stats will be updated.
   * @param stats_proto the stats values we are updating with.
   * @return special values relating to the "server" stats scope, whose
   *         merging has to be handled by Server::InstanceImpl.
   */
  virtual ServerStatsFromParent mergeParentStatsIfAny(Stats::StoreRoot& stats_store) PURE;

  /**
   * Shutdown the half of our hot restarter that acts as a parent.
   */
  virtual void shutdown() PURE;

  /**
   * Return the base id used to generate a domain socket name.
   */
  virtual uint32_t baseId() PURE;

  /**
   * Return the hot restart compatibility version so that operations code can decide whether to
   * perform a full or hot restart.
   */
  virtual std::string version() PURE;

  /**
   * @return Thread::BasicLockable& a lock for logging.
   */
  virtual Thread::BasicLockable& logLock() PURE;

  /**
   * @return Thread::BasicLockable& a lock for access logs.
   */
  virtual Thread::BasicLockable& accessLogLock() PURE;
};

/**
 * HotRestartDomainSocketInUseException is thrown during HotRestart construction only when the
 * underlying domain socket is in use.
 */
class HotRestartDomainSocketInUseException : public EnvoyException {
public:
  HotRestartDomainSocketInUseException(const std::string& what) : EnvoyException(what) {}
};

} // namespace Server
} // namespace Envoy
