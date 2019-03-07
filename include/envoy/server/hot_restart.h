#pragma once

#include <cstdint>
#include <string>

#include "envoy/api/v2/core/hot_restart.pb.h"
#include "envoy/common/pure.h"
#include "envoy/event/dispatcher.h"
#include "envoy/stats/stat_data_allocator.h"
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
  virtual std::unique_ptr<envoy::api::v2::core::HotRestartMessage> getParentStats() PURE;

  /**
   * Initialize the parent logic of our restarter. Meant to be called after initialization of a
   * new child has begun. The hot restart implementation needs to be created early to deal with
   * shared memory, logging, etc. so late initialization of needed interfaces is done here.
   */
  virtual void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) PURE;

  /**
   * Shutdown admin processing in the parent process if applicable. This allows admin processing
   * to start up in the new process.
   * @param info will be filled with information from our parent if it can be retrieved.
   */
  virtual void shutdownParentAdmin(ShutdownParentAdminInfo& info) PURE;

  /**
   * Tell our parent process to gracefully terminate itself.
   */
  virtual void terminateParent() PURE;

  /**
   * Merge stats_proto into stats_store, taking into account the stats values we've already
   * seen transferred.
   * @param stats_store the store whose stats will be updated.
   * @param stats_proto the stats values we are updating with.
   */
  virtual void
  mergeParentStats(Stats::StoreRoot& stats_store,
                   const envoy::api::v2::core::HotRestartMessage::Reply::Stats& stats_proto) PURE;

  /**
   * Shutdown the half of our hot restarter that acts as a parent.
   */
  virtual void shutdown() PURE;

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

  /**
   * @returns an allocator for stats.
   */
  virtual Stats::StatDataAllocator& statsAllocator() PURE;
};

} // namespace Server
} // namespace Envoy
