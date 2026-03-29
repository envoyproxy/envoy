# Part 4: Envoy API — Extensions API

## Table of Contents
1. [Overview](#overview)
2. [Extension Registration Model](#extension-registration-model)
3. [HTTP Filters](#http-filters)
4. [Network Filters](#network-filters)
5. [Listener Filters](#listener-filters)
6. [UDP Filters](#udp-filters)
7. [Transport Sockets](#transport-sockets)
8. [Access Loggers](#access-loggers)
9. [Load Balancing Policies](#load-balancing-policies)
10. [Clusters](#clusters)
11. [Health Checkers](#health-checkers)
12. [Tracers](#tracers)
13. [Stat Sinks](#stat-sinks)
14. [Resource Monitors](#resource-monitors)
15. [Other Extension Categories](#other-extension-categories)

## Overview

The `extensions/` directory (301 `.proto` files across 40 categories) contains configuration protos for every pluggable component in Envoy. Each extension is identified by a unique name and configured via a `google.protobuf.Any` typed config field.

All extension protos follow the package pattern `envoy.extensions.<category>.<subcategory>.v3`.

```
api/envoy/extensions/
├── filters/                    # HTTP, network, listener, UDP filters
│   ├── http/                   #   HTTP filters (50+ types)
│   ├── network/                #   Network filters (20+ types)
│   ├── listener/               #   Listener filters
│   ├── udp/                    #   UDP filters
│   └── common/                 #   Shared filter types
├── transport_sockets/          # TLS, ALTS, QUIC, raw buffer, proxy protocol
├── access_loggers/             # File, gRPC, OTel, stream, stats
├── load_balancing_policies/    # Ring hash, round robin, least request, etc.
├── clusters/                   # Aggregate, DFP, Redis, DNS
├── health_checkers/            # Redis, Thrift
├── tracers/                    # OpenTelemetry, Fluentd
├── stat_sinks/                 # OpenTelemetry, Graphite/StatsD, WASM
├── resource_monitors/          # Fixed heap, cgroup, CPU, downstream connections
├── compression/                # Brotli, gzip, zstd
├── matching/                   # Input matchers
├── rbac/                       # Principals, matchers, audit loggers
├── retry/                      # Host and priority predicates
├── upstreams/                  # HTTP and TCP upstream connection pools
└── ...                         # 25+ more categories
```

---

## Extension Registration Model

All Envoy extensions share a common registration pattern:

1. A **configuration proto** defines the extension's settings (in `extensions/`)
2. The extension is referenced by **name** in the parent configuration
3. The configuration is wrapped in `google.protobuf.Any` via the `typed_config` field

### Example: Referencing an HTTP Filter

```yaml
http_filters:
- name: envoy.filters.http.router
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
    suppress_envoy_headers: true
```

### Example: Referencing a Transport Socket

```yaml
transport_socket:
  name: envoy.transport_sockets.tls
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
    common_tls_context:
      tls_certificates: [...]
```

### Extension Config Discovery Service (ECDS)

Extensions can also be configured dynamically via ECDS using the `config_discovery` field on `Filter`:

```yaml
filters:
- name: envoy.filters.http.ext_authz
  config_discovery:
    config_source:
      ads: {}
    type_urls:
    - type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
```

---

## HTTP Filters

**Directory:** `extensions/filters/http/`
**Filter chain position:** Inside `HttpConnectionManager.http_filters`

HTTP filters process individual HTTP requests and responses within the HTTP Connection Manager.

### Key HTTP Filters

| Extension Name | Proto | Key Messages | Purpose |
|---------------|-------|-------------|---------|
| `envoy.filters.http.router` | `filters/http/router/v3/router.proto` | `Router` | **Terminal filter.** Routes requests to upstream clusters. Configures dynamic stats, upstream access logs, header suppression. |
| `envoy.filters.http.ext_authz` | `filters/http/ext_authz/v3/ext_authz.proto` | `ExtAuthz`, `HttpService`, `AuthorizationResponse` | External authorization via gRPC or HTTP service. |
| `envoy.filters.http.jwt_authn` | `filters/http/jwt_authn/v3/config.proto` | `JwtAuthentication`, `JwtProvider`, `JwtRequirement` | JWT token validation with JWKS fetching. |
| `envoy.filters.http.cors` | `filters/http/cors/v3/cors.proto` | `CorsPolicy` | Cross-Origin Resource Sharing (CORS) handling. |
| `envoy.filters.http.fault` | `filters/http/fault/v3/fault.proto` | `HTTPFault` | Fault injection (delays, aborts) for testing. |
| `envoy.filters.http.ratelimit` | `filters/http/ratelimit/v3/rate_limit.proto` | `RateLimit` | Global rate limiting via external service. |
| `envoy.filters.http.local_ratelimit` | `filters/http/local_ratelimit/v3/local_rate_limit.proto` | `LocalRateLimit` | Local (per-instance) rate limiting with token bucket. |
| `envoy.filters.http.lua` | `filters/http/lua/v3/lua.proto` | `Lua` | Lua scripting for request/response processing. |
| `envoy.filters.http.grpc_web` | `filters/http/grpc_web/v3/grpc_web.proto` | `GrpcWeb` | gRPC-Web protocol translation. |
| `envoy.filters.http.grpc_json_transcoder` | `filters/http/grpc_json_transcoder/v3/transcoder.proto` | `GrpcJsonTranscoder` | REST/JSON to gRPC transcoding. |
| `envoy.filters.http.header_to_metadata` | `filters/http/header_to_metadata/v3/header_to_metadata.proto` | `Config` | Copy header values to dynamic metadata. |
| `envoy.filters.http.health_check` | `filters/http/health_check/v3/health_check.proto` | `HealthCheck` | Respond to health check requests locally. |
| `envoy.filters.http.rbac` | `filters/http/rbac/v3/rbac.proto` | `RBAC` | Role-based access control on HTTP requests. |
| `envoy.filters.http.cache` | `filters/http/cache/v3/cache.proto` | `CacheConfig` | HTTP response caching. |
| `envoy.filters.http.compressor` | `filters/http/compressor/v3/compressor.proto` | `Compressor` | Response compression (wraps compression library extensions). |
| `envoy.filters.http.buffer` | `filters/http/buffer/v3/buffer.proto` | `Buffer` | Buffer complete request body before forwarding. |
| `envoy.filters.http.on_demand` | `filters/http/on_demand/v3/on_demand.proto` | `OnDemand` | On-demand xDS resource loading (VHDS, route). |
| `envoy.filters.http.tap` | `filters/http/tap/v3/tap.proto` | `Tap` | Tap HTTP traffic for debugging. |
| `envoy.filters.http.ext_proc` | `filters/http/ext_proc/v3/ext_proc.proto` | `ExternalProcessor` | External processing via bidirectional gRPC stream. |
| `envoy.filters.http.wasm` | `filters/http/wasm/v3/wasm.proto` | `Wasm` | WebAssembly filter execution. |
| `envoy.filters.http.adaptive_concurrency` | `filters/http/adaptive_concurrency/v3/adaptive_concurrency.proto` | `AdaptiveConcurrency` | Adaptive concurrency limiting based on latency. |
| `envoy.filters.http.set_filter_state` | `filters/common/set_filter_state/v3/set_filter_state.proto` | `Config` | Set filter state from headers or metadata. |

### Router — The Terminal HTTP Filter

The Router is the most important HTTP filter. Every HTTP filter chain must end with the router.

**Proto:** `extensions/filters/http/router/v3/router.proto`
**Message:** `Router`

| Field | Type | Description |
|-------|------|-------------|
| `dynamic_stats` | `BoolValue` | Enable per-route stats (default true). |
| `start_child_span` | `bool` | Create child span for upstream request. |
| `upstream_log` | `repeated AccessLog` | Access logs for upstream requests. |
| `upstream_log_options` | `UpstreamAccessLogOptions` | Flush upstream logs on stream end or upstream connection close. |
| `suppress_envoy_headers` | `bool` | Suppress `x-envoy-*` headers. |
| `upstream_http_filters` | `repeated HttpFilter` | HTTP filters for upstream requests. |
| `respect_expected_rq_timeout` | `bool` | Use `x-envoy-expected-rq-timeout-ms` for upstream timeout. |

---

## Network Filters

**Directory:** `extensions/filters/network/`
**Filter chain position:** Inside `FilterChain.filters` (on listener)

Network filters process raw TCP connections and data.

### Key Network Filters

| Extension Name | Proto | Key Messages | Purpose |
|---------------|-------|-------------|---------|
| `envoy.filters.network.http_connection_manager` | `filters/network/http_connection_manager/v3/http_connection_manager.proto` | `HttpConnectionManager` | **The bridge between L4 and L7.** Parses HTTP, manages HTTP filter chain, routing. |
| `envoy.filters.network.tcp_proxy` | `filters/network/tcp_proxy/v3/tcp_proxy.proto` | `TcpProxy` | TCP proxy to upstream cluster with tunneling support. |
| `envoy.filters.network.ext_authz` | `filters/network/ext_authz/v3/ext_authz.proto` | `ExtAuthz` | Network-level external authorization. |
| `envoy.filters.network.redis_proxy` | `filters/network/redis_proxy/v3/redis_proxy.proto` | `RedisProxy` | Redis protocol proxy with command splitting. |
| `envoy.filters.network.mongo_proxy` | `filters/network/mongo_proxy/v3/mongo_proxy.proto` | `MongoProxy` | MongoDB wire protocol proxy. |
| `envoy.filters.network.thrift_proxy` | `filters/network/thrift_proxy/v3/thrift_proxy.proto` | `ThriftProxy` | Apache Thrift protocol proxy. |
| `envoy.filters.network.kafka_broker` | `filters/network/kafka_broker/v3/kafka_broker.proto` | `KafkaBroker` | Kafka broker protocol proxy. |
| `envoy.filters.network.rate_limit` | `filters/network/ratelimit/v3/rate_limit.proto` | `RateLimit` | Network-level rate limiting. |
| `envoy.filters.network.local_ratelimit` | `filters/network/local_ratelimit/v3/local_rate_limit.proto` | `LocalRateLimit` | Per-instance network rate limiting. |
| `envoy.filters.network.rbac` | `filters/network/rbac/v3/rbac.proto` | `RBAC` | Network-level RBAC. |
| `envoy.filters.network.wasm` | `filters/network/wasm/v3/wasm.proto` | `Wasm` | WebAssembly network filter. |
| `envoy.filters.network.direct_response` | `filters/network/direct_response/v3/config.proto` | `Config` | Return fixed response data. |
| `envoy.filters.network.connection_limit` | `filters/network/connection_limit/v3/connection_limit.proto` | `ConnectionLimit` | Per-listener connection limiting. |

### HttpConnectionManager — The Central Network Filter

The HCM is the most complex network filter. It bridges L4 (TCP) and L7 (HTTP).

**Proto:** `extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto`
**Message:** `HttpConnectionManager`

| Field | Type | Description |
|-------|------|-------------|
| `codec_type` | `CodecType` | HTTP codec: AUTO, HTTP1, HTTP2, HTTP3. |
| `stat_prefix` | `string` | Stats prefix for this HCM. |
| `rds` | `Rds` | RDS config source for route configuration. |
| `route_config` | `RouteConfiguration` | Inline route configuration. |
| `scoped_routes` | `ScopedRoutes` | Scoped route configuration. |
| `http_filters` | `repeated HttpFilter` | HTTP filter chain (must end with router). |
| `tracing` | `HttpTracing` | Tracing configuration. |
| `access_log` | `repeated AccessLog` | Access logs. |
| `server_name` | `string` | Server name (default "envoy"). |
| `stream_idle_timeout` | `Duration` | Stream idle timeout. |
| `request_timeout` | `Duration` | Total request timeout. |
| `request_headers_timeout` | `Duration` | Timeout for request headers. |
| `use_remote_address` | `BoolValue` | Trust downstream remote address. |
| `upgrade_configs` | `repeated UpgradeConfig` | Protocol upgrades (WebSocket, CONNECT). |
| `http_protocol_options` | `Http1ProtocolOptions` | HTTP/1.1 options. |
| `http2_protocol_options` | `Http2ProtocolOptions` | HTTP/2 options. |

### TcpProxy

**Proto:** `extensions/filters/network/tcp_proxy/v3/tcp_proxy.proto`
**Message:** `TcpProxy`

| Field | Type | Description |
|-------|------|-------------|
| `stat_prefix` | `string` | Stats prefix. |
| `cluster` | `string` | Target upstream cluster. |
| `weighted_clusters` | `WeightedCluster` | Traffic split across clusters. |
| `idle_timeout` | `Duration` | Connection idle timeout (default 1h). |
| `tunneling_config` | `TunnelingConfig` | HTTP CONNECT tunneling. |
| `access_log` | `repeated AccessLog` | Access logs. |
| `max_downstream_connection_duration` | `Duration` | Max connection lifetime. |

---

## Listener Filters

**Directory:** `extensions/filters/listener/`
**Filter chain position:** In `Listener.listener_filters`, run before filter chain selection.

| Extension Name | Proto | Purpose |
|---------------|-------|---------|
| `envoy.filters.listener.tls_inspector` | `filters/listener/tls_inspector/v3/tls_inspector.proto` | Detect TLS, extract SNI and ALPN for filter chain matching. Supports JA3/JA4 fingerprinting. |
| `envoy.filters.listener.http_inspector` | `filters/listener/http_inspector/v3/http_inspector.proto` | Detect HTTP protocol version. |
| `envoy.filters.listener.proxy_protocol` | `filters/listener/proxy_protocol/v3/proxy_protocol.proto` | Parse HAProxy PROXY protocol header. |
| `envoy.filters.listener.original_dst` | `filters/listener/original_dst/v3/original_dst.proto` | Extract original destination from SO_ORIGINAL_DST. |
| `envoy.filters.listener.original_src` | `filters/listener/original_src/v3/original_src.proto` | Replicate downstream source address on upstream. |

---

## UDP Filters

**Directory:** `extensions/filters/udp/`

| Extension Name | Proto | Purpose |
|---------------|-------|---------|
| `envoy.filters.udp_listener.dns_filter` | `filters/udp/dns_filter/v3/dns_filter.proto` | DNS request handling and forwarding. |
| `envoy.filters.udp_listener.udp_proxy` | `filters/udp/udp_proxy/v3/udp_proxy.proto` | UDP proxy to upstream cluster. |

---

## Transport Sockets

**Directory:** `extensions/transport_sockets/`
**Used in:** `FilterChain.transport_socket` (downstream) and `Cluster.transport_socket` (upstream)

Transport sockets handle the connection-level security and framing.

| Extension Name | Proto | Key Messages | Purpose |
|---------------|-------|-------------|---------|
| `envoy.transport_sockets.tls` | `transport_sockets/tls/v3/tls.proto` | `UpstreamTlsContext`, `DownstreamTlsContext`, `CommonTlsContext` | TLS/mTLS: certificates, validation, SNI, OCSP, session tickets. |
| `envoy.transport_sockets.raw_buffer` | `transport_sockets/raw_buffer/v3/raw_buffer.proto` | `RawBuffer` | No security, raw TCP. |
| `envoy.transport_sockets.alts` | `transport_sockets/alts/v3/alts.proto` | `AltsSocket` | Google ALTS (Application Layer Transport Security). |
| `envoy.transport_sockets.tap` | `transport_sockets/tap/v3/tap.proto` | `Tap` | Wrap another transport socket with tap/trace. |
| `envoy.transport_sockets.proxy_protocol` | `transport_sockets/proxy_protocol/v3/upstream_proxy_protocol.proto` | `ProxyProtocolUpstreamTransport` | Add PROXY protocol header on upstream connections. |
| `envoy.transport_sockets.quic` | `transport_sockets/quic/v3/quic_transport.proto` | `QuicDownstreamTransport`, `QuicUpstreamTransport` | QUIC (HTTP/3) transport. |
| `envoy.transport_sockets.starttls` | `transport_sockets/starttls/v3/starttls.proto` | `StartTlsConfig`, `UpstreamStartTlsConfig` | STARTTLS upgrade from plaintext to TLS. |
| `envoy.transport_sockets.internal_upstream` | `transport_sockets/internal_upstream/v3/internal_upstream.proto` | `InternalUpstreamTransport` | Transport for internal listeners. |

### TLS Configuration (Key Types)

**CommonTlsContext** — shared between upstream and downstream:

| Field | Type | Description |
|-------|------|-------------|
| `tls_certificates` | `repeated TlsCertificate` | Certificate chain + private key. |
| `tls_certificate_sds_secret_configs` | `repeated SdsSecretConfig` | SDS-sourced certificates. |
| `validation_context` | `CertificateValidationContext` | CA certs for peer validation. |
| `tls_params` | `TlsParameters` | TLS version, cipher suites. |
| `alpn_protocols` | `repeated string` | ALPN protocols. |
| `custom_handshaker` | `TypedExtensionConfig` | Custom TLS handshaker. |

---

## Access Loggers

**Directory:** `extensions/access_loggers/`
**Used in:** `Listener.access_log`, `HttpConnectionManager.access_log`, `Router.upstream_log`

| Extension Name | Proto | Key Messages | Purpose |
|---------------|-------|-------------|---------|
| `envoy.access_loggers.file` | `access_loggers/file/v3/file.proto` | `FileAccessLog` | Log to file (text format, JSON, typed JSON). |
| `envoy.access_loggers.http_grpc` | `access_loggers/grpc/v3/als.proto` | `HttpGrpcAccessLogConfig` | Stream HTTP access logs via gRPC. |
| `envoy.access_loggers.tcp_grpc` | `access_loggers/grpc/v3/als.proto` | `TcpGrpcAccessLogConfig` | Stream TCP access logs via gRPC. |
| `envoy.access_loggers.open_telemetry` | `access_loggers/open_telemetry/v3/logs_service.proto` | `OpenTelemetryAccessLogConfig` | OpenTelemetry access logging. |
| `envoy.access_loggers.stream` | `access_loggers/stream/v3/stream.proto` | `StdoutAccessLog`, `StderrAccessLog` | Log to stdout/stderr. |
| `envoy.access_loggers.wasm` | `access_loggers/wasm/v3/wasm.proto` | `WasmAccessLog` | WASM access logging. |
| `envoy.access_loggers.fluentd` | `access_loggers/fluentd/v3/fluentd.proto` | `FluentdAccessLogConfig` | Fluentd access logging. |

---

## Load Balancing Policies

**Directory:** `extensions/load_balancing_policies/`
**Used in:** `Cluster.load_balancing_policy`

Extensible load balancing policies that replace the legacy `Cluster.lb_policy` enum.

| Extension Name | Proto | Key Messages | Purpose |
|---------------|-------|-------------|---------|
| `envoy.load_balancing_policies.round_robin` | `load_balancing_policies/round_robin/v3/round_robin.proto` | `RoundRobin` | Round-robin selection with slow start support. |
| `envoy.load_balancing_policies.least_request` | `load_balancing_policies/least_request/v3/least_request.proto` | `LeastRequest` | Least active requests (power of N choices). |
| `envoy.load_balancing_policies.ring_hash` | `load_balancing_policies/ring_hash/v3/ring_hash.proto` | `RingHash` | Consistent hashing (XX_HASH, MURMUR_HASH_2). |
| `envoy.load_balancing_policies.maglev` | `load_balancing_policies/maglev/v3/maglev.proto` | `Maglev` | Maglev consistent hashing. |
| `envoy.load_balancing_policies.random` | `load_balancing_policies/random/v3/random.proto` | `Random` | Random host selection. |
| `envoy.load_balancing_policies.subset` | `load_balancing_policies/subset/v3/subset.proto` | `Subset` | Subset-based LB using endpoint metadata. |
| `envoy.load_balancing_policies.wrr_locality` | `load_balancing_policies/wrr_locality/v3/wrr_locality.proto` | `WrrLocality` | Weighted round-robin across localities. |
| `envoy.load_balancing_policies.pick_first` | `load_balancing_policies/pick_first/v3/pick_first.proto` | `PickFirst` | Use first available host (for singleton clusters). |
| `envoy.load_balancing_policies.client_side_weighted_round_robin` | `load_balancing_policies/client_side_weighted_round_robin/v3/...` | `ClientSideWeightedRoundRobin` | Client-side WRR using backend-reported load. |

---

## Clusters

**Directory:** `extensions/clusters/`

Custom cluster discovery types:

| Extension Name | Proto | Key Messages | Purpose |
|---------------|-------|-------------|---------|
| `envoy.clusters.aggregate` | `clusters/aggregate/v3/cluster.proto` | `ClusterConfig` | Aggregate across multiple clusters with priority failover. |
| `envoy.clusters.dynamic_forward_proxy` | `clusters/dynamic_forward_proxy/v3/cluster.proto` | `ClusterConfig` | Dynamic forward proxy with DNS caching. |
| `envoy.clusters.redis` | `clusters/redis/v3/redis_cluster.proto` | `RedisClusterConfig` | Redis cluster topology discovery. |

---

## Health Checkers

**Directory:** `extensions/health_checkers/`

Protocol-specific health check implementations:

| Extension Name | Proto | Purpose |
|---------------|-------|---------|
| `envoy.health_checkers.redis` | `health_checkers/redis/v3/redis.proto` | Redis PING or EXISTS health check (with optional AWS IAM auth). |
| `envoy.health_checkers.thrift` | `health_checkers/thrift/v3/thrift.proto` | Thrift health check. |

---

## Tracers

**Directory:** `extensions/tracers/`

| Extension Name | Proto | Purpose |
|---------------|-------|---------|
| `envoy.tracers.opentelemetry` | `tracers/opentelemetry/v3/opentelemetry.proto` | OpenTelemetry distributed tracing via OTLP. |
| `envoy.tracers.fluentd` | `tracers/fluentd/v3/fluentd.proto` | Fluentd trace export. |

---

## Stat Sinks

**Directory:** `extensions/stat_sinks/`

| Extension Name | Proto | Key Messages | Purpose |
|---------------|-------|-------------|---------|
| `envoy.stat_sinks.open_telemetry` | `stat_sinks/open_telemetry/v3/open_telemetry.proto` | `SinkConfig` | OpenTelemetry metric export via OTLP. |
| `envoy.stat_sinks.graphite_statsd` | `stat_sinks/graphite_statsd/v3/graphite_statsd.proto` | `GraphiteStatsdSink` | Graphite/StatsD metric export. |
| `envoy.stat_sinks.wasm` | `stat_sinks/wasm/v3/wasm.proto` | `Wasm` | WASM stat sink. |

---

## Resource Monitors

**Directory:** `extensions/resource_monitors/`
**Used in:** `OverloadManager.resource_monitors`

Monitors system resources for overload protection:

| Extension Name | Proto | Purpose |
|---------------|-------|---------|
| `envoy.resource_monitors.fixed_heap` | `resource_monitors/fixed_heap/v3/fixed_heap.proto` | Monitor heap memory against a configured max. |
| `envoy.resource_monitors.cgroup_memory` | `resource_monitors/cgroup_memory/v3/cgroup_memory.proto` | Monitor cgroup memory limits. |
| `envoy.resource_monitors.cpu_utilization` | `resource_monitors/cpu_utilization/v3/cpu_utilization.proto` | Monitor CPU utilization. |
| `envoy.resource_monitors.downstream_connections` | `resource_monitors/downstream_connections/v3/downstream_connections.proto` | Monitor active downstream connections. |
| `envoy.resource_monitors.global_downstream_max_connections` | `resource_monitors/global_downstream_max_connections/v3/...` | Global downstream connection limit. |

---

## Other Extension Categories

### Compression Libraries
**Directory:** `extensions/compression/`

| Category | Extensions |
|----------|-----------|
| Brotli | `brotli/compressor`, `brotli/decompressor` |
| Gzip | `gzip/compressor`, `gzip/decompressor` |
| Zstd | `zstd/compressor`, `zstd/decompressor` |

### Matching
**Directory:** `extensions/matching/`

Input matchers and common inputs for the xDS matching API:

- `matching/common_inputs/` — Environment, network, SSL inputs
- `matching/input_matchers/` — CEL, IP, consistent hashing, runtime fraction matchers

### RBAC Extensions
**Directory:** `extensions/rbac/`

- `rbac/matchers/` — Custom RBAC matchers (upstream IP port)
- `rbac/principals/` — Custom principals (mTLS authenticated)
- `rbac/audit_loggers/` — Audit log implementations (stdout)

### Retry Predicates
**Directory:** `extensions/retry/`

- `retry/host/` — Host selection predicates (omit canary hosts, omit by metadata, previous hosts)
- `retry/priority/` — Priority selection predicates (previous priorities)

### Upstream Connection Pools
**Directory:** `extensions/upstreams/`

- `upstreams/http/v3/` — HTTP upstream options (keepalive, auto config)
- `upstreams/tcp/v3/` — TCP upstream options

### Path Handling
**Directory:** `extensions/path/`

- `path/match/` — URI template matching
- `path/rewrite/` — URI template rewriting

### QUIC Extensions
**Directory:** `extensions/quic/`

- Connection ID generators
- Proof source configuration
- Server preferred address handling
- Crypto stream configuration

### WASM
**Directory:** `extensions/wasm/v3/`

Base WASM runtime configuration shared by HTTP filter, network filter, and access logger WASM extensions.

### Dynamic Modules
**Directory:** `extensions/dynamic_modules/v3/`

Configuration for dynamically loaded native modules (shared libraries).

### Internal Redirect
**Directory:** `extensions/internal_redirect/`

Predicates for controlling internal redirect behavior:
- `allow_listed_routes` — Only follow redirects to specific routes
- `previous_routes` — Avoid redirect loops
- `safe_cross_scheme` — Allow HTTP→HTTPS redirects

### Formatter Extensions
**Directory:** `extensions/formatter/`

Custom access log format extensions:
- `formatter/cel/` — CEL expression formatter
- `formatter/metadata/` — Metadata formatter
- `formatter/req_without_query/` — Request path without query string

### GeoIP Providers
**Directory:** `extensions/geoip_providers/`

- `geoip_providers/maxmind/` — MaxMind GeoIP database lookup

### Key-Value Store
**Directory:** `extensions/key_value/`

- `key_value/file_based/` — File-based key-value store for xDS caching
