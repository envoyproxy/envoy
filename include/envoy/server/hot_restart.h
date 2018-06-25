#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats.h"
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
  struct GetParentStatsInfo {
    uint64_t memory_allocated_;
    uint64_t num_connections_;
  };

  struct ShutdownParentAdminInfo {
    time_t original_start_time_;
  };

  virtual ~HotRestart() {}

  /**
   * Shutdown listeners in the parent process if applicable. Listeners will begin draining to
   * clear out old connections.
   */
  virtual void drainParentListeners() PURE;

  /**
   * Retrieve a listening socket on the specified address from the parent process. The socket will
   * be duplicated across process boundaries.
   * @param address supplies the address of the socket to duplicate, e.g. tcp://127.0.0.1:5000.
   * @return int the fd or -1 if there is no bound listen port in the parent.
   */
  virtual int duplicateParentListenSocket(const std::string& address) PURE;

  /**
   * Retrieve stats from our parent process.
   * @param info will be filled with information from our parent if it can be retrieved.
   */
  virtual void getParentStats(GetParentStatsInfo& info) PURE;

  /**
   * Initialize the restarter after primary server initialization begins. The hot restart
   * implementation needs to be created early to deal with shared memory, logging, etc. so
   * late initialization of needed interfaces is done here.
   */
  virtual void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) PURE;

  /**
   * Shutdown admin processing in the parent process if applicable. This allows admin processing
   * to start up in the new process.
   * @param info will be filled with information from our parent if it can be retrieved.
   */
  virtual void shutdownParentAdmin(ShutdownParentAdminInfo& info) PURE;

  /**
   * Tell our parent to gracefully terminate itself.
   */
  virtual void terminateParent() PURE;

  /**
   * Shutdown the hot restarter.
   */
  virtual void shutdown() PURE;

  /**
   * Return the hot restart compatability version so that operations code can decide whether to
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

  /**
   * @returns an allocator for stats.
   */
  virtual Stats::StatDataAllocator& statsAllocator() PURE;
};

} // namespace Server
} // namespace Envoy
