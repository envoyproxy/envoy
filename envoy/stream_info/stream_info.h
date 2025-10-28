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
class VirtualHost;
using VirtualHostConstSharedPtr = std::shared_ptr<const VirtualHost>;
} // namespace Router

namespace Upstream {
class ClusterInfo;
using ClusterInfoConstSharedPtr = std::shared_ptr<const ClusterInfo>;
} // namespace Upstream

namespace StreamInfo {

enum CoreResponseFlag : uint16_t {
  // Local server healthcheck failed.
  FailedLocalHealthCheck,
  // No healthy upstream.
  NoHealthyUpstream,
  // Request timeout on upstream.
  UpstreamRequestTimeout,
  // Local codec level reset was sent on the stream.
  LocalReset,
  // Remote codec level reset was received on the stream.
  UpstreamRemoteReset,
  // Local reset by a connection pool due to an initial connection failure.
  UpstreamConnectionFailure,
  // If the stream was locally reset due to connection termination.
  UpstreamConnectionTermination,
  // The stream was reset because of a resource overflow.
  UpstreamOverflow,
  // No route found for a given request.
  NoRouteFound,
  // Request was delayed before proxying.
  DelayInjected,
  // Abort with error code was injected.
  FaultInjected,
  // Request was ratelimited locally by rate limit filter.
  RateLimited,
  // Request was unauthorized by external authorization service.
  UnauthorizedExternalService,
  // Unable to call Ratelimit service.
  RateLimitServiceError,
  // If the stream was reset due to a downstream connection termination.
  DownstreamConnectionTermination,
  // Exceeded upstream retry limit.
  UpstreamRetryLimitExceeded,
  // Request hit the stream idle timeout, triggering a 408.
  StreamIdleTimeout,
  // Request specified x-envoy-* header values that failed strict header checks.
  InvalidEnvoyRequestHeaders,
  // Downstream request had an HTTP protocol error
  DownstreamProtocolError,
  // Upstream request reached to user defined max stream duration.
  UpstreamMaxStreamDurationReached,
  // True if the response was served from an Envoy cache filter.
  ResponseFromCacheFilter,
  // Filter config was not received within the permitted warming deadline.
  NoFilterConfigFound,
  // Request or connection exceeded the downstream connection duration.
  DurationTimeout,
  // Upstream response had an HTTP protocol error
  UpstreamProtocolError,
  // No cluster found for a given request.
  NoClusterFound,
  // Overload Manager terminated the stream.
  OverloadManager,
  // DNS resolution failed.
  DnsResolutionFailed,
  // Drop certain percentage of overloaded traffic.
  DropOverLoad,
  // Downstream remote codec level reset was received on the stream.
  DownstreamRemoteReset,
  // Unconditionally drop all traffic due to drop_overload is set to 100%.
  UnconditionalDropOverload,
  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST FLAG.
  LastFlag = UnconditionalDropOverload,
};

class ResponseFlagUtils;

class ResponseFlag {
public:
  constexpr ResponseFlag() = default;

  /**
   * Construct a response flag from the core response flag enum. The integer
   * value of the enum is used as the raw integer value of the flag.
   * @param flag the core response flag enum.
   */
  constexpr ResponseFlag(CoreResponseFlag flag) : value_(flag) {}

  /**
   * Get the raw integer value of the flag.
   * @return uint16_t the raw integer value.
   */
  uint16_t value() const { return value_; }

  bool operator==(const ResponseFlag& other) const { return value_ == other.value_; }

private:
  friend class ResponseFlagUtils;

  // This private constructor is used to create extended response flags from
  // uint16_t values. This can only be used by ResponseFlagUtils to ensure
  // only validated values are used.
  ResponseFlag(uint16_t value) : value_(value) {}

  uint16_t value_{};
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
  // The request was rejected due to an invalid Path or :path header field.
  const std::string InvalidPath = "invalid_path";
  // The request was rejected due to using an absolute path on a route not supporting them.
  const std::string AbsolutePath = "absolute_path_rejected";
  // The request was rejected because path normalization was configured on and failed, probably due
  // to an invalid path.
  const std::string PathNormalizationFailed = "path_normalization_failed";
  // The request was rejected because it attempted an unsupported upgrade.
  const std::string UpgradeFailed = "upgrade_failed";
  // The websocket handshake is unsuccessful and only SwitchingProtocols is considering successful.
  const std::string WebsocketHandshakeUnsuccessful = "websocket_handshake_unsuccessful";

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
  // The request was rejected by the router filter because the DROP_OVERLOAD configuration.
  const std::string DropOverload = "drop_overload";
  // The request was rejected by the router filter because the DROP_OVERLOAD configuration is set to
  // 100%.
  const std::string UnconditionalDropOverload = "unconditional_drop_overload";
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
  // The client connection was locally closed for the given reason.
  const std::string DownstreamLocalDisconnect = "downstream_local_disconnect({})";
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

/**
 * Constants for the locally closing a connection. This is used in response code
 * details field of StreamInfo for details sent by core (non-extension) code.
 * This is incomplete as some details may be
 *
 * Custom extensions can define additional values provided they are appropriately
 * scoped to avoid collisions.
 */
struct LocalCloseReasonValues {
  const std::string DeferredCloseOnDrainedConnection = "deferred_close_on_drained_connection";
  const std::string IdleTimeoutOnConnection = "on_idle_timeout";
  const std::string CloseForConnectRequestOrTcpTunneling =
      "close_for_connect_request_or_tcp_tunneling";
  const std::string Http2PingTimeout = "http2_ping_timeout";
  const std::string Http2ConnectionProtocolViolation = "http2_connection_protocol_violation";
  const std::string TransportSocketTimeout = "transport_socket_timeout";
  const std::string TriggeredDelayedCloseTimeout = "triggered_delayed_close_timeout";
  const std::string TcpProxyInitializationFailure = "tcp_initializion_failure:";
  const std::string TcpSessionIdleTimeout = "tcp_session_idle_timeout";
  const std::string MaxConnectionDurationReached = "max_connection_duration_reached";
  const std::string ClosingUpstreamTcpDueToDownstreamRemoteClose =
      "closing_upstream_tcp_connection_due_to_downstream_remote_close";
  const std::string ClosingUpstreamTcpDueToDownstreamLocalClose =
      "closing_upstream_tcp_connection_due_to_downstream_local_close";
  const std::string ClosingUpstreamTcpDueToDownstreamResetClose =
      "closing_upstream_tcp_connection_due_to_downstream_reset_close";
  const std::string NonPooledTcpConnectionHostHealthFailure =
      "non_pooled_tcp_connection_host_health_failure";
};

using LocalCloseReasons = ConstSingleton<LocalCloseReasonValues>;

struct UpstreamTiming {
  /**
   * Records the latency from when the upstream request was created to when the
   * connection pool callbacks (either success of failure were triggered).
   */
  void recordConnectionPoolCallbackLatency(MonotonicTime start, TimeSource& time_source) {
    ASSERT(!connection_pool_callback_latency_);
    connection_pool_callback_latency_ =
        std::chrono::duration_cast<std::chrono::nanoseconds>(time_source.monotonicTime() - start);
  }

  /**
   * Sets the time when the first byte of the request was sent upstream.
   */
  void onFirstUpstreamTxByteSent(TimeSource& time_source) {
    ASSERT(!first_upstream_tx_byte_sent_);
    first_upstream_tx_byte_sent_ = time_source.monotonicTime();
  }

  /**
   * Sets the time when the last byte of the request was sent upstream.
   */
  void onLastUpstreamTxByteSent(TimeSource& time_source) {
    ASSERT(!last_upstream_tx_byte_sent_);
    last_upstream_tx_byte_sent_ = time_source.monotonicTime();
  }

  /**
   * Sets the time when the first byte of the response is received from upstream.
   */
  void onFirstUpstreamRxByteReceived(TimeSource& time_source) {
    ASSERT(!first_upstream_rx_byte_received_);
    first_upstream_rx_byte_received_ = time_source.monotonicTime();
  }

  /**
   * Sets the time when the last byte of the response is received from upstream.
   */
  void onLastUpstreamRxByteReceived(TimeSource& time_source) {
    ASSERT(!last_upstream_rx_byte_received_);
    last_upstream_rx_byte_received_ = time_source.monotonicTime();
  }

  /**
   * Sets the time when the first byte of the response body is received from upstream.
   */
  void onFirstUpstreamRxBodyByteReceived(TimeSource& time_source) {
    ASSERT(!first_upstream_rx_body_byte_received_);
    first_upstream_rx_body_byte_received_ = time_source.monotonicTime();
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

  absl::optional<MonotonicTime> upstreamHandshakeComplete() const {
    return upstream_handshake_complete_;
  }

  absl::optional<std::chrono::nanoseconds> connectionPoolCallbackLatency() const {
    return connection_pool_callback_latency_;
  }

  absl::optional<std::chrono::nanoseconds> connection_pool_callback_latency_;
  absl::optional<MonotonicTime> first_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> last_upstream_tx_byte_sent_;
  absl::optional<MonotonicTime> first_upstream_rx_byte_received_;
  absl::optional<MonotonicTime> last_upstream_rx_byte_received_;
  absl::optional<MonotonicTime> first_upstream_rx_body_byte_received_;

  absl::optional<MonotonicTime> upstream_connect_start_;
  absl::optional<MonotonicTime> upstream_connect_complete_;
  absl::optional<MonotonicTime> upstream_handshake_complete_;
};

struct DownstreamTiming {
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
  absl::optional<MonotonicTime> lastDownstreamAckReceived() const {
    return last_downstream_ack_received_;
  }
  absl::optional<MonotonicTime> lastDownstreamHeaderRxByteReceived() const {
    return last_downstream_header_rx_byte_received_;
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
  void onLastDownstreamAckReceived(TimeSource& time_source) {
    ASSERT(!last_downstream_ack_received_);
    last_downstream_ack_received_ = time_source.monotonicTime();
  }
  void onLastDownstreamHeaderRxByteReceived(TimeSource& time_source) {
    ASSERT(!last_downstream_header_rx_byte_received_);
    last_downstream_header_rx_byte_received_ = time_source.monotonicTime();
  }

  absl::flat_hash_map<std::string, MonotonicTime> timings_;
  // The time when the last byte of the request was received.
  absl::optional<MonotonicTime> last_downstream_rx_byte_received_;
  // The time when the first byte of the response was sent downstream.
  absl::optional<MonotonicTime> first_downstream_tx_byte_sent_;
  // The time when the last byte of the response was sent downstream.
  absl::optional<MonotonicTime> last_downstream_tx_byte_sent_;
  // The time the TLS handshake completed. Set at connection level.
  absl::optional<MonotonicTime> downstream_handshake_complete_;
  // The time the final ack was received from the client.
  absl::optional<MonotonicTime> last_downstream_ack_received_;
  // The time when the last header byte was received.
  absl::optional<MonotonicTime> last_downstream_header_rx_byte_received_;
};

// Measure the number of bytes sent and received for a stream.
struct BytesMeter {
  BytesMeter() = default;
  uint64_t wireBytesSent() const { return wire_bytes_sent_; }
  uint64_t wireBytesReceived() const { return wire_bytes_received_; }
  uint64_t headerBytesSent() const { return header_bytes_sent_; }
  uint64_t headerBytesReceived() const { return header_bytes_received_; }
  uint64_t decompressedHeaderBytesSent() const { return decompressed_header_bytes_sent_; }
  uint64_t decompressedHeaderBytesReceived() const { return decompressed_header_bytes_received_; }

  void addHeaderBytesSent(uint64_t added_bytes) { header_bytes_sent_ += added_bytes; }
  void addHeaderBytesReceived(uint64_t added_bytes) { header_bytes_received_ += added_bytes; }
  void addDecompressedHeaderBytesSent(uint64_t added_bytes) {
    decompressed_header_bytes_sent_ += added_bytes;
  }
  void addDecompressedHeaderBytesReceived(uint64_t added_bytes) {
    decompressed_header_bytes_received_ += added_bytes;
  }
  void addWireBytesSent(uint64_t added_bytes) { wire_bytes_sent_ += added_bytes; }
  void addWireBytesReceived(uint64_t added_bytes) { wire_bytes_received_ += added_bytes; }

  struct BytesSnapshot {
    SystemTime snapshot_time;
    uint64_t header_bytes_sent{};
    uint64_t header_bytes_received{};
    uint64_t decompressed_header_bytes_sent{};
    uint64_t decompressed_header_bytes_received{};
    uint64_t wire_bytes_sent{};
    uint64_t wire_bytes_received{};
  };
  void takeDownstreamPeriodicLoggingSnapshot(const SystemTime& snapshot_time) {
    downstream_periodic_logging_bytes_snapshot_ = std::make_unique<BytesSnapshot>();

    downstream_periodic_logging_bytes_snapshot_->snapshot_time = snapshot_time;
    downstream_periodic_logging_bytes_snapshot_->header_bytes_sent = header_bytes_sent_;
    downstream_periodic_logging_bytes_snapshot_->header_bytes_received = header_bytes_received_;
    downstream_periodic_logging_bytes_snapshot_->decompressed_header_bytes_sent =
        decompressed_header_bytes_sent_;
    downstream_periodic_logging_bytes_snapshot_->decompressed_header_bytes_received =
        decompressed_header_bytes_received_;
    downstream_periodic_logging_bytes_snapshot_->wire_bytes_sent = wire_bytes_sent_;
    downstream_periodic_logging_bytes_snapshot_->wire_bytes_received = wire_bytes_received_;
  }
  void takeUpstreamPeriodicLoggingSnapshot(const SystemTime& snapshot_time) {
    upstream_periodic_logging_bytes_snapshot_ = std::make_unique<BytesSnapshot>();

    upstream_periodic_logging_bytes_snapshot_->snapshot_time = snapshot_time;
    upstream_periodic_logging_bytes_snapshot_->header_bytes_sent = header_bytes_sent_;
    upstream_periodic_logging_bytes_snapshot_->header_bytes_received = header_bytes_received_;
    upstream_periodic_logging_bytes_snapshot_->decompressed_header_bytes_sent =
        decompressed_header_bytes_sent_;
    upstream_periodic_logging_bytes_snapshot_->decompressed_header_bytes_received =
        decompressed_header_bytes_received_;
    upstream_periodic_logging_bytes_snapshot_->wire_bytes_sent = wire_bytes_sent_;
    upstream_periodic_logging_bytes_snapshot_->wire_bytes_received = wire_bytes_received_;
  }
  const BytesSnapshot* bytesAtLastDownstreamPeriodicLog() const {
    return downstream_periodic_logging_bytes_snapshot_.get();
  }
  const BytesSnapshot* bytesAtLastUpstreamPeriodicLog() const {
    return upstream_periodic_logging_bytes_snapshot_.get();
  }
  // Adds the bytes from `existing` to `this`.
  // Additionally, captures the snapshots on `existing` and adds them to `this`.
  void captureExistingBytesMeter(BytesMeter& existing) {
    // Add bytes accumulated on `this` to the pre-existing periodic bytes collectors.
    if (existing.downstream_periodic_logging_bytes_snapshot_) {
      downstream_periodic_logging_bytes_snapshot_ =
          std::move(existing.downstream_periodic_logging_bytes_snapshot_);
      existing.downstream_periodic_logging_bytes_snapshot_ = nullptr;
    }
    if (existing.upstream_periodic_logging_bytes_snapshot_) {
      upstream_periodic_logging_bytes_snapshot_ =
          std::move(existing.upstream_periodic_logging_bytes_snapshot_);
      existing.upstream_periodic_logging_bytes_snapshot_ = nullptr;
    }

    // Accumulate existing bytes.
    header_bytes_sent_ += existing.header_bytes_sent_;
    header_bytes_received_ += existing.header_bytes_received_;
    decompressed_header_bytes_sent_ += existing.decompressed_header_bytes_sent_;
    decompressed_header_bytes_received_ += existing.decompressed_header_bytes_received_;
    wire_bytes_sent_ += existing.wire_bytes_sent_;
    wire_bytes_received_ += existing.wire_bytes_received_;
  }

private:
  uint64_t header_bytes_sent_{};
  uint64_t header_bytes_received_{};
  uint64_t decompressed_header_bytes_sent_{};
  uint64_t decompressed_header_bytes_received_{};
  uint64_t wire_bytes_sent_{};
  uint64_t wire_bytes_received_{};
  std::unique_ptr<BytesSnapshot> downstream_periodic_logging_bytes_snapshot_;
  std::unique_ptr<BytesSnapshot> upstream_periodic_logging_bytes_snapshot_;
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

  /*
   * @param short string type flag to indicate the noteworthy event of this stream. Mutliple flags
   * could be added and will be concatenated with comma. It should not contain any empty or space
   * characters (' ', '\t', '\f', '\v', '\n', '\r').
   *
   * The short string should not duplicate with the any registered response flags.
   */
  virtual void addCustomFlag(absl::string_view) PURE;

  /**
   * @return std::string& the name of the route. The name is get from the route() and it is
   *         empty if there is no route.
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
   * @param bytes_retransmitted denotes number of bytes to add to total retransmitted bytes.
   */
  virtual void addBytesRetransmitted(uint64_t bytes_retransmitted) PURE;

  /**
   * @return the number of bytes retransmitted by the stream.
   */
  virtual uint64_t bytesRetransmitted() const PURE;

  /**
   * @param packets_retransmitted denotes number of packets to add to total retransmitted packets.
   */
  virtual void addPacketsRetransmitted(uint64_t packets_retransmitted) PURE;

  /**
   * @return the number of packets retransmitted by the stream.
   */
  virtual uint64_t packetsRetransmitted() const PURE;

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
   * @return returns the time source.
   */
  virtual TimeSource& timeSource() const PURE;

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
   * @return the current duration of the request, or the total duration of the request, if ended.
   */
  virtual absl::optional<std::chrono::nanoseconds> currentDuration() const PURE;

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
   * @return all response flags that are set.
   */
  virtual absl::Span<const ResponseFlag> responseFlags() const PURE;

  /**
   * @return response flags encoded as an integer. Every bit of the integer is used to represent a
   * flag. Only flags that are declared in the enum CoreResponseFlag type are supported.
   */
  virtual uint64_t legacyResponseFlags() const PURE;

  /**
   * @return all stream flags that are added.
   */
  virtual absl::string_view customFlags() const PURE;

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
   * @return const Router::VirtualHostConstSharedPtr& Get the virtual host selected for this
   * request.
   */
  virtual const Router::VirtualHostConstSharedPtr& virtualHost() const PURE;

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
  virtual void setDynamicMetadata(const std::string& name, const Protobuf::Struct& value) PURE;

  /**
   * @param name the namespace used in the metadata in reverse DNS format, for example:
   * envoy.test.my_filter.
   * @param value of type protobuf any to set on the namespace.
   */
  virtual void setDynamicTypedMetadata(const std::string& name, const Protobuf::Any& value) PURE;

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

  /**
   * @return absl::string_view the downstream transport failure reason,
   *         e.g. certificate validation failed.
   */
  virtual absl::string_view downstreamTransportFailureReason() const PURE;

  /**
   * @param failure_reason the downstream transport failure reason.
   */
  virtual void setDownstreamTransportFailureReason(absl::string_view failure_reason) PURE;

  /**
   * Checked by routing filters before forwarding a request upstream.
   * @return to override the scheme header to match the upstream transport
   * protocol at routing filters.
   */
  virtual bool shouldSchemeMatchUpstream() const PURE;

  /**
   * Called if a filter decides that the scheme should match the upstream transport protocol
   * @param should_match_upstream true to hint to routing filters to override the scheme header
   * to match the upstream transport protocol.
   */
  virtual void setShouldSchemeMatchUpstream(bool should_match_upstream) PURE;

  /**
   * Checked by streams after finishing serving the request.
   * @return bool true if the connection should be drained once this stream has
   * finished sending and receiving.
   */
  virtual bool shouldDrainConnectionUponCompletion() const PURE;

  /**
   * Set the parent for this StreamInfo. This is used to associate the
   * stream info of an async client with the stream info of the downstream
   * connection.
   */
  virtual void setParentStreamInfo(const StreamInfo& parent_stream_info) PURE;

  /**
   * Get the parent for this StreamInfo, if available.
   */
  virtual OptRef<const StreamInfo> parentStreamInfo() const PURE;

  /**
   * Clear the parent for this StreamInfo.
   */
  virtual void clearParentStreamInfo() PURE;

  /**
   * Called if the connection decides to drain itself after serving this request.
   * @param should_drain true to close the connection once this stream has
   * finished sending and receiving.
   */
  virtual void setShouldDrainConnectionUponCompletion(bool should_drain) PURE;
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
