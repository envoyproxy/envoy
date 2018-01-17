#pragma once

#include <functional>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Http {
namespace ConnectionPool {

/**
 * Handle that allows a pending request to be cancelled before it is bound to a connection.
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
 * Reason that a pool stream could not be obtained.
 */
enum class PoolFailureReason {
  // A resource overflowed and policy prevented a new stream from being created.
  Overflow,
  // A connection failure took place and the stream could not be bound.
  ConnectionFailure
};

/**
 * Pool callbacks invoked in the context of a newStream() call, either synchronously or
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
   * Called when a connection is available to process a request/response.
   * @param encoder supplies the request encoder to use.
   * @param host supplies the description of the host that will carry the request. For logical
   *             connection pools the description may be different each time this is called.
   */
  virtual void onPoolReady(Http::StreamEncoder& encoder,
                           Upstream::HostDescriptionConstSharedPtr host) PURE;
};

/**
 * An instance of a generic connection pool.
 */
class Instance : public Event::DeferredDeletable {
public:
  virtual ~Instance() {}

  /**
   * @return Http::Protocol Reports the protocol in use by this connection pool.
   */
  virtual Http::Protocol protocol() const PURE;

  /**
   * Called when a connection pool has been drained of pending requests, busy connections, and
   * ready connections.
   */
  typedef std::function<void()> DrainedCb;

  /**
   * Register a callback that gets called when the connection pool is fully drained. No actual
   * draining is done. The owner of the connection pool is responsible for not creating any
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
   * Create a new stream on the pool.
   * @param response_decoder supplies the decoder events to fire when the response is
   *                         available.
   * @param cb supplies the callbacks to invoke when the connection is ready or has failed. The
   *           callbacks may be invoked immediately within the context of this call if there is a
   *           ready connection or an immediate failure. In this case, the routine returns nullptr.
   * @return Cancellable* If no connection is ready, the callback is not invoked, and a handle
   *                      is returned that can be used to cancel the request. Otherwise, one of the
   *                      callbacks is called and the routine returns nullptr. NOTE: Once a callback
   *                      is called, the handle is no longer valid and any further cancellation
   *                      should be done by resetting the stream.
   */
  virtual Cancellable* newStream(Http::StreamDecoder& response_decoder, Callbacks& callbacks) PURE;
};

typedef std::unique_ptr<Instance> InstancePtr;

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
