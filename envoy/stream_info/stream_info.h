#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/http/protocol.h"
#include "envoy/network/socket.h"
#include "envoy/ssl/connection.h"
#include "envoy/stream_info/filter_state.h"
#include "envoy/stream_info/stream_id_provider.h"
#include "envoy/tracing/trace_reason.h"
#include "envoy/upstream/host_description.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/singleton/const_singleton.h"

#include "absl/types/optional.h"

namespace Envoy {

namespace Router {
class Route;
using RouteConstSharedPtr = std::shared_ptr<const Route>;
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
  // Upstream response had an HTTP protocol error
  UpstreamProtocolError = 0x800000,
  // No cluster found for a given request.
  NoClusterFound = 0x1000000,
  // Overload Manager terminated the stream.
  OverloadManager = 0x2000000,
  // DNS resolution failed.
  DnsResolutionFailed = 0x4000000,
  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST FLAG.
  LastFlag = DnsResolutionFailed,
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
  // The request was forwarded upstream but the response timed out.
  const std::string ResponseTimeout = "response_timeout";
  // The final upstream try timed out.
  const std::string UpstreamPerTryTimeout = "upstream_per_try_timeout";
  // The final upstream try idle timed out.
  const std::string UpstreamPerTryIdleTimeout = "upstream_per_try_idle_timeout";
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
  // The request was rejected because configured filters erroneously removed required request
  // headers.
  const std::string FilterRemovedRequiredRequestHeaders = "filter_removed_required_request_headers";
  // The request was rejected because configured filters erroneously removed required response
  // headers.
  const std::string FilterRemovedRequiredResponseHeaders =
      "filter_removed_required_response_headers";
  // The request was rejected because the original IP couldn't be detected.
  const std::string OriginalIPDetectionFailed = "rejecting_because_detection_failed";
  // A filter called addDecodedData at the wrong point in the filter chain.
  const std::string FilterAddedInvalidRequestData = "filter_added_invalid_request_data";
  // A filter called addDecodedData at the wrong point in the filter chain.
  const std::string FilterAddedInvalidResponseData = "filter_added_invalid_response_data";
  // Changes or additions to details should be reflected in
  // docs/root/configuration/http/http_conn_man/response_code_details.rst
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

  void onUpstreamConnectStart(TimeSource& time_source) {
    ASSERT(!upstream_connect_start_);
    upstream_connect_start_ = time_source.monotonicTime();
  }

  void onUpstreamConnectComplete(TimeSource& time_source) {
    upstream_connect_complete_ = time_source.monotonicTime();
  }

  void onUpstreamHandshakeComplete(TimeSource& time_source) {
    upstream_handshake_complete_ = time_source.monotonicTime();
  }

  absl::optional<MonotonicTime> first_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> last_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> first_upstream_rx_byte_received_;
  absl::optional<MonotonicTime> last_upstream_rx_byte_received_;

  absl::optional<MonotonicTime> upstream_connect_start_;
  absl::optional<MonotonicTime> upstream_connect_complete_;
  absl::optional<MonotonicTime> upstream_handshake_complete_;
};

class DownstreamTiming {
public:
  void setValue(absl::string_view key, MonotonicTime value) { timings_[key] = value; }

  absl::optional<MonotonicTime> getValue(absl::string_view value) const {
    auto ret = timings_.find(value);
    if (ret == timings_.end()) {
      return {};
    }
    return ret->second;
  }

  absl::optional<MonotonicTime> lastDownstreamRxByteReceived() const {
    return last_downstream_rx_byte_received_;
  }
  absl::optional<MonotonicTime> firstDownstreamTxByteSent() const {
    return first_downstream_tx_byte_sent_;
  }
  absl::optional<MonotonicTime> lastDownstreamTxByteSent() const {
    return last_downstream_tx_byte_sent_;
  }
  absl::optional<MonotonicTime> downstreamHandshakeComplete() const {
    return downstream_handshake_complete_;
  }

  void onLastDownstreamRxByteReceived(TimeSource& time_source) {
    ASSERT(!last_downstream_rx_byte_received_);
    last_downstream_rx_byte_received_ = time_source.monotonicTime();
  }
  void onFirstDownstreamTxByteSent(TimeSource& time_source) {
    ASSERT(!first_downstream_tx_byte_sent_);
    first_downstream_tx_byte_sent_ = time_source.monotonicTime();
  }
  void onLastDownstreamTxByteSent(TimeSource& time_source) {
    ASSERT(!last_downstream_tx_byte_sent_);
    last_downstream_tx_byte_sent_ = time_source.monotonicTime();
  }
  void onDownstreamHandshakeComplete(TimeSource& time_source) {
    // An existing value can be overwritten, e.g. in resumption case.
    downstream_handshake_complete_ = time_source.monotonicTime();
  }

private:
  absl::flat_hash_map<std::string, MonotonicTime> timings_;
  // The time when the last byte of the request was received.
  absl::optional<MonotonicTime> last_downstream_rx_byte_received_;
  // The time when the first byte of the response was sent downstream.
  absl::optional<MonotonicTime> first_downstream_tx_byte_sent_;
  // The time when the last byte of the response was sent downstream.
  absl::optional<MonotonicTime> last_downstream_tx_byte_sent_;
  // The time the TLS handshake completed. Set at connection level.
  absl::optional<MonotonicTime> downstream_handshake_complete_;
};

// Measure the number of bytes sent and received for a stream.
struct BytesMeter {
  uint64_t wireBytesSent() const { return wire_bytes_sent_; }
  uint64_t wireBytesReceived() const { return wire_bytes_received_; }
  uint64_t headerBytesSent() const { return header_bytes_sent_; }
  uint64_t headerBytesReceived() const { return header_bytes_received_; }
  void addHeaderBytesSent(uint64_t added_bytes) { header_bytes_sent_ += added_bytes; }
  void addHeaderBytesReceived(uint64_t added_bytes) { header_bytes_received_ += added_bytes; }
  void addWireBytesSent(uint64_t added_bytes) { wire_bytes_sent_ += added_bytes; }
  void addWireBytesReceived(uint64_t added_bytes) { wire_bytes_received_ += added_bytes; }

private:
  uint64_t header_bytes_sent_{};
  uint64_t header_bytes_received_{};
  uint64_t wire_bytes_sent_{};
  uint64_t wire_bytes_received_{};
};

using BytesMeterSharedPtr = std::shared_ptr<BytesMeter>;

class UpstreamInfo {
public:
  virtual ~UpstreamInfo() = default;

  /**
   * Dump the upstream info to the specified ostream.
   *
   * @param os the ostream to dump state to
   * @param indent_level the depth, for pretty-printing.
   *
   * This function is called on Envoy fatal errors so should avoid memory allocation.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;

  /**
   * @param connection ID of the upstream connection.
   */
  virtual void setUpstreamConnectionId(uint64_t id) PURE;

  /**
   * @return the ID of the upstream connection, or absl::nullopt if not available.
   */
  virtual absl::optional<uint64_t> upstreamConnectionId() const PURE;

  /**
   * @param interface name of the upstream connection's local socket.
   */
  virtual void setUpstreamInterfaceName(absl::string_view interface_name) PURE;

  /**
   * @return interface name of the upstream connection's local socket, or absl::nullopt if not
   * available.
   */
  virtual absl::optional<absl::string_view> upstreamInterfaceName() const PURE;

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

  /*
   * @return the upstream timing for this stream
   * */
  virtual UpstreamTiming& upstreamTiming() PURE;
  virtual const UpstreamTiming& upstreamTiming() const PURE;

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
   * @param upstream_remote_address sets the remote address of the upstream connection.
   */
  virtual void setUpstreamRemoteAddress(
      const Network::Address::InstanceConstSharedPtr& upstream_remote_address) PURE;

  /**
   * @return the upstream remote address.
   */
  virtual const Network::Address::InstanceConstSharedPtr& upstreamRemoteAddress() const PURE;

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
   * @param host the selected upstream host for the request.
   */
  virtual void setUpstreamHost(Upstream::HostDescriptionConstSharedPtr host) PURE;

  /**
   * @return upstream host description.
   */
  virtual Upstream::HostDescriptionConstSharedPtr upstreamHost() const PURE;

  /**
   * Filter State object to be shared between upstream and downstream filters.
   * @param pointer to upstream connections filter state.
   * @return pointer to filter state to be used by upstream connections.
   */
  virtual const FilterStateSharedPtr& upstreamFilterState() const PURE;
  virtual void setUpstreamFilterState(const FilterStateSharedPtr& filter_state) PURE;

  /**
   * Getters and setters for the number of streams started on this connection.
   * For upstream connections this is updated as streams are created.
   * For downstream connections this is latched at the time the upstream stream
   * is assigned.
   */
  virtual void setUpstreamNumStreams(uint64_t num_streams) PURE;
  virtual uint64_t upstreamNumStreams() const PURE;

  virtual void setUpstreamProtocol(Http::Protocol protocol) PURE;
  virtual absl::optional<Http::Protocol> upstreamProtocol() const PURE;
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
   * @param rc_details the response code details string to set for this request. It should not
   * contain any empty or space characters (' ', '\t', '\f', '\v', '\n', '\r'). See
   * ResponseCodeDetailValues above for well-known constants.
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
   * @param std::string name denotes the name of the route.
   */
  virtual void setRouteName(absl::string_view name) PURE;

  /**
   * @return std::string& the name of the route.
   */
  virtual const std::string& getRouteName() const PURE;

  /**
   * @param std::string name denotes the name of the virtual cluster.
   */
  virtual void setVirtualClusterName(const absl::optional<std::string>& name) PURE;

  /**
   * @return std::string& the name of the virtual cluster which got matched.
   */
  virtual const absl::optional<std::string>& virtualClusterName() const PURE;

  /**
   * @param bytes_received denotes number of bytes to add to total received bytes.
   */
  virtual void addBytesReceived(uint64_t bytes_received) PURE;

  /**
   * @return the number of body bytes received by the stream.
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
   * Sets the upstream information for this stream.
   */
  virtual void setUpstreamInfo(std::shared_ptr<UpstreamInfo>) PURE;

  /**
   * Returns the upstream information for this stream.
   */
  virtual std::shared_ptr<UpstreamInfo> upstreamInfo() PURE;
  virtual OptRef<const UpstreamInfo> upstreamInfo() const PURE;

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
   * @return the downstream timing information.
   */
  virtual DownstreamTiming& downstreamTiming() PURE;
  virtual OptRef<const DownstreamTiming> downstreamTiming() const PURE;

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
   * @return whether the request is a health check request or not.
   */
  virtual bool healthCheck() const PURE;

  /**
   * @param is_health_check whether the request is a health check request or not.
   */
  virtual void healthCheck(bool is_health_check) PURE;

  /**
   * @return the downstream connection info provider.
   */
  virtual const Network::ConnectionInfoProvider& downstreamAddressProvider() const PURE;

  /**
   * @return const Router::RouteConstSharedPtr Get the route selected for this request.
   */
  virtual Router::RouteConstSharedPtr route() const PURE;

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
   * @param provider The unique id implementation this stream uses.
   */
  virtual void setStreamIdProvider(StreamIdProviderSharedPtr provider) PURE;

  /**
   * @return the unique id for this stream if available.
   */
  virtual OptRef<const StreamIdProvider> getStreamIdProvider() const PURE;

  /**
   * Set the trace reason for the stream.
   */
  virtual void setTraceReason(Tracing::Reason reason) PURE;

  /**
   * @return the trace reason for the stream.
   */
  virtual Tracing::Reason traceReason() const PURE;

  /**
   * @param filter_chain_name Network filter chain name of the downstream connection.
   */
  virtual void setFilterChainName(absl::string_view filter_chain_name) PURE;

  /**
   * @return Network filter chain name of the downstream connection.
   */
  virtual const std::string& filterChainName() const PURE;

  /**
   * @param attempt_count, the number of times the request was attempted upstream.
   */
  virtual void setAttemptCount(uint32_t attempt_count) PURE;

  /**
   * @return the number of times the request was attempted upstream, absl::nullopt if the request
   * was never attempted upstream.
   */
  virtual absl::optional<uint32_t> attemptCount() const PURE;

  /**
   * @return the bytes meter for upstream http stream.
   */
  virtual const BytesMeterSharedPtr& getUpstreamBytesMeter() const PURE;

  /**
   * @return the bytes meter for downstream http stream.
   */
  virtual const BytesMeterSharedPtr& getDownstreamBytesMeter() const PURE;

  /**
   * @param upstream_bytes_meter, the bytes meter for upstream http stream.
   */
  virtual void setUpstreamBytesMeter(const BytesMeterSharedPtr& upstream_bytes_meter) PURE;

  /**
   * @param downstream_bytes_meter, the bytes meter for downstream http stream.
   */
  virtual void setDownstreamBytesMeter(const BytesMeterSharedPtr& downstream_bytes_meter) PURE;

  virtual bool isShadow() const PURE;

  static void syncUpstreamAndDownstreamBytesMeter(StreamInfo& downstream_info,
                                                  StreamInfo& upstream_info) {
    downstream_info.setUpstreamBytesMeter(upstream_info.getUpstreamBytesMeter());
    upstream_info.setDownstreamBytesMeter(downstream_info.getDownstreamBytesMeter());
  }

  /**
   * Dump the info to the specified ostream.
   *
   * @param os the ostream to dump state to
   * @param indent_level the depth, for pretty-printing.
   *
   * This function is called on Envoy fatal errors so should avoid memory allocation.
   */
  virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;
};

// An enum representation of the Proxy-Status error space.
enum class ProxyStatusError {
  DnsTimeout,
  DnsError,
  DestinationNotFound,
  DestinationUnavailable,
  DestinationIpProhibited,
  DestinationIpUnroutable,
  ConnectionRefused,
  ConnectionTerminated,
  ConnectionTimeout,
  ConnectionReadTimeout,
  ConnectionWriteTimeout,
  ConnectionLimitReached,
  TlsProtocolError,
  TlsCertificateError,
  TlsAlertReceived,
  HttpRequestError,
  HttpRequestDenied,
  HttpResponseIncomplete,
  HttpResponseHeaderSectionSize,
  HttpResponseHeaderSize,
  HttpResponseBodySize,
  HttpResponseTrailerSectionSize,
  HttpResponseTrailerSize,
  HttpResponseTransferCoding,
  HttpResponseContentCoding,
  HttpResponseTimeout,
  HttpUpgradeFailed,
  HttpProtocolError,
  ProxyInternalResponse,
  ProxyInternalError,
  ProxyConfigurationError,
  ProxyLoopDetected,
  // ATTENTION: MAKE SURE THAT THIS REMAINS EQUAL TO THE LAST FLAG.
  LastProxyStatus = ProxyLoopDetected,
};

} // namespace StreamInfo
} // namespace Envoy
