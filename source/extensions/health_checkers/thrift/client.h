#pragma once

#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

/**
 * Outbound request callbacks.
 */
class ClientCallback : public Network::ConnectionCallbacks {
public:
  /**
   * Called when the client needs a connection
   */
  virtual Upstream::Host::CreateConnectionData createConnection() PURE;

  /**
   * Called when a response is received.
   * @param is_success indicate if the response is a success response
   */
  virtual void onResponseResult(bool is_success) PURE;
};

/**
 * A single thrift client connection.
 */
class Client : public Event::DeferredDeletable {
public:
  /**
   * Initialize the connection.
   */
  virtual void start() PURE;

  /**
   * Send the health check request.
   */
  virtual bool sendRequest() PURE;

  /**
   * Close the underlying network connection.
   */
  virtual void close() PURE;
};

using ClientPtr = std::unique_ptr<Client>;

/**
 * A factory for individual thrift client connections.
 */
class ClientFactory {
public:
  virtual ~ClientFactory() = default;

  /**
   * Create a thrift client.
   * @param callbacks supplies the connection data creation, network connection callbacks
   *                  to the underlying network connection, and thrift response handling.
   * @param transport supplies the type of transport.
   * @param protocol supplies the type of protocol.
   * @param method_name supplies the method name.
   * @param data supplies the connection data.
   * @param seq_id supplies the initial sequence id.
   * @param fixed_seq_id supplies whether we have sequence id fixed or not.
   */
  virtual ClientPtr create(ClientCallback& callbacks,
                           NetworkFilters::ThriftProxy::TransportType transport,
                           NetworkFilters::ThriftProxy::ProtocolType protocol,
                           const std::string& method_name, Upstream::HostSharedPtr host,
                           int32_t seq_id, bool fixed_seq_id) PURE;
};

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
