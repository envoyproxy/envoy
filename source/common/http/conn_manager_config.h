#pragma once

#include "envoy/router/rds.h"

#include "common/http/date_provider.h"

namespace Envoy {
namespace Http {

/**
 * All stats for the connection manager. @see stats_macros.h
 */
// clang-format off
#define ALL_HTTP_CONN_MAN_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER  (downstream_cx_total)                                                                   \
  COUNTER  (downstream_cx_ssl_total)                                                               \
  COUNTER  (downstream_cx_http1_total)                                                             \
  COUNTER  (downstream_cx_websocket_total)                                                         \
  COUNTER  (downstream_cx_http2_total)                                                             \
  COUNTER  (downstream_cx_destroy)                                                                 \
  COUNTER  (downstream_cx_destroy_remote)                                                          \
  COUNTER  (downstream_cx_destroy_local)                                                           \
  COUNTER  (downstream_cx_destroy_active_rq)                                                       \
  COUNTER  (downstream_cx_destroy_local_active_rq)                                                 \
  COUNTER  (downstream_cx_destroy_remote_active_rq)                                                \
  GAUGE    (downstream_cx_active)                                                                  \
  GAUGE    (downstream_cx_ssl_active)                                                              \
  GAUGE    (downstream_cx_http1_active)                                                            \
  GAUGE    (downstream_cx_websocket_active)                                                        \
  GAUGE    (downstream_cx_http2_active)                                                            \
  COUNTER  (downstream_cx_protocol_error)                                                          \
  HISTOGRAM(downstream_cx_length_ms)                                                               \
  COUNTER  (downstream_cx_rx_bytes_total)                                                          \
  GAUGE    (downstream_cx_rx_bytes_buffered)                                                       \
  COUNTER  (downstream_cx_tx_bytes_total)                                                          \
  GAUGE    (downstream_cx_tx_bytes_buffered)                                                       \
  COUNTER  (downstream_cx_drain_close)                                                             \
  COUNTER  (downstream_cx_idle_timeout)                                                            \
  COUNTER  (downstream_flow_control_paused_reading_total)                                          \
  COUNTER  (downstream_flow_control_resumed_reading_total)                                         \
  COUNTER  (downstream_rq_total)                                                                   \
  COUNTER  (downstream_rq_http1_total)                                                             \
  COUNTER  (downstream_rq_http2_total)                                                             \
  GAUGE    (downstream_rq_active)                                                                  \
  COUNTER  (downstream_rq_response_before_rq_complete)                                             \
  COUNTER  (downstream_rq_rx_reset)                                                                \
  COUNTER  (downstream_rq_tx_reset)                                                                \
  COUNTER  (downstream_rq_non_relative_path)                                                       \
  COUNTER  (downstream_rq_ws_on_non_ws_route)                                                      \
  COUNTER  (downstream_rq_too_large)                                                               \
  COUNTER  (downstream_rq_1xx)                                                                     \
  COUNTER  (downstream_rq_2xx)                                                                     \
  COUNTER  (downstream_rq_3xx)                                                                     \
  COUNTER  (downstream_rq_4xx)                                                                     \
  COUNTER  (downstream_rq_5xx)                                                                     \
  HISTOGRAM(downstream_rq_time)                                                                    \
  COUNTER  (rs_too_large)
// clang-format on

/**
 * Wrapper struct for connection manager stats. @see stats_macros.h
 */
struct ConnectionManagerNamedStats {
  ALL_HTTP_CONN_MAN_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

struct ConnectionManagerStats {
  ConnectionManagerNamedStats named_;
  std::string prefix_;
  Stats::Scope& scope_;
};

/**
 * Connection manager tracing specific stats. @see stats_macros.h
 */
// clang-format off
#define CONN_MAN_TRACING_STATS(COUNTER)                                                            \
  COUNTER(random_sampling)                                                                         \
  COUNTER(service_forced)                                                                          \
  COUNTER(client_enabled)                                                                          \
  COUNTER(not_traceable)                                                                           \
  COUNTER(health_check)
// clang-format on

/**
 * Wrapper struct for connection manager tracing stats. @see stats_macros.h
 */
struct ConnectionManagerTracingStats {
  CONN_MAN_TRACING_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Configuration for tracing which is set on the connection manager level.
 * Http Tracing can be enabled/disabled on a per connection manager basis.
 * Here we specify some specific for connection manager settings.
 */
struct TracingConnectionManagerConfig {
  Tracing::OperationName operation_name_;
  std::vector<Http::LowerCaseString> request_headers_for_tags_;
};

typedef std::unique_ptr<TracingConnectionManagerConfig> TracingConnectionManagerConfigPtr;

/**
 * Connection manager per listener stats. @see stats_macros.h
 */
// clang-format off
#define CONN_MAN_LISTENER_STATS(COUNTER)                                                           \
  COUNTER(downstream_rq_1xx)                                                                       \
  COUNTER(downstream_rq_2xx)                                                                       \
  COUNTER(downstream_rq_3xx)                                                                       \
  COUNTER(downstream_rq_4xx)                                                                       \
  COUNTER(downstream_rq_5xx)
// clang-format on

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
enum class ClientCertDetailsType { Cert, Subject, SAN, URI, DNS };

/**
 * Abstract configuration for the connection manager.
 */
class ConnectionManagerConfig {
public:
  virtual ~ConnectionManagerConfig() {}

  /**
   *  @return const std::list<AccessLog::InstanceSharedPtr>& the access logs to write to.
   */
  virtual const std::list<AccessLog::InstanceSharedPtr>& accessLogs() PURE;

  /**
   * Called to create a codec for the connection manager. This function will be called when the
   * first byte of application data is received. This is done to support handling of ALPN, protocol
   * detection, etc.
   * @param connection supplies the owning connection.
   * @param data supplies the currently available read data.
   * @param callbacks supplies the callbacks to install into the codec.
   * @return a codec or nullptr if no codec can be created.
   */
  virtual ServerConnectionPtr createCodec(Network::Connection& connection,
                                          const Buffer::Instance& data,
                                          ServerConnectionCallbacks& callbacks) PURE;

  /**
   * @return DateProvider& the date provider to use for
   */
  virtual DateProvider& dateProvider() PURE;

  /**
   * @return the time in milliseconds the connection manager will wait betwen issuing a "shutdown
   *         notice" to the time it will issue a full GOAWAY and not accept any new streams.
   */
  virtual std::chrono::milliseconds drainTimeout() PURE;

  /**
   * @return FilterChainFactory& the HTTP level filter factory to build the connection's filter
   *         chain.
   */
  virtual FilterChainFactory& filterFactory() PURE;

  /**
   * @return whether the connection manager will generate a fresh x-request-id if the request does
   *         not have one.
   */
  virtual bool generateRequestId() PURE;

  /**
   * @return optional idle timeout for incoming connection manager connections.
   */
  virtual const absl::optional<std::chrono::milliseconds>& idleTimeout() PURE;

  /**
   * @return Router::RouteConfigProvider& the configuration provider used to acquire a route
   *         config for each request flow.
   */
  virtual Router::RouteConfigProvider& routeConfigProvider() PURE;

  /**
   * @return const std::string& the server name to write into responses.
   */
  virtual const std::string& serverName() PURE;

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
  virtual bool useRemoteAddress() PURE;

  /**
   * @return uint32_t the number of trusted proxy hops in front of this Envoy instance, for
   *         the purposes of XFF processing.
   */
  virtual uint32_t xffNumTrustedHops() const PURE;

  /**
   * @return ForwardClientCertType the configuration of how to forward the client cert information.
   */
  virtual ForwardClientCertType forwardClientCert() PURE;

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
   * @return supplies the http1 settings.
   */
  virtual const Http::Http1Settings& http1Settings() const PURE;
};
}
} // namespace Envoy
