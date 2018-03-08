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
  // Request was unauthorized.
  Unauthorized = 0x1000,
  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST FLAG.
  LastFlag = Unauthorized
};

/**
 * Additional information about a completed request for logging.
 */
class RequestInfo {
public:
  virtual ~RequestInfo() {}

  /**
   * @param response_flag the response flag. Each filter can set independent response flags. The
   * flags are accumulated.
   */
  virtual void setResponseFlag(ResponseFlag response_flag) PURE;

  /**
   * @param host the selected upstream host for the request.
   */
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * @return the number of body bytes received in the request.
   */
  virtual uint64_t bytesReceived() const PURE;

  /**
   * @return the protocol of the request.
   */
  virtual Optional<Http::Protocol> protocol() const PURE;

  /**
   * @param protocol the request's protocol.
   */
  virtual void protocol(Http::Protocol protocol) PURE;

  /**
   * @return the response code.
   */
  virtual Optional<uint32_t> responseCode() const PURE;

  /**
   * @return the time that the first byte of the request was received.
   */
  virtual SystemTime startTime() const PURE;

  /**
   * @return the monotonic time that the first byte of the request was received. Duration
  calculations should be made relative to this value.
  */
  virtual MonotonicTime startTimeMonotonic() const PURE;

  /**
   * @return the duration between the last byte of the request was received and the start of the
   * request.
   */
  virtual Optional<std::chrono::nanoseconds> lastDownstreamRxByteReceived() const PURE;

  /**
   * Sets the time when the last byte of the request was received.
   */
  virtual void onLastDownstreamRxByteReceived() PURE;

  /**
   * @return the duration between the first byte of the request was sent upstream and the start of
   * the request. There may be a considerable delta between lastDownstreamByteReceived and this
   * value due to filters.
   */
  virtual Optional<std::chrono::nanoseconds> firstUpstreamTxByteSent() const PURE;

  /**
   * Sets the time when the first byte of the request was sent upstream.
   */
  virtual void onFirstUpstreamTxByteSent() PURE;

  /**
   * @return the duration between the last bye of the request was sent upstream and the start of the
   * request.
   */
  virtual Optional<std::chrono::nanoseconds> lastUpstreamTxByteSent() const PURE;

  /**
   * Sets the time when the last bye of the request was sent upstream.
   */
  virtual void onLastUpstreamTxByteSent() PURE;

  /**
   * @return the duration between the first byte of the response is received from upstream and the
   * start of the request.
   */
  virtual Optional<std::chrono::nanoseconds> firstUpstreamRxByteReceived() const PURE;

  /**
   * Sets the time when the first byte of the response is received from
   * upstream.
   */
  virtual void onFirstUpstreamRxByteReceived() PURE;

  /**
   * @return the duration between the last byte of the response is received from upstream and the
   * start of the request.
   */
  virtual Optional<std::chrono::nanoseconds> lastUpstreamRxByteReceived() const PURE;

  /**
   * Sets the time when the last byte of the response is received from
   * upstream.
   */
  virtual void onLastUpstreamRxByteReceived() PURE;

  /**
   * @return the duration between the first byte of the response is sent downstream and the start of
   * the request. There may be a considerable delta between lastUpstreamByteReceived and this value
   * due to filters.
   */
  virtual Optional<std::chrono::nanoseconds> firstDownstreamTxByteSent() const PURE;

  /**
   * Sets the time when the first byte of the response is sent downstream.
   */
  virtual void onFirstDownstreamTxByteSent() PURE;

  /**
   * @return the duration between the last byte of the response is sent downstream and the start of
   * the request.
   */
  virtual Optional<std::chrono::nanoseconds> lastDownstreamTxByteSent() const PURE;

  /**
   * Sets the time when the last byte of the response is sent downstream.
   */
  virtual void onLastDownstreamTxByteSent() PURE;

  /**
   * @return the total duration of the request (i.e., when the request's ActiveStream is destroyed)
   * and may be longer than lastDownstreamTxByteSent.
   */
  virtual Optional<std::chrono::nanoseconds> requestComplete() const PURE;

  /**
   * Sets the end time for the request. This method is called once the request has been fully
   * completed (i.e., when the request's ActiveStream is destroyed).
   */
  virtual void onRequestComplete() PURE;

  /**
   * Resets all timings related to the upstream in the event of a retry.
   */
  virtual void resetUpstreamTimings() PURE;

  /**
   * @return the number of body bytes sent in the response.
   */
  virtual uint64_t bytesSent() const PURE;

  /**
   * @return whether response flag is set or not.
   */
  virtual bool getResponseFlag(ResponseFlag response_flag) const PURE;

  /**
   * @return upstream host description.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() const PURE;

  /**
   * @return the upstream local address.
   */
  virtual const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const PURE;

  /**
   * @return whether the request is a health check request or not.
   */
  virtual bool healthCheck() const PURE;

  /**
   * @param is_hc whether the request is a health check request or not.
   */
  virtual void healthCheck(bool is_hc) PURE;

  /**
   * @return the downstream local address. Note that this will never be nullptr.
   */
  virtual const Network::Address::InstanceConstSharedPtr& downstreamLocalAddress() const PURE;

  /**
   * @return the downstream remote address. Note that this will never be nullptr. Additionally note
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
