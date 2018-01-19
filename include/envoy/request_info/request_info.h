#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/optional.h"
#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/http/protocol.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {

namespace Router {
class RouteEntry;
} // namespace Router

namespace RequestInfo {

enum ResponseFlag {
  // Local server healthcheck failed.
  FailedLocalHealthCheck = 0x1,
  // No healthy upstream.
  NoHealthyUpstream = 0x2,
  // Request timeout on upstream.
  UpstreamRequestTimeout = 0x4,
  // Local codec level reset was sent on the stream.
  LocalReset = 0x8,
  // Remote codec level reset was received on the stream.
  UpstreamRemoteReset = 0x10,
  // Local reset by a connection pool due to an initial connection failure.
  UpstreamConnectionFailure = 0x20,
  // If the stream was locally reset due to connection termination.
  UpstreamConnectionTermination = 0x40,
  // The stream was reset because of a resource overflow.
  UpstreamOverflow = 0x80,
  // No route found for a given request.
  NoRouteFound = 0x100,
  // Request was delayed before proxying.
  DelayInjected = 0x200,
  // Abort with error code was injected.
  FaultInjected = 0x400,
  // Request was ratelimited locally by rate limit filter.
  RateLimited = 0x800,
  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST FLAG.
  LastFlag = RateLimited
};

/**
 * Additional information about a completed request for logging.
 * TODO(mattklein123): This interface needs a thorough cleanup in terms of how we handle time
 *                     durations. I will be following up with a dedicated change for this.
 */
class RequestInfo {
public:
  virtual ~RequestInfo() {}

  /**
   * Each filter can set independent response flags. The flags are accumulated.
   */
  virtual void setResponseFlag(ResponseFlag response_flag) PURE;

  /**
   * Filters can trigger this callback when an upstream host has been selected.
   */
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * @return the time that the first byte of the request was received.
   */
  virtual SystemTime startTime() const PURE;

  /**
   * @return duration from request start to when the entire request was received from the
   * downstream client in microseconds.
   */
  virtual const Optional<std::chrono::microseconds>& requestReceivedDuration() const PURE;

  /**
   * Set the duration from request start to when the entire request was received from the
   * downstream client.
   * @param time monotonic clock time when the response was received.
   */
  virtual void requestReceivedDuration(MonotonicTime time) PURE;

  /**
   * @return the duration from request start to when the first byte of the response was received
   * from the upstream host in microseconds.
   */
  virtual const Optional<std::chrono::microseconds>& responseReceivedDuration() const PURE;

  /**
   * Set the duration from request start to when the first byte of the response was received
   * from the upstream host in microseconds.
   * @param time monotonic clock time when the first byte of the response was received.
   */
  virtual void responseReceivedDuration(MonotonicTime time) PURE;

  /**
   * @return the # of body bytes received in the request.
   */
  virtual uint64_t bytesReceived() const PURE;

  /**
   * @return the protocol of the request.
   */
  virtual const Optional<Http::Protocol>& protocol() const PURE;

  /**
   * Set the request's protocol.
   */
  virtual void protocol(Http::Protocol protocol) PURE;

  /**
   * @return the response code.
   */
  virtual const Optional<uint32_t>& responseCode() const PURE;

  /**
   * @return the # of body bytes sent in the response.
   */
  virtual uint64_t bytesSent() const PURE;

  /**
   * @return the microseconds duration of the first byte received to the last byte sent.
   */
  virtual std::chrono::microseconds duration() const PURE;

  /**
   * @return whether response flag is set or not.
   */
  virtual bool getResponseFlag(ResponseFlag response_flag) const PURE;

  /**
   * @return upstream host description.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() const PURE;

  /**
   * Get the upstream local address.
   */
  virtual const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const PURE;

  /**
   * @return whether the request is a health check request or not.
   */
  virtual bool healthCheck() const PURE;

  /**
   * Set whether the request is a health check request or not.
   */
  virtual void healthCheck(bool is_hc) PURE;

  /**
   * Get the downstream local address. Note that this will never be nullptr.
   */
  virtual const Network::Address::InstanceConstSharedPtr& downstreamLocalAddress() const PURE;

  /**
   * Get the downstream remote address. Note that this will never be nullptr. Additionally note
   * that this may not be the address of the physical connection if for example the address was
   * inferred from proxy proto, x-forwarded-for, etc.
   */
  virtual const Network::Address::InstanceConstSharedPtr& downstreamRemoteAddress() const PURE;

  /**
   * @return const Router::RouteEntry* Get the route entry selected for this request. Note: this
   * will be nullptr if no route was selected.
   */
  virtual const Router::RouteEntry* routeEntry() const PURE;
};

} // namespace RequestInfo
} // namespace Envoy
