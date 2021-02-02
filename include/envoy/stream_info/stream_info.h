#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/network/socket.h"
#include "envoy/ssl/connection.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/upstream/host_description.h"

#include "common/common/assert.h"
#include "common/protobuf/protobuf.h"
#include "common/singleton/const_singleton.h"

#include "absl/types/optional.h"

namespace Envoy {

namespace Router {
class RouteEntry;
} // namespace Router

namespace Upstream {
class ClusterInfo;
using ClusterInfoConstSharedPtr = std::shared_ptr<const ClusterInfo>;
} // namespace Upstream

namespace StreamInfo {

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
  // Request was unauthorized by external authorization service.
  UnauthorizedExternalService = 0x1000,
  // Unable to call Ratelimit service.
  RateLimitServiceError = 0x2000,
  // If the stream was reset due to a downstream connection termination.
  DownstreamConnectionTermination = 0x4000,
  // Exceeded upstream retry limit.
  UpstreamRetryLimitExceeded = 0x8000,
  // Request hit the stream idle timeout, triggering a 408.
  StreamIdleTimeout = 0x10000,
  // Request specified x-envoy-* header values that failed strict header checks.
  InvalidEnvoyRequestHeaders = 0x20000,
  // Downstream request had an HTTP protocol error
  DownstreamProtocolError = 0x40000,
  // Upstream request reached to user defined max stream duration.
  UpstreamMaxStreamDurationReached = 0x80000,
  // True if the response was served from an Envoy cache filter.
  ResponseFromCacheFilter = 0x100000,
  // Filter config was not received within the permitted warming deadline.
  NoFilterConfigFound = 0x200000,
  // Request or connection exceeded the downstream connection duration.
  DurationTimeout = 0x400000,
  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST FLAG.
  LastFlag = DurationTimeout
};

/**
 * Constants for the response code details field of StreamInfo for details sent
 * by core (non-extension) code.
 *
 * These provide details about the stream state such as whether the
 * response is from the upstream or from envoy (in case of a local reply).
 * Custom extensions can define additional values provided they are appropriately
 * scoped to avoid collisions.
 */
struct ResponseCodeDetailValues {
  // Response code was set by the upstream.
  const std::string ViaUpstream = "via_upstream";
  // Envoy is doing non-streaming proxying, and the request payload exceeded
  // configured limits.
  const std::string RequestPayloadTooLarge = "request_payload_too_large";
  // Envoy is doing non-streaming proxying, and the response payload exceeded
  // configured limits.
  const std::string ResponsePayloadTooLarge = "response_payload_too_large";
  // Envoy is doing streaming proxying, but too much data arrived while waiting
  // to attempt a retry.
  const std::string RequestPayloadExceededRetryBufferLimit =
      "request_payload_exceeded_retry_buffer_limit";
  // The per-stream keepalive timeout was exceeded.
  const std::string StreamIdleTimeout = "stream_idle_timeout";
  // The per-stream max duration timeout was exceeded.
  const std::string MaxDurationTimeout = "max_duration_timeout";
  // The per-stream total request timeout was exceeded.
  const std::string RequestOverallTimeout = "request_overall_timeout";
  // The per-stream request header timeout was exceeded.
  const std::string RequestHeaderTimeout = "request_header_timeout";
  // The request was rejected due to the Overload Manager reaching configured resource limits.
  const std::string Overload = "overload";
  // The HTTP/1.0 or HTTP/0.9 request was rejected due to HTTP/1.0 support not being configured.
  const std::string LowVersion = "low_version";
  // The request was rejected due to a missing Host: or :authority field.
  const std::string MissingHost = "missing_host_header";
  // The request was rejected due to x-envoy-* headers failing strict header validation.
  const std::string InvalidEnvoyRequestHeaders = "request_headers_failed_strict_check";
  // The request was rejected due to a missing Path or :path header field.
  const std::string MissingPath = "missing_path_rejected";
  // The request was rejected due to using an absolute path on a route not supporting them.
  const std::string AbsolutePath = "absolute_path_rejected";
  // The request was rejected because path normalization was configured on and failed, probably due
  // to an invalid path.
  const std::string PathNormalizationFailed = "path_normalization_failed";
  // The request was rejected because it attempted an unsupported upgrade.
  const std::string UpgradeFailed = "upgrade_failed";

  // The request was rejected by the HCM because there was no route configuration found.
  const std::string RouteConfigurationNotFound = "route_configuration_not_found";
  // The request was rejected by the router filter because there was no route found.
  const std::string RouteNotFound = "route_not_found";
  // A direct response was generated by the router filter.
  const std::string DirectResponse = "direct_response";
  // The request was rejected by the router filter because there was no cluster found for the
  // selected route.
  const std::string ClusterNotFound = "cluster_not_found";
  // The request was rejected by the router filter because the cluster was in maintenance mode.
  const std::string MaintenanceMode = "maintenance_mode";
  // The request was rejected by the router filter because there was no healthy upstream found.
  const std::string NoHealthyUpstream = "no_healthy_upstream";
  // The upstream response timed out.
  const std::string UpstreamTimeout = "upstream_response_timeout";
  // The final upstream try timed out.
  const std::string UpstreamPerTryTimeout = "upstream_per_try_timeout";
  // The request was destroyed because of user defined max stream duration.
  const std::string UpstreamMaxStreamDurationReached = "upstream_max_stream_duration_reached";
  // The upstream connection was reset before a response was started. This
  // will generally be accompanied by details about why the reset occurred.
  const std::string EarlyUpstreamReset = "upstream_reset_before_response_started";
  // The upstream connection was reset after a response was started. This
  // will generally be accompanied by details about why the reset occurred but
  // indicates that original "success" headers may have been sent downstream
  // despite the subsequent failure.
  const std::string LateUpstreamReset = "upstream_reset_after_response_started";
  // The request was rejected due to no matching filter chain.
  const std::string FilterChainNotFound = "filter_chain_not_found";
  // The client disconnected unexpectedly.
  const std::string DownstreamRemoteDisconnect = "downstream_remote_disconnect";
  // The client connection was locally closed for an unspecified reason.
  const std::string DownstreamLocalDisconnect = "downstream_local_disconnect";
  // The max connection duration was exceeded.
  const std::string DurationTimeout = "duration_timeout";
  // The max request downstream header duration was exceeded.
  const std::string DownstreamHeaderTimeout = "downstream_header_timeout";
  // The response was generated by the admin filter.
  const std::string AdminFilterResponse = "admin_filter_response";
  // The original stream was replaced with an internal redirect.
  const std::string InternalRedirect = "internal_redirect";
  // The request was rejected because configured filters erroneously removed required headers.
  const std::string FilterRemovedRequiredHeaders = "filter_removed_required_headers";
  // Changes or additions to details should be reflected in
  // docs/root/configuration/http/http_conn_man/response_code_details_details.rst
};

using ResponseCodeDetails = ConstSingleton<ResponseCodeDetailValues>;

struct UpstreamTiming {
  /**
   * Sets the time when the first byte of the request was sent upstream.
   */
  void onFirstUpstreamTxByteSent(TimeSource& time_source) {
    ASSERT(!first_upstream_tx_byte_sent_);
    first_upstream_tx_byte_sent_ = time_source.monotonicTime();
  }

  /**
   * Sets the time when the first byte of the response is received from upstream.
   */
  void onLastUpstreamTxByteSent(TimeSource& time_source) {
    ASSERT(!last_upstream_tx_byte_sent_);
    last_upstream_tx_byte_sent_ = time_source.monotonicTime();
  }

  /**
   * Sets the time when the last byte of the response is received from upstream.
   */
  void onFirstUpstreamRxByteReceived(TimeSource& time_source) {
    ASSERT(!first_upstream_rx_byte_received_);
    first_upstream_rx_byte_received_ = time_source.monotonicTime();
  }

  /**
   * Sets the time when the last byte of the request was sent upstream.
   */
  void onLastUpstreamRxByteReceived(TimeSource& time_source) {
    ASSERT(!last_upstream_rx_byte_received_);
    last_upstream_rx_byte_received_ = time_source.monotonicTime();
  }

  absl::optional<MonotonicTime> first_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> last_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> first_upstream_rx_byte_received_;
  absl::optional<MonotonicTime> last_upstream_rx_byte_received_;
};

/**
 * Additional information about a completed request for logging.
 */
class StreamInfo {
public:
  virtual ~StreamInfo() = default;

  /**
   * @param response_flag the response flag. Each filter can set independent response flags. The
   * flags are accumulated.
   */
  virtual void setResponseFlag(ResponseFlag response_flag) PURE;

  /**
   * @param code the HTTP response code to set for this request.
   */
  virtual void setResponseCode(uint32_t code) PURE;

  /**
   * @param rc_details the response code details string to set for this request.
   * See ResponseCodeDetailValues above for well-known constants.
   */
  virtual void setResponseCodeDetails(absl::string_view rc_details) PURE;

  /**
   * @param connection_termination_details the termination details string to set for this
   * connection.
   */
  virtual void
  setConnectionTerminationDetails(absl::string_view connection_termination_details) PURE;

  /**
   * @param response_flags the response_flags to intersect with.
   * @return true if the intersection of the response_flags argument and the currently set response
   * flags is non-empty.
   */
  virtual bool intersectResponseFlags(uint64_t response_flags) const PURE;

  /**
   * @param host the selected upstream host for the request.
   */
  virtual void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * @param std::string name denotes the name of the route.
   */
  virtual void setRouteName(absl::string_view name) PURE;

  /**
   * @return std::string& the name of the route.
   */
  virtual const std::string& getRouteName() const PURE;
  /**
   * @param bytes_received denotes number of bytes to add to total received bytes.
   */
  virtual void addBytesReceived(uint64_t bytes_received) PURE;

  /**
   * @return the number of body bytes received in the request.
   */
  virtual uint64_t bytesReceived() const PURE;

  /**
   * @return the protocol of the request.
   */
  virtual absl::optional<Http::Protocol> protocol() const PURE;

  /**
   * @param protocol the request's protocol.
   */
  virtual void protocol(Http::Protocol protocol) PURE;

  /**
   * @return the response code.
   */
  virtual absl::optional<uint32_t> responseCode() const PURE;

  /**
   * @return the response code details.
   */
  virtual const absl::optional<std::string>& responseCodeDetails() const PURE;

  /**
   * @return the termination details of the connection.
   */
  virtual const absl::optional<std::string>& connectionTerminationDetails() const PURE;

  /**
   * @return the time that the first byte of the request was received.
   */
  virtual SystemTime startTime() const PURE;

  /**
   * @return the monotonic time that the first byte of the request was received. Duration
   * calculations should be made relative to this value.
   */
  virtual MonotonicTime startTimeMonotonic() const PURE;

  /**
   * @return the duration between the last byte of the request was received and the start of the
   * request.
   */
  virtual absl::optional<std::chrono::nanoseconds> lastDownstreamRxByteReceived() const PURE;

  /**
   * Sets the time when the last byte of the request was received.
   */
  virtual void onLastDownstreamRxByteReceived() PURE;

  /**
   * Sets the upstream timing information for this stream. This is useful for
   * when multiple upstream requests are issued and we want to save timing
   * information for the one that "wins".
   */
  virtual void setUpstreamTiming(const UpstreamTiming& upstream_timing) PURE;

  /**
   * @return the duration between the first byte of the request was sent upstream and the start of
   * the request. There may be a considerable delta between lastDownstreamByteReceived and this
   * value due to filters.
   */
  virtual absl::optional<std::chrono::nanoseconds> firstUpstreamTxByteSent() const PURE;

  /**
   * @return the duration between the last byte of the request was sent upstream and the start of
   * the request.
   */
  virtual absl::optional<std::chrono::nanoseconds> lastUpstreamTxByteSent() const PURE;

  /**
   * @return the duration between the first byte of the response is received from upstream and the
   * start of the request.
   */
  virtual absl::optional<std::chrono::nanoseconds> firstUpstreamRxByteReceived() const PURE;

  /**
   * @return the duration between the last byte of the response is received from upstream and the
   * start of the request.
   */
  virtual absl::optional<std::chrono::nanoseconds> lastUpstreamRxByteReceived() const PURE;
  /**
   * @return the duration between the first byte of the response is sent downstream and the start of
   * the request. There may be a considerable delta between lastUpstreamByteReceived and this value
   * due to filters.
   */
  virtual absl::optional<std::chrono::nanoseconds> firstDownstreamTxByteSent() const PURE;

  /**
   * Sets the time when the first byte of the response is sent downstream.
   */
  virtual void onFirstDownstreamTxByteSent() PURE;

  /**
   * @return the duration between the last byte of the response is sent downstream and the start of
   * the request.
   */
  virtual absl::optional<std::chrono::nanoseconds> lastDownstreamTxByteSent() const PURE;

  /**
   * Sets the time when the last byte of the response is sent downstream.
   */
  virtual void onLastDownstreamTxByteSent() PURE;

  /**
   * @return the total duration of the request (i.e., when the request's ActiveStream is destroyed)
   * and may be longer than lastDownstreamTxByteSent.
   */
  virtual absl::optional<std::chrono::nanoseconds> requestComplete() const PURE;

  /**
   * Sets the end time for the request. This method is called once the request has been fully
   * completed (i.e., when the request's ActiveStream is destroyed).
   */
  virtual void onRequestComplete() PURE;

  /**
   * @param bytes_sent denotes the number of bytes to add to total sent bytes.
   */
  virtual void addBytesSent(uint64_t bytes_sent) PURE;

  /**
   * @return the number of body bytes sent in the response.
   */
  virtual uint64_t bytesSent() const PURE;

  /**
   * @return whether response flag is set or not.
   */
  virtual bool hasResponseFlag(ResponseFlag response_flag) const PURE;

  /**
   * @return whether any response flag is set or not.
   */
  virtual bool hasAnyResponseFlag() const PURE;

  /**
   * @return response flags encoded as an integer.
   */
  virtual uint64_t responseFlags() const PURE;

  /**
   * @return upstream host description.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() const PURE;

  /**
   * @param upstream_local_address sets the local address of the upstream connection. Note that it
   * can be different than the local address of the downstream connection.
   */
  virtual void setUpstreamLocalAddress(
      const Network::Address::InstanceConstSharedPtr& upstream_local_address) PURE;

  /**
   * @return the upstream local address.
   */
  virtual const Network::Address::InstanceConstSharedPtr& upstreamLocalAddress() const PURE;

  /**
   * @return whether the request is a health check request or not.
   */
  virtual bool healthCheck() const PURE;

  /**
   * @param is_health_check whether the request is a health check request or not.
   */
  virtual void healthCheck(bool is_health_check) PURE;

  /**
   * @return the downstream address provider.
   */
  virtual const Network::SocketAddressProvider& downstreamAddressProvider() const PURE;

  /**
   * @param connection_info sets the downstream ssl connection.
   */
  virtual void
  setDownstreamSslConnection(const Ssl::ConnectionInfoConstSharedPtr& ssl_connection_info) PURE;

  /**
   * @return the downstream SSL connection. This will be nullptr if the downstream
   * connection does not use SSL.
   */
  virtual Ssl::ConnectionInfoConstSharedPtr downstreamSslConnection() const PURE;

  /**
   * @param connection_info sets the upstream ssl connection.
   */
  virtual void
  setUpstreamSslConnection(const Ssl::ConnectionInfoConstSharedPtr& ssl_connection_info) PURE;

  /**
   * @return the upstream SSL connection. This will be nullptr if the upstream
   * connection does not use SSL.
   */
  virtual Ssl::ConnectionInfoConstSharedPtr upstreamSslConnection() const PURE;

  /**
   * @return const Router::RouteEntry* Get the route entry selected for this request. Note: this
   * will be nullptr if no route was selected.
   */
  virtual const Router::RouteEntry* routeEntry() const PURE;

  /**
   * @return const envoy::config::core::v3::Metadata& the dynamic metadata associated with this
   * request
   */
  virtual envoy::config::core::v3::Metadata& dynamicMetadata() PURE;
  virtual const envoy::config::core::v3::Metadata& dynamicMetadata() const PURE;

  /**
   * @param name the namespace used in the metadata in reverse DNS format, for example:
   * envoy.test.my_filter.
   * @param value the struct to set on the namespace. A merge will be performed with new values for
   * the same key overriding existing.
   */
  virtual void setDynamicMetadata(const std::string& name, const ProtobufWkt::Struct& value) PURE;

  /**
   * Object on which filters can share data on a per-request basis. For singleton data objects, only
   * one filter can produce a named data object. List data objects can be updated by multiple
   * filters (append only). Both object types can be consumed by multiple filters.
   * @return the filter state associated with this request.
   */
  virtual const FilterStateSharedPtr& filterState() PURE;
  virtual const FilterState& filterState() const PURE;

  /**
   * Filter State object to be shared between upstream and downstream filters.
   * @param pointer to upstream connections filter state.
   * @return pointer to filter state to be used by upstream connections.
   */
  virtual const FilterStateSharedPtr& upstreamFilterState() const PURE;
  virtual void setUpstreamFilterState(const FilterStateSharedPtr& filter_state) PURE;

  /**
   * @param SNI value requested.
   */
  virtual void setRequestedServerName(const absl::string_view requested_server_name) PURE;

  /**
   * @return SNI value for downstream host.
   */
  virtual const std::string& requestedServerName() const PURE;

  /**
   * @param failure_reason the upstream transport failure reason.
   */
  virtual void setUpstreamTransportFailureReason(absl::string_view failure_reason) PURE;

  /**
   * @return const std::string& the upstream transport failure reason, e.g. certificate validation
   *         failed.
   */
  virtual const std::string& upstreamTransportFailureReason() const PURE;

  /**
   * @param headers request headers.
   */
  virtual void setRequestHeaders(const Http::RequestHeaderMap& headers) PURE;

  /**
   * @return request headers.
   */
  virtual const Http::RequestHeaderMap* getRequestHeaders() const PURE;

  /**
   * @param Upstream Connection's ClusterInfo.
   */
  virtual void
  setUpstreamClusterInfo(const Upstream::ClusterInfoConstSharedPtr& upstream_cluster_info) PURE;

  /**
   * @return Upstream Connection's ClusterInfo.
   * This returns an optional to differentiate between unset(absl::nullopt),
   * no route or cluster does not exist(nullptr), and set to a valid cluster(not nullptr).
   */
  virtual absl::optional<Upstream::ClusterInfoConstSharedPtr> upstreamClusterInfo() const PURE;

  /**
   * @param utils The requestID utils implementation this stream uses
   */
  virtual void setRequestIDExtension(Http::RequestIDExtensionSharedPtr utils) PURE;

  /**
   * @return A shared pointer to the request ID utils for this stream
   */
  virtual Http::RequestIDExtensionSharedPtr getRequestIDExtension() const PURE;

  /**
   * @return Connection ID of the downstream connection, or unset if not available.
   **/
  virtual absl::optional<uint64_t> connectionID() const PURE;

  /**
   * @param id Connection ID of the downstream connection.
   **/
  virtual void setConnectionID(uint64_t id) PURE;
};

} // namespace StreamInfo
} // namespace Envoy
