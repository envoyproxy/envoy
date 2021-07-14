#pragma once

#include <memory>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/router/router.h"
#include "envoy/tcp/conn_pool.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/metadata.h"
#include "source/extensions/filters/network/thrift_proxy/protocol_converter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

class RateLimitPolicy;

/**
 * RouteEntry is an individual resolved route entry.
 */
class RouteEntry {
public:
  virtual ~RouteEntry() = default;

  /**
   * @return const std::string& the upstream cluster that owns the route.
   */
  virtual const std::string& clusterName() const PURE;

  /**
   * @return MetadataMatchCriteria* the metadata that a subset load balancer should match when
   * selecting an upstream host
   */
  virtual const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const PURE;

  /**
   * @return const RateLimitPolicy& the rate limit policy for the route.
   */
  virtual const RateLimitPolicy& rateLimitPolicy() const PURE;

  /**
   * @return bool should the service name prefix be stripped from the method.
   */
  virtual bool stripServiceName() const PURE;

  /**
   * @return const Http::LowerCaseString& the header used to determine the cluster.
   */
  virtual const Http::LowerCaseString& clusterHeader() const PURE;
};

/**
 * Route holds the RouteEntry for a request.
 */
class Route {
public:
  virtual ~Route() = default;

  /**
   * @return the route entry or nullptr if there is no matching route for the request.
   */
  virtual const RouteEntry* routeEntry() const PURE;
};

using RouteConstSharedPtr = std::shared_ptr<const Route>;

/**
 * The router configuration.
 */
class Config {
public:
  virtual ~Config() = default;

  /**
   * Based on the incoming Thrift request transport and/or protocol data, determine the target
   * route for the request.
   * @param metadata MessageMetadata for the message to route
   * @param random_value uint64_t used to select cluster affinity
   * @return the route or nullptr if there is no matching route for the request.
   */
  virtual RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const PURE;
};

using ConfigConstSharedPtr = std::shared_ptr<const Config>;

/**
 * This interface is used by an upstream request to communicate its state.
 */
class RequestOwner : public ProtocolConverter {
public:
  ~RequestOwner() override = default;

  /**
   * @return ConnectionPool::UpstreamCallbacks& the handler for upstream data.
   */
  virtual Tcp::ConnectionPool::UpstreamCallbacks& upstreamCallbacks() PURE;

  /**
   * @return Buffer::OwnedImpl& the buffer used to serialize the upstream request.
   */
  virtual Buffer::OwnedImpl& buffer() PURE;

  /**
   * @return Event::Dispatcher& the dispatcher used for timers, etc.
   */
  virtual Event::Dispatcher& dispatcher() PURE;

  /**
   * Converts message begin into the right protocol.
   */
  void convertMessageBegin(MessageMetadataSharedPtr metadata) {
    ProtocolConverter::messageBegin(metadata);
  }

  /**
   * Used to update the request size every time bytes are pushed out.
   *
   * @param size uint64_t the value of the increment.
   */
  virtual void addSize(uint64_t size) PURE;

  /**
   * Used to continue decoding if it was previously stopped.
   */
  virtual void continueDecoding() PURE;

  /**
   * Used to reset the downstream connection after an error.
   */
  virtual void resetDownstreamConnection() PURE;

  /**
   * Sends a locally generated response using the provided response object.
   *
   * @param response DirectResponse the response to send to the downstream client
   * @param end_stream if true, the downstream connection should be closed after this response
   */
  virtual void sendLocalReply(const ThriftProxy::DirectResponse& response, bool end_stream) PURE;

  /**
   * Records the duration of the request.
   *
   * @param value uint64_t the value of the duration.
   * @param unit Unit the unit of the duration.
   */
  virtual void recordResponseDuration(uint64_t value, Stats::Histogram::Unit unit) PURE;
};

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
