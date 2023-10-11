#pragma once

#include "envoy/config/config_provider.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/http/early_header_mutation.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_validator.h"
#include "envoy/http/original_ip_detection.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/router/rds.h"
#include "envoy/router/scopes.h"
#include "envoy/stats/scope.h"
#include "envoy/tracing/tracer.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/http/date_provider.h"
#include "source/common/local_reply/local_reply.h"
#include "source/common/network/utility.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/tracing/tracer_config_impl.h"

namespace Envoy {
namespace Http {

/**
 * All stats for the connection manager. @see stats_macros.h
 */
#define ALL_HTTP_CONN_MAN_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(downstream_cx_delayed_close_timeout)                                                     \
  COUNTER(downstream_cx_destroy)                                                                   \
  COUNTER(downstream_cx_destroy_active_rq)                                                         \
  COUNTER(downstream_cx_destroy_local)                                                             \
  COUNTER(downstream_cx_destroy_local_active_rq)                                                   \
  COUNTER(downstream_cx_destroy_remote)                                                            \
  COUNTER(downstream_cx_destroy_remote_active_rq)                                                  \
  COUNTER(downstream_cx_drain_close)                                                               \
  COUNTER(downstream_cx_http1_total)                                                               \
  COUNTER(downstream_cx_http2_total)                                                               \
  COUNTER(downstream_cx_http3_total)                                                               \
  COUNTER(downstream_cx_idle_timeout)                                                              \
  COUNTER(downstream_cx_max_duration_reached)                                                      \
  COUNTER(downstream_cx_max_requests_reached)                                                      \
  COUNTER(downstream_cx_overload_disable_keepalive)                                                \
  COUNTER(downstream_cx_protocol_error)                                                            \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  COUNTER(downstream_cx_ssl_total)                                                                 \
  COUNTER(downstream_cx_total)                                                                     \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  COUNTER(downstream_cx_upgrades_total)                                                            \
  COUNTER(downstream_flow_control_paused_reading_total)                                            \
  COUNTER(downstream_flow_control_resumed_reading_total)                                           \
  COUNTER(downstream_rq_1xx)                                                                       \
  COUNTER(downstream_rq_2xx)                                                                       \
  COUNTER(downstream_rq_3xx)                                                                       \
  COUNTER(downstream_rq_4xx)                                                                       \
  COUNTER(downstream_rq_5xx)                                                                       \
  COUNTER(downstream_rq_completed)                                                                 \
  COUNTER(downstream_rq_failed_path_normalization)                                                 \
  COUNTER(downstream_rq_http1_total)                                                               \
  COUNTER(downstream_rq_http2_total)                                                               \
  COUNTER(downstream_rq_http3_total)                                                               \
  COUNTER(downstream_rq_idle_timeout)                                                              \
  COUNTER(downstream_rq_non_relative_path)                                                         \
  COUNTER(downstream_rq_overload_close)                                                            \
  COUNTER(downstream_rq_redirected_with_normalized_path)                                           \
  COUNTER(downstream_rq_rejected_via_ip_detection)                                                 \
  COUNTER(downstream_rq_response_before_rq_complete)                                               \
  COUNTER(downstream_rq_rx_reset)                                                                  \
  COUNTER(downstream_rq_too_many_premature_resets)                                                 \
  COUNTER(downstream_rq_timeout)                                                                   \
  COUNTER(downstream_rq_header_timeout)                                                            \
  COUNTER(downstream_rq_too_large)                                                                 \
  COUNTER(downstream_rq_total)                                                                     \
  COUNTER(downstream_rq_tx_reset)                                                                  \
  COUNTER(downstream_rq_max_duration_reached)                                                      \
  COUNTER(downstream_rq_ws_on_non_ws_route)                                                        \
  COUNTER(rs_too_large)                                                                            \
  GAUGE(downstream_cx_active, Accumulate)                                                          \
  GAUGE(downstream_cx_http1_active, Accumulate)                                                    \
  GAUGE(downstream_cx_http2_active, Accumulate)                                                    \
  GAUGE(downstream_cx_http3_active, Accumulate)                                                    \
  GAUGE(downstream_cx_rx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_cx_ssl_active, Accumulate)                                                      \
  GAUGE(downstream_cx_tx_bytes_buffered, Accumulate)                                               \
  GAUGE(downstream_cx_upgrades_active, Accumulate)                                                 \
  GAUGE(downstream_rq_active, Accumulate)                                                          \
  HISTOGRAM(downstream_cx_length_ms, Milliseconds)                                                 \
  HISTOGRAM(downstream_rq_time, Milliseconds)

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct ConnectionManagerNamedStats {
  ALL_HTTP_CONN_MAN_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

struct ConnectionManagerStats {
  ConnectionManagerStats(ConnectionManagerNamedStats&& named_stats, const std::string& prefix,
                         Stats::Scope& scope)
      : named_(std::move(named_stats)), prefix_(prefix),
        prefix_stat_name_storage_(prefix, scope.symbolTable()), scope_(scope) {}

  Stats::StatName prefixStatName() const { return prefix_stat_name_storage_.statName(); }

  ConnectionManagerNamedStats named_;
  std::string prefix_;
  Stats::StatNameManagedStorage prefix_stat_name_storage_;
  Stats::Scope& scope_;
};

/**
 * Connection manager tracing specific stats. @see stats_macros.h
 */
#define CONN_MAN_TRACING_STATS(COUNTER)                                                            \
  COUNTER(random_sampling)                                                                         \
  COUNTER(service_forced)                                                                          \
  COUNTER(client_enabled)                                                                          \
  COUNTER(not_traceable)                                                                           \
  COUNTER(health_check)

/**
 * Wrapper struct for connection manager tracing stats. @see stats_macros.h
 */
struct ConnectionManagerTracingStats {
  CONN_MAN_TRACING_STATS(GENERATE_COUNTER_STRUCT)
};

using TracingConnectionManagerConfig = Tracing::ConnectionManagerTracingConfigImpl;
using TracingConnectionManagerConfigPtr = std::unique_ptr<TracingConnectionManagerConfig>;

/**
 * Connection manager per listener stats. @see stats_macros.h
 */
#define CONN_MAN_LISTENER_STATS(COUNTER)                                                           \
  COUNTER(downstream_rq_1xx)                                                                       \
  COUNTER(downstream_rq_2xx)                                                                       \
  COUNTER(downstream_rq_3xx)                                                                       \
  COUNTER(downstream_rq_4xx)                                                                       \
  COUNTER(downstream_rq_5xx)                                                                       \
  COUNTER(downstream_rq_completed)

/**
 * Wrapper struct for connection manager listener stats. @see stats_macros.h
 */
struct ConnectionManagerListenerStats {
  CONN_MAN_LISTENER_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for how to forward client certs.
 */
enum class ForwardClientCertType {
  ForwardOnly,
  AppendForward,
  SanitizeSet,
  Sanitize,
  AlwaysForwardOnly
};

/**
 * Configuration for the fields of the client cert, used for populating the current client cert
 * information to the next hop.
 */
enum class ClientCertDetailsType { Cert, Chain, Subject, URI, DNS };

/**
 * Type that indicates how port should be stripped from Host header.
 */
enum class StripPortType {
  // Removes the port from host/authority header only if the port matches with the listener port.
  MatchingHost,
  // Removes any port from host/authority header.
  Any,
  // Keeps the port in host/authority header as is.
  None
};

/**
 * Configuration for what addresses should be considered internal beyond the defaults.
 */
class InternalAddressConfig {
public:
  virtual ~InternalAddressConfig() = default;
  virtual bool isInternalAddress(const Network::Address::Instance& address) const PURE;
};

/**
 * Determines if an address is internal based on whether it is an RFC1918 ip address.
 */
class DefaultInternalAddressConfig : public Http::InternalAddressConfig {
public:
  bool isInternalAddress(const Network::Address::Instance& address) const override {
    return Network::Utility::isInternalAddress(address);
  }
};

/**
 * Abstract configuration for the connection manager.
 */
class ConnectionManagerConfig {
public:
  using HttpConnectionManagerProto =
      envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager;

  virtual ~ConnectionManagerConfig() = default;

  /**
   * @return RequestIDExtensionSharedPtr The request id utilities instance to use.
   */
  virtual const RequestIDExtensionSharedPtr& requestIDExtension() PURE;

  /**
   *  @return const std::list<AccessLog::InstanceSharedPtr>& the access logs to write to.
   */
  virtual const std::list<AccessLog::InstanceSharedPtr>& accessLogs() PURE;

  /**
   * @return const absl::optional<std::chrono::milliseconds>& the interval to flush the access logs.
   */
  virtual const absl::optional<std::chrono::milliseconds>& accessLogFlushInterval() PURE;

  // If set to true, access log will be flushed when a new HTTP request is received, after request
  // headers have been evaluated, and before attempting to establish a connection with the upstream.
  virtual bool flushAccessLogOnNewRequest() PURE;

  virtual bool flushAccessLogOnTunnelSuccessfullyEstablished() const PURE;

  /**
   * Called to create a codec for the connection manager. This function will be called when the
   * first byte of application data is received. This is done to support handling of ALPN, protocol
   * detection, etc.
   * @param connection supplies the owning connection.
   * @param data supplies the currently available read data.
   * @param callbacks supplies the callbacks to install into the codec.
   * @param overload_manager supplies overload manager that the codec can
   * integrate with.
   * @return a codec or nullptr if no codec can be created.
   */
  virtual ServerConnectionPtr createCodec(Network::Connection& connection,
                                          const Buffer::Instance& data,
                                          ServerConnectionCallbacks& callbacks,
                                          Server::OverloadManager& overload_manager) PURE;

  /**
   * @return DateProvider& the date provider to use for
   */
  virtual DateProvider& dateProvider() PURE;

  /**
   * @return the time in milliseconds the connection manager will wait between issuing a "shutdown
   *         notice" to the time it will issue a full GOAWAY and not accept any new streams.
   */
  virtual std::chrono::milliseconds drainTimeout() const PURE;

  /**
   * @return FilterChainFactory& the HTTP level filter factory to build the connection's filter
   *         chain.
   */
  virtual FilterChainFactory& filterFactory() PURE;

  /**
   * @return whether the connection manager will generate a fresh x-request-id if the request does
   *         not have one.
   */
  virtual bool generateRequestId() const PURE;

  /**
   * @return whether the x-request-id should not be reset on edge entry inside mesh
   */
  virtual bool preserveExternalRequestId() const PURE;

  /**
   * @return whether the x-request-id should always be set in the response.
   */
  virtual bool alwaysSetRequestIdInResponse() const PURE;

  /**
   * @return optional idle timeout for incoming connection manager connections.
   */
  virtual absl::optional<std::chrono::milliseconds> idleTimeout() const PURE;

  /**
   * @return if the connection manager does routing base on router config, e.g. a Server::Admin impl
   * has no route config.
   */
  virtual bool isRoutable() const PURE;

  /**
   * @return optional maximum connection duration timeout for manager connections.
   */
  virtual absl::optional<std::chrono::milliseconds> maxConnectionDuration() const PURE;

  /**
   * @return maximum request headers size the connection manager will accept.
   */
  virtual uint32_t maxRequestHeadersKb() const PURE;

  /**
   * @return maximum number of request headers the codecs will accept.
   */
  virtual uint32_t maxRequestHeadersCount() const PURE;

  /**
   * @return per-stream idle timeout for incoming connection manager connections. Zero indicates a
   *         disabled idle timeout.
   */
  virtual std::chrono::milliseconds streamIdleTimeout() const PURE;

  /**
   * @return request timeout for incoming connection manager connections. Zero indicates
   *         a disabled request timeout.
   */
  virtual std::chrono::milliseconds requestTimeout() const PURE;

  /**
   * @return request header timeout for incoming connection manager connections. Zero indicates a
   *         disabled request header timeout.
   */
  virtual std::chrono::milliseconds requestHeadersTimeout() const PURE;

  /**
   * @return delayed close timeout for downstream HTTP connections. Zero indicates a disabled
   *         timeout. See http_connection_manager.proto for a detailed description of this timeout.
   */
  virtual std::chrono::milliseconds delayedCloseTimeout() const PURE;

  /**
   * @return maximum duration time to keep alive stream
   */
  virtual absl::optional<std::chrono::milliseconds> maxStreamDuration() const PURE;

  /**
   * @return Router::RouteConfigProvider* the configuration provider used to acquire a route
   *         config for each request flow. Pointer ownership is _not_ transferred to the caller of
   *         this function. This will return nullptr when scoped routing is enabled.
   */
  virtual Router::RouteConfigProvider* routeConfigProvider() PURE;

  /**
   * @return Config::ConfigProvider* the configuration provider used to acquire scoped routing
   * configuration for each request flow. Pointer ownership is _not_ transferred to the caller of
   * this function. This will return nullptr when scoped routing is not enabled.
   */
  virtual Config::ConfigProvider* scopedRouteConfigProvider() PURE;

  /**
   * @return OptRef<Router::ScopeKeyBuilder> the scope key builder to calculate the scope key.
   * This will return nullptr when scoped routing is not enabled.
   */
  virtual OptRef<const Router::ScopeKeyBuilder> scopeKeyBuilder() PURE;

  /**
   * @return const std::string& the server name to write into responses.
   */
  virtual const std::string& serverName() const PURE;

  /**
   * @return ServerHeaderTransformation the transformation to apply to Server response headers.
   */
  virtual HttpConnectionManagerProto::ServerHeaderTransformation
  serverHeaderTransformation() const PURE;

  /**
   * @return const absl::optional<std::string> the scheme name to write into requests.
   */
  virtual const absl::optional<std::string>& schemeToSet() const PURE;

  /**
   * @return ConnectionManagerStats& the stats to write to.
   */
  virtual ConnectionManagerStats& stats() PURE;

  /**
   * @return ConnectionManagerTracingStats& the stats to write to.
   */
  virtual ConnectionManagerTracingStats& tracingStats() PURE;

  /**
   * @return bool whether to use the remote address for populating XFF, determining internal request
   *         status, etc. or to assume that XFF will already be populated with the remote address.
   */
  virtual bool useRemoteAddress() const PURE;

  /**
   * @return InternalAddressConfig configuration for user defined internal addresses.
   */
  virtual const InternalAddressConfig& internalAddressConfig() const PURE;

  /**
   * @return uint32_t the number of trusted proxy hops in front of this Envoy instance, for
   *         the purposes of XFF processing.
   */
  virtual uint32_t xffNumTrustedHops() const PURE;

  /**
   * @return bool don't append the remote address to XFF? This overrides the behavior of
   *              useRemoteAddress() and may be used when XFF should not be modified but we still
   *              want to avoid trusting incoming XFF in remote IP determination.
   */
  virtual bool skipXffAppend() const PURE;

  /**
   * @return const absl::optional<std::string>& value of via header to add to requests and response
   *                                            headers if set.
   */
  virtual const std::string& via() const PURE;

  /**
   * @return ForwardClientCertType the configuration of how to forward the client cert information.
   */
  virtual ForwardClientCertType forwardClientCert() const PURE;

  /**
   * @return vector of ClientCertDetailsType the configuration of the current client cert's details
   * to be forwarded.
   */
  virtual const std::vector<ClientCertDetailsType>& setCurrentClientCertDetails() const PURE;

  /**
   * @return local address.
   * Gives richer information in case of internal requests.
   */
  virtual const Network::Address::Instance& localAddress() PURE;

  /**
   * @return custom user agent for internal requests for better debugging. Must be configured to
   *         be enabled. User agent will only overwritten if it doesn't already exist. If enabled,
   *         the same user agent will be written to the x-envoy-downstream-service-cluster header.
   */
  virtual const absl::optional<std::string>& userAgent() PURE;

  /**
   *  @return TracerSharedPtr Tracer to use.
   */
  virtual Tracing::TracerSharedPtr tracer() PURE;

  /**
   * @return tracing config.
   */
  virtual const TracingConnectionManagerConfig* tracingConfig() PURE;

  /**
   * @return ConnectionManagerListenerStats& the stats to write to.
   */
  virtual ConnectionManagerListenerStats& listenerStats() PURE;

  /**
   * @return bool supplies if the HttpConnectionManager should proxy the Expect: 100-Continue
   */
  virtual bool proxy100Continue() const PURE;

  /**
   * @return bool supplies if the HttpConnectionManager should handle invalid HTTP with a stream
   * error or connection error.
   */
  virtual bool streamErrorOnInvalidHttpMessaging() const PURE;

  /**
   * @return supplies the http1 settings.
   */
  virtual const Http::Http1Settings& http1Settings() const PURE;

  /**
   * @return if the HttpConnectionManager should normalize url following RFC3986
   */
  virtual bool shouldNormalizePath() const PURE;

  /**
   * @return if the HttpConnectionManager should merge two or more adjacent slashes in the path into
   * one.
   */
  virtual bool shouldMergeSlashes() const PURE;

  /**
   * @return port strip type from host/authority header.
   */
  virtual StripPortType stripPortType() const PURE;

  /**
   * @return the action HttpConnectionManager should take when receiving client request
   * headers containing underscore characters.
   */
  virtual envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
  headersWithUnderscoresAction() const PURE;

  /**
   * @return LocalReply configuration which supplies mapping for local reply generated by Envoy.
   */
  virtual const LocalReply::LocalReply& localReply() const PURE;

  /**
   * @return the action HttpConnectionManager should take when receiving client request
   * with URI path containing %2F, %2f, %5c or %5C sequences.
   */
  virtual envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      PathWithEscapedSlashesAction
      pathWithEscapedSlashesAction() const PURE;

  /**
   * @return vector of OriginalIPDetectionSharedPtr original IP detection extensions.
   */
  virtual const std::vector<OriginalIPDetectionSharedPtr>&
  originalIpDetectionExtensions() const PURE;

  virtual const std::vector<EarlyHeaderMutationPtr>& earlyHeaderMutationExtensions() const PURE;

  /**
   * @return if the HttpConnectionManager should remove trailing host dot from host/authority
   * header.
   */
  virtual bool shouldStripTrailingHostDot() const PURE;
  /**
   * @return maximum requests for downstream.
   */
  virtual uint64_t maxRequestsPerConnection() const PURE;
  /**
   * @return the config describing if/how to write the Proxy-Status HTTP response header.
   * If nullptr, don't write the Proxy-Status HTTP response header.
   */
  virtual const HttpConnectionManagerProto::ProxyStatusConfig* proxyStatusConfig() const PURE;

  /**
   * Creates new header validator. This method always returns nullptr unless the `ENVOY_ENABLE_UHV`
   * pre-processor variable is defined.
   * @param protocol HTTP protocol version that is to be validated.
   * @return pointer to the header validator.
   *         If nullptr, header validation will not be done.
   */
  virtual ServerHeaderValidatorPtr makeHeaderValidator(Protocol protocol) PURE;

  /**
   * @return whether to append the x-forwarded-port header.
   */
  virtual bool appendXForwardedPort() const PURE;

  /**
   * @return whether the HCM will insert ProxyProtocolFilterState into the filter state at the
   *         Connection Lifetime.
   */
  virtual bool addProxyProtocolConnectionState() const PURE;
};
} // namespace Http
} // namespace Envoy
