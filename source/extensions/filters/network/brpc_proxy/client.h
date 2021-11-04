#pragma once

#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"
#include "envoy/event/dispatcher.h"

#include "source/extensions/filters/network/brpc_proxy/message.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace BrpcProxy {

/**
 * A single redis client connection.
 */
class Client:public Event::DeferredDeletable {
public:
  virtual ~Client() = default;

  /**
   * Adds network connection callbacks to the underlying network connection.
   */
  virtual void addConnectionCallbacks(Network::ConnectionCallbacks& callbacks) PURE;

  /**
   * Called to determine if the client has pending requests.
   * @return bool true if the client is processing requests or false if it is currently idle.
   */
  virtual bool active() PURE;

  /**
   * Closes the underlying network connection.
   */
  virtual void close() PURE;

  virtual BrpcRequest* makeRequest(BrpcMessage& request, PoolCallbacks& callbacks) PURE;
};

using ClientPtr = std::unique_ptr<Client>;

/**
 * A factory for individual  client connections.
 */
class ClientFactory {
public:
  virtual ~ClientFactory() = default;

  /**
   * Create a client given an upstream host.
   * @param host supplies the upstream host.
   * @param dispatcher supplies the owning thread's dispatcher.
   * @return ClientPtr a new connection pool client.
   */
  virtual ClientPtr create(Upstream::HostConstSharedPtr host, Event::Dispatcher& dispatcher) PURE;
};

} // namespace BrpcProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy


