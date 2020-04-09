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
  virtual ~Cancellable() = default;

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
  virtual ~Callbacks() = default;

  /**
   * Called when a pool error occurred and no connection could be acquired for making the request.
   * @param reason supplies the failure reason.
   * @param transport_failure_reason supplies the details of the transport failure reason.
   * @param host supplies the description of the host that caused the failure. This may be nullptr
   *             if no host was involved in the failure (for example overflow).
   */
  virtual void onPoolFailure(PoolFailureReason reason, absl::string_view transport_failure_reason,
                             Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * Called when a connection is available to process a request/response.
   * @param encoder supplies the request encoder to use.
   * @param host supplies the description of the host that will carry the request. For logical
   *             connection pools the description may be different each time this is called.
   * @param info supplies the stream info object associated with the upstream connection.
   */
  virtual void onPoolReady(RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
                           const StreamInfo::StreamInfo& info) PURE;
};

/**
 * An instance of a generic connection pool.
 */
class Instance : public Event::DeferredDeletable {
public:
  ~Instance() override = default;

  /**
   * @return Http::Protocol Reports the protocol in use by this connection pool.
   */
  virtual Http::Protocol protocol() const PURE;

  /**
   * Called when a connection pool has been drained of pending requests, busy connections, and
   * ready connections.
   */
  using DrainedCb = std::function<void()>;

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
   * Determines whether the connection pool is actively processing any requests.
   * @return true if the connection pool has any pending requests or any active requests.
   */
  virtual bool hasActiveConnections() const PURE;

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
   * @warning Do not call cancel() from the callbacks, as the request is implicitly canceled when
   *          the callbacks are called.
   */
  virtual Cancellable* newStream(Http::ResponseDecoder& response_decoder,
                                 Callbacks& callbacks) PURE;

  /**
   * @return Upstream::HostDescriptionConstSharedPtr the host for which connections are pooled.
   */
  virtual Upstream::HostDescriptionConstSharedPtr host() const PURE;
};

using InstancePtr = std::unique_ptr<Instance>;

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
