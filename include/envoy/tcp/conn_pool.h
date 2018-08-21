#pragma once

#include <functional>
#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Tcp {
namespace ConnectionPool {

/**
 * Handle that allows a pending connection request to be canceled before it is completed.
 */
class Cancellable {
public:
  virtual ~Cancellable() {}

  /**
   * Cancel the pending request.
   */
  virtual void cancel() PURE;
};

/**
 * Reason that a pool connection could not be obtained.
 */
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

/*
 * UpstreamCallbacks for connection pool upstream connection callbacks and data. Note that
 * onEvent(Connected) is never triggered since the event always occurs before a ConnectionPool
 * caller is assigned a connection.
 */
class UpstreamCallbacks : public Network::ConnectionCallbacks {
public:
  virtual ~UpstreamCallbacks() {}

  /*
   * Invoked when data is delivered from the upstream connection while the connection is owned by a
   * ConnectionPool::Instance caller.
   * @param data supplies data from the upstream
   * @param end_stream whether the data is the last data frame
   */
  virtual void onUpstreamData(Buffer::Instance& data, bool end_stream) PURE;
};

/**
 * ConnectionState is a base class for connection state maintained across requests. For example, a
 * protocol may maintain a connection-specific request sequence number or negotiate options that
 * affect the behavior of requests for the duration of the connection. A ConnectionState subclass
 * is assigned to the ConnectionData to track this state when the connection is returned to the
 * pool so that the state is available when the connection is re-used for a subsequent request.
 * The ConnectionState assigned to a connection is automatically destroyed when the connection is
 * closed.
 */
class ConnectionState {
public:
  virtual ~ConnectionState() {}
};

typedef std::unique_ptr<ConnectionState> ConnectionStatePtr;

/*
 * ConnectionData wraps a ClientConnection allocated to a caller. Open ClientConnections are
 * released back to the pool for re-use when their containing ConnectionData is destroyed.
 */
class ConnectionData {
public:
  virtual ~ConnectionData() {}

  /**
   * @return the ClientConnection for the connection.
   */
  virtual Network::ClientConnection& connection() PURE;

  /**
   * Sets the ConnectionState for this connection. Any existing ConnectionState is destroyed.
   * @param ConnectionStatePtr&& new ConnectionState for this connection.
   */
  virtual void setConnectionState(ConnectionStatePtr&& state) PURE;

  /**
   * @return T* the current ConnectionState or nullptr if no state is set or if the state's type
   *            is not T.
   */
  template <class T> T* connectionStateTyped() { return dynamic_cast<T*>(connectionState()); }

  /**
   * Sets the ConnectionPool::UpstreamCallbacks for the connection. If no callback is attached,
   * data from the upstream will cause the connection to be closed. Callbacks cease when the
   * connection is released.
   * @param callback the UpstreamCallbacks to invoke for upstream data
   */
  virtual void addUpstreamCallbacks(ConnectionPool::UpstreamCallbacks& callback) PURE;

protected:
  /**
   * @return ConnectionState* pointer to the current ConnectionState or nullptr if not set
   */
  virtual ConnectionState* connectionState() PURE;
};

typedef std::unique_ptr<ConnectionData> ConnectionDataPtr;

/**
 * Pool callbacks invoked in the context of a newConnection() call, either synchronously or
 * asynchronously.
 */
class Callbacks {
public:
  virtual ~Callbacks() {}

  /**
   * Called when a pool error occurred and no connection could be acquired for making the request.
   * @param reason supplies the failure reason.
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onPoolFailure(PoolFailureReason reason,
                             Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * Called when a connection is available to process a request/response. Connections may be
   * released back to the pool for re-use by resetting the ConnectionDataPtr. If the connection is
   * no longer viable for reuse (e.g. due to some kind of protocol error), the underlying
   * ClientConnection should be closed to prevent its reuse.
   *
   * @param conn supplies the connection data to use.
   * @param host supplies the description of the host that will carry the request. For logical
   *             connection pools the description may be different each time this is called.
   */
  virtual void onPoolReady(ConnectionDataPtr&& conn,
                           Upstream::HostDescriptionConstSharedPtr host) PURE;
};

/**
 * An instance of a generic connection pool.
 */
class Instance : public Event::DeferredDeletable {
public:
  virtual ~Instance() {}

  /**
   * Called when a connection pool has been drained of pending requests, busy connections, and
   * ready connections.
   */
  typedef std::function<void()> DrainedCb;

  /**
   * Register a callback that gets called when the connection pool is fully drained. No actual
   * draining is done. The owner of the connection pool is responsible for not creating any
   * new connections.
   */
  virtual void addDrainedCallback(DrainedCb cb) PURE;

  /**
   * Actively drain all existing connection pool connections. This method can be used in cases
   * where the connection pool is not being destroyed, but the caller wishes to make sure that
   * all new requests take place on a new connection. For example, when a health check failure
   * occurs.
   */
  virtual void drainConnections() PURE;

  /**
   * Create a new connection on the pool.
   * @param cb supplies the callbacks to invoke when the connection is ready or has failed. The
   *           callbacks may be invoked immediately within the context of this call if there is a
   *           ready connection or an immediate failure. In this case, the routine returns nullptr.
   * @return Cancellable* If no connection is ready, the callback is not invoked, and a handle
   *                      is returned that can be used to cancel the request. Otherwise, one of the
   *                      callbacks is called and the routine returns nullptr. NOTE: Once a callback
   *                      is called, the handle is no longer valid and any further cancellation
   *                      should be done by resetting the connection.
   */
  virtual Cancellable* newConnection(Callbacks& callbacks) PURE;
};

typedef std::unique_ptr<Instance> InstancePtr;

} // namespace ConnectionPool
} // namespace Tcp
} // namespace Envoy
