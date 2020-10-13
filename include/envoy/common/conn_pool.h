#pragma once

#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace ConnectionPool {

/**
 * Controls the behavior of a canceled stream.
 */
enum class CancelPolicy {
  // By default, canceled streams allow a pending connection to complete and become
  // available for a future stream.
  Default,
  // When a stream is canceled, closes a pending connection if there will still be sufficient
  // connections to serve pending streams. CloseExcess is largely useful for callers that never
  // re-use connections (e.g. by closing rather than releasing connections). Using CloseExcess in
  // this situation guarantees that no idle connections will be held open by the conn pool awaiting
  // a connection stream.
  CloseExcess,
};

/**
 * Handle that allows a pending connection or stream to be canceled before it is completed.
 */
class Cancellable {
public:
  virtual ~Cancellable() = default;

  /**
   * Cancel the pending connection or stream.
   * @param cancel_policy a CancelPolicy that controls the behavior of this cancellation.
   */
  virtual void cancel(CancelPolicy cancel_policy) PURE;
};

/**
 * An instance of a generic connection pool.
 */
class Instance {
public:
  virtual ~Instance() = default;

  /**
   * Called when a connection pool has been drained of pending streams, busy connections, and
   * ready connections.
   */
  using DrainedCb = std::function<void()>;

  /**
   * Register a callback that gets called when the connection pool is fully drained and kicks
   * off a drain. The owner of the connection pool is responsible for not creating any
   * new streams.
   */
  virtual void addDrainedCallback(DrainedCb cb) PURE;

  /**
   * Actively drain all existing connection pool connections. This method can be used in cases
   * where the connection pool is not being destroyed, but the caller wishes to make sure that
   * all new streams take place on a new connection. For example, when a health check failure
   * occurs.
   */
  virtual void drainConnections() PURE;

  /**
   * @return Upstream::HostDescriptionConstSharedPtr the host for which connections are pooled.
   */
  virtual Upstream::HostDescriptionConstSharedPtr host() const PURE;

  /**
   * Prefetches an upstream connection, if existing connections do not meet both current and
   * anticipated load.
   *
   * @return true if a connection was prefetched, false otherwise.
   */
  virtual bool maybePrefetch(float prefetch_ratio) PURE;
};

enum class PoolFailureReason {
  // A resource overflowed and policy prevented a new connection from being created.
  Overflow,
  // A local connection failure took place while creating a new connection.
  LocalConnectionFailure,
  // A remote connection failure took place while creating a new connection.
  RemoteConnectionFailure,
  // A timeout occurred while creating a new connection.
  Timeout,
};

} // namespace ConnectionPool
} // namespace Envoy
