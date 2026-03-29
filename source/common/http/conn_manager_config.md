# Connection Manager Config Interface — `conn_manager_config.h`

**File:** `source/common/http/conn_manager_config.h`

Defines the `ConnectionManagerConfig` pure virtual interface that `ConnectionManagerImpl`
depends on. The concrete implementation lives in
`source/extensions/filters/network/http_connection_manager/`. Also defines all HCM stats
macros and supporting types.

---

## Role in the Dependency Graph

```mermaid
flowchart TD
    Proto[HttpConnectionManager proto\nhcm.proto] --> Impl[ConnectionManagerFilterConfigImpl\nextensions/filters/network/hcm/]
    Impl -.->|implements| Config[ConnectionManagerConfig\nconn_manager_config.h]
    Config <-- CMI[ConnectionManagerImpl\nconn_manager_impl.h]
    AdminImpl[AdminImpl] -.->|implements| Config
    TestConfig[Test doubles] -.->|implements| Config
```

`ConnectionManagerImpl` holds a reference `ConnectionManagerConfig& config_` and calls
only the pure virtual methods below — it never includes the extensions directory.

---

## Stats Structures

### `ConnectionManagerStats` (`ALL_HTTP_CONN_MAN_STATS`)

All metrics are under the listener's stats prefix (e.g. `http.ingress.`).

**Counters:**

| Stat | Meaning |
|---|---|
| `downstream_cx_total` | Total downstream connections accepted |
| `downstream_cx_http1_total/2/3` | Connections by protocol |
| `downstream_cx_ssl_total` | TLS connections |
| `downstream_cx_upgrades_total` | Upgraded connections (WebSocket, CONNECT) |
| `downstream_cx_destroy` | Connections destroyed |
| `downstream_cx_destroy_active_rq` | Connections closed with active requests |
| `downstream_cx_destroy_local/remote` | Closed by Envoy / by client |
| `downstream_cx_destroy_local_active_rq / remote_active_rq` | Breakdown of above |
| `downstream_cx_idle_timeout` | Idle timeout fired |
| `downstream_cx_max_duration_reached` | Max connection duration exceeded |
| `downstream_cx_max_requests_reached` | `max_requests_per_connection` limit hit |
| `downstream_cx_protocol_error` | Protocol error on connection |
| `downstream_cx_rx_bytes_total / tx_bytes_total` | Connection-level bytes |
| `downstream_cx_drain_close` | Connection closed during drain |
| `downstream_cx_delayed_close_timeout` | Delayed close timer fired |
| `downstream_cx_overload_disable_keepalive` | Keep-alive disabled due to overload |
| `downstream_rq_total` | Total requests |
| `downstream_rq_http1/2/3_total` | Requests by protocol |
| `downstream_rq_1xx/2xx/3xx/4xx/5xx` | Response class counters |
| `downstream_rq_completed` | Requests with final response sent |
| `downstream_rq_idle_timeout` | Per-stream idle timeout fired |
| `downstream_rq_timeout` | Request timeout fired |
| `downstream_rq_header_timeout` | Request header timeout fired |
| `downstream_rq_max_duration_reached` | Per-request max duration exceeded |
| `downstream_rq_too_large` | Request body exceeded `max_request_headers_kb` |
| `downstream_rq_non_relative_path` | Non-relative path rejected |
| `downstream_rq_failed_path_normalization` | Path normalization failed |
| `downstream_rq_redirected_with_normalized_path` | Redirected after path normalization |
| `downstream_rq_rejected_via_ip_detection` | Request rejected by IP detection extension |
| `downstream_rq_response_before_rq_complete` | Response sent before request body fully received |
| `downstream_rq_rx_reset / tx_reset` | Stream resets received/sent |
| `downstream_rq_too_many_premature_resets` | Premature reset flood protection triggered |
| `downstream_rq_overload_close` | Request closed due to overload action |
| `downstream_rq_ws_on_non_ws_route` | WebSocket upgrade on non-WS route |
| `downstream_flow_control_paused/resumed_reading_total` | Read disable/enable events |
| `rs_too_large` | Response size exceeded buffer limit |

**Gauges:**

| Stat | Meaning |
|---|---|
| `downstream_cx_active` | Currently active connections |
| `downstream_cx_http1/2/3_active` | Active connections by protocol |
| `downstream_cx_ssl_active` | Active TLS connections |
| `downstream_cx_upgrades_active` | Active upgraded connections |
| `downstream_cx_http1_soft_drain` | H1 connections in soft drain |
| `downstream_cx_rx/tx_bytes_buffered` | Bytes buffered in read/write buffers |
| `downstream_rq_active` | Currently active requests |

**Histograms:**

| Stat | Unit |
|---|---|
| `downstream_cx_length_ms` | Connection lifetime in ms |
| `downstream_rq_time` | Request time in ms |

### Tracing Stats (`CONN_MAN_TRACING_STATS`)

| Stat | Meaning |
|---|---|
| `random_sampling` | Trace started via random sampling |
| `service_forced` | Trace forced by x-envoy-force-trace |
| `client_enabled` | Trace enabled by client (x-b3-sampled=1) |
| `not_traceable` | Not traceable (x-b3-sampled=0) |
| `health_check` | Health check request (not traced by default) |

### Listener Stats (`CONN_MAN_LISTENER_STATS`)

Per-listener counters for `downstream_rq_1xx/2xx/3xx/4xx/5xx/completed` — duplicated
under the listener prefix for per-listener aggregation.

---

## Supporting Types

| Type | Values | Description |
|---|---|---|
| `ForwardClientCertType` | `ForwardOnly`, `AppendForward`, `SanitizeSet`, `Sanitize`, `AlwaysForwardOnly` | How to handle `x-forwarded-client-cert` |
| `ClientCertDetailsType` | `Cert`, `Chain`, `Subject`, `URI`, `DNS` | Fields to include in XFCC header |
| `StripPortType` | `MatchingHost`, `Any`, `None` | Port stripping behavior on `:authority` |
| `InternalAddressConfig` | abstract class | Override which addresses are "internal" |
| `DefaultInternalAddressConfig` | `isInternalAddress → false` | No internal address override |

---

## `ConnectionManagerConfig` Interface — Method Reference

### Codec / Connection Factory

| Method | Returns | Description |
|---|---|---|
| `createCodec(conn, data, callbacks, overload)` | `ServerConnectionPtr` | Factory — called on first byte; detects H1/H2/H3 from ALPN/data |
| `dateProvider()` | `DateProvider&` | Provides `Date:` header value |
| `filterFactory()` | `FilterChainFactory&` | Builds the HTTP filter chain per request |

### Timeouts

| Method | Returns | Description |
|---|---|---|
| `idleTimeout()` | `optional<ms>` | Connection-level idle timeout |
| `maxConnectionDuration()` | `optional<ms>` | Maximum time a connection is kept alive |
| `http1SafeMaxConnectionDuration()` | `bool` | If true, H1 client drives close on max duration (no FIN from Envoy while streams are active) |
| `streamIdleTimeout()` | `ms` | Per-request idle timeout (no data on stream) |
| `streamFlushTimeout()` | `optional<ms>` | Per-request flush timeout (response bytes in write buffer but not drained) |
| `requestTimeout()` | `ms` | Full request timeout (headers + body) |
| `requestHeadersTimeout()` | `ms` | Timeout to receive complete request headers |
| `delayedCloseTimeout()` | `ms` | After FIN sent, time to wait for client FIN before hard close |
| `maxStreamDuration()` | `optional<ms>` | Maximum per-stream duration regardless of activity |
| `drainTimeout()` | `ms` | Time between "shutdown notice" GOAWAY and final GOAWAY on drain |

### Request ID

| Method | Description |
|---|---|
| `requestIDExtension()` | Extension for generating / propagating `x-request-id` |
| `generateRequestId()` | Whether to generate a fresh request ID if absent |
| `preserveExternalRequestId()` | Whether to preserve incoming `x-request-id` on edge entry |
| `alwaysSetRequestIdInResponse()` | Whether to always echo `x-request-id` in responses |

### Routing

| Method | Description |
|---|---|
| `routeConfigProvider()` | Returns the static/RDS route config provider; null when scoped routing is active |
| `scopedRouteConfigProvider()` | Returns the scoped route config provider; null when not scoped |
| `scopeKeyBuilder()` | Returns the scope key builder for scoped routing; null when not scoped |
| `isRoutable()` | False for Admin — no route config |

### Headers and Security

| Method | Description |
|---|---|
| `maxRequestHeadersKb()` | Max size of request headers in KB |
| `maxRequestHeadersCount()` | Max number of request headers |
| `maxRequestsPerConnection()` | 0 = unlimited |
| `useRemoteAddress()` | Trust remote IP for XFF and internal detection |
| `xffNumTrustedHops()` | Number of trusted proxy hops for XFF |
| `skipXffAppend()` | Don't append remote IP to XFF even if `useRemoteAddress = true` |
| `internalAddressConfig()` | Custom internal address determination |
| `headersWithUnderscoresAction()` | ALLOW / DROP / REJECT for `_` in header names |
| `forwardClientCert()` | XFCC header policy |
| `setCurrentClientCertDetails()` | Which cert fields to include in XFCC |
| `forwardClientCertMatcher()` | Per-request XFCC policy matcher; null = use static policy |
| `earlyHeaderMutationExtensions()` | Extensions that mutate headers before routing |
| `originalIpDetectionExtensions()` | Extensions for original IP detection (before XFF) |
| `shouldNormalizePath()` | RFC 3986 path normalization |
| `shouldMergeSlashes()` | Collapse `//` sequences in path |
| `stripPortType()` | Port stripping from `:authority` |
| `shouldStripTrailingHostDot()` | Remove trailing `.` from host |
| `pathWithEscapedSlashesAction()` | Behaviour for `%2F`, `%5C` in paths |
| `proxy100Continue()` | Whether to proxy `100 Continue` responses |
| `streamErrorOnInvalidHttpMessaging()` | Stream error vs. connection error for invalid HTTP |
| `http1Settings()` | H1-specific settings |
| `makeHeaderValidator(protocol)` | Factory for UHV (Unified Header Validator); null if UHV disabled |

### Server Identity / Response

| Method | Description |
|---|---|
| `serverName()` | Value for `server:` response header |
| `serverHeaderTransformation()` | `OVERWRITE`, `APPEND_IF_ABSENT`, or `PASS_THROUGH` |
| `schemeToSet()` | Fixed `:scheme` to inject on requests |
| `shouldSchemeMatchUpstream()` | Overwrite `:scheme` to match upstream transport (http/https) |
| `via()` | Value to append to `via:` header |
| `appendXForwardedPort()` | Whether to add `x-forwarded-port` |
| `appendLocalOverload()` | Whether to add `x-envoy-local-overloaded` on overload-dropped responses |

### Observability

| Method | Description |
|---|---|
| `accessLogs()` | Vector of access log sinks |
| `accessLogFlushInterval()` | Periodic flush interval for streaming access logs |
| `flushAccessLogOnNewRequest()` | Flush access log when request headers arrive (pre-upstream) |
| `flushAccessLogOnTunnelSuccessfullyEstablished()` | Flush access log when tunnel is established |
| `tracer()` | Distributed tracing tracer |
| `tracingConfig()` | Tracing configuration (sampling, custom tags) |
| `stats()` | `ConnectionManagerStats&` |
| `tracingStats()` | `ConnectionManagerTracingStats&` |
| `listenerStats()` | Per-listener `ConnectionManagerListenerStats&` |

### Miscellaneous

| Method | Description |
|---|---|
| `localAddress()` | Listener local address (for internal request enrichment) |
| `userAgent()` | Custom user-agent override for internal requests |
| `localReply()` | `LocalReply` config for mapping/overriding locally generated responses |
| `proxyStatusConfig()` | Config for `Proxy-Status` response header; null = disabled |
| `addProxyProtocolConnectionState()` | Insert PROXY protocol filter state at connection lifetime |
| `httpsDestinationPorts()` | Treat these PROXY protocol destination ports as HTTPS |
| `httpDestinationPorts()` | Treat these PROXY protocol destination ports as HTTP |

---

## Key Design Points

- All methods are `PURE` virtual — `ConnectionManagerImpl` is fully decoupled from config parsing
- `createCodec()` is lazy — called on first data byte so ALPN negotiation can complete first
- Two route config paths are mutually exclusive: `routeConfigProvider` (static/RDS) vs.
  `scopedRouteConfigProvider` + `scopeKeyBuilder` (SRDS) — only one returns non-null
- `makeHeaderValidator()` always returns `nullptr` unless compiled with `ENVOY_ENABLE_UHV` — 
  header validation falls through to codec-level checks otherwise
