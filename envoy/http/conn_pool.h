#pragma once

#include <functional>
#include <memory>

#include "envoy/common/conn_pool.h"
#include "envoy/common/pure.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/http/codec.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Http {
namespace ConnectionPool {

using PoolFailureReason = ::Envoy::ConnectionPool::PoolFailureReason;
using Cancellable = ::Envoy::ConnectionPool::Cancellable;

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
   * @param info supplies the stream info object associated with the upstream L4 connection.
   * @param protocol supplies the protocol associated with the stream, or absl::nullopt for raw TCP.
   */
  virtual void onPoolReady(RequestEncoder& encoder, Upstream::HostDescriptionConstSharedPtr host,
                           StreamInfo::StreamInfo& info,
                           absl::optional<Http::Protocol> protocol) PURE;
};

class Instance;

/**
 * Pool callbacks invoked to track the lifetime of connections in the pool.
 */
class ConnectionLifetimeCallbacks {
public:
  virtual ~ConnectionLifetimeCallbacks() = default;

  /**
   * Called when a connection is open for requests in a pool.
   * @param pool which the connection is associated with.
   * @param hash_key the hash key used for this connection.
   * @param connection newly open connection.
   */
  virtual void onConnectionOpen(Instance& pool, std::vector<uint8_t>& hash_key,
                                const Network::Connection& connection) PURE;

  /**
   * Called when a connection is draining and may no longer be used for requests.
   * @param pool which the connection is associated with.
   * @param hash_key the hash key used for this connection.
   * @param connection newly open connection.
   */
  virtual void onConnectionDraining(Instance& pool, std::vector<uint8_t>& hash_key,
                                    const Network::Connection& connection) PURE;
};

/**
 * An instance of a generic connection pool.
 */
class Instance : public Envoy::ConnectionPool::Instance, public Event::DeferredDeletable {
public:
  struct StreamOptions {
    // True if the request can be sent as early data.
    bool can_send_early_data_;
    // True if the request can be sent over HTTP/3.
    bool can_use_http3_;
  };

  ~Instance() override = default;

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
   * @param options specifies how to create the requested stream.
   * @return Cancellable* If no connection is ready, the callback is not invoked, and a handle
   *                      is returned that can be used to cancel the request. Otherwise, one of the
   *                      callbacks is called and the routine returns nullptr. NOTE: Once a callback
   *                      is called, the handle is no longer valid and any further cancellation
   *                      should be done by resetting the stream.
   * @warning Do not call cancel() from the callbacks, as the request is implicitly canceled when
   *          the callbacks are called.
   */
  virtual Cancellable* newStream(Http::ResponseDecoder& response_decoder, Callbacks& callbacks,
                                 const StreamOptions& options) PURE;

  /**
   * Returns a user-friendly protocol description for logging.
   * @return absl::string_view a protocol description for logging.
   */
  virtual absl::string_view protocolDescription() const PURE;
};

using InstancePtr = std::unique_ptr<Instance>;

} // namespace ConnectionPool
} // namespace Http
} // namespace Envoy
