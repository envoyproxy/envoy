# Part 5: Envoy API — Common Types, Data, Admin, and Annotations

## Table of Contents
1. [Overview](#overview)
2. [Common Types (type/)](#common-types-type)
3. [Data Types (data/)](#data-types-data)
4. [Admin Interface (admin/)](#admin-interface-admin)
5. [Watchdog (watchdog/)](#watchdog-watchdog)
6. [Annotations (annotations/)](#annotations-annotations)
7. [Additional Config Areas](#additional-config-areas)

## Overview

This document covers the supporting API areas that provide shared types, event data structures, admin introspection, watchdog actions, and proto annotations. These types are used throughout the core configuration and extension APIs.

```
api/envoy/
├── type/           42 .proto   Shared types (matchers, percent, metadata, HTTP)
├── data/           17 .proto   Event data (access logs, outlier detection, health checks, tap)
├── admin/          20 .proto   Admin interface response types
├── watchdog/        1 .proto   Watchdog action configuration
└── annotations/     2 .proto   Proto field/service annotations
```

---

## Common Types (type/)

**Directory:** `api/envoy/type/`
**Packages:** `envoy.type.v3`, `envoy.type.matcher.v3`, `envoy.type.metadata.v3`, `envoy.type.http.v3`, `envoy.type.tracing.v3`

The `type/` directory contains reusable types shared across all Envoy API areas. These are building blocks referenced by configuration, extensions, and data types.

### Core Value Types (`type/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `percent.proto` | `Percent`, `FractionalPercent` | Percentage values. `FractionalPercent` uses a numerator and denominator (HUNDRED, TEN_THOUSAND, MILLION) for precision. |
| `range.proto` | `Int64Range`, `Int32Range`, `DoubleRange` | Numeric ranges (inclusive start, exclusive end). |
| `semantic_version.proto` | `SemanticVersion` | Major, minor, patch version. |
| `token_bucket.proto` | `TokenBucket` | Token bucket rate limiter: max_tokens, tokens_per_fill, fill_interval. |
| `hash_policy.proto` | `HashPolicy` | Hash policy for consistent load balancing. |
| `http_status.proto` | `HttpStatus` | HTTP status code wrapper. |
| `http.proto` | `CodecClientType` | HTTP codec type enum: HTTP10, HTTP11, HTTP2, HTTP3. |

### FractionalPercent

Used extensively throughout Envoy for runtime-overridable probabilities:

| Field | Type | Description |
|-------|------|-------------|
| `numerator` | `uint32` | Numerator value. |
| `denominator` | `DenominatorType` | HUNDRED (default), TEN_THOUSAND, or MILLION. |

Example: `{numerator: 50, denominator: HUNDRED}` = 50%. `{numerator: 100, denominator: TEN_THOUSAND}` = 1%.

### Matchers (`type/matcher/v3/`)

Matchers provide reusable matching predicates used in routing, RBAC, access log filters, and more.

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `string.proto` | `StringMatcher`, `ListStringMatcher` | Match strings by exact value, prefix, suffix, contains, regex, or custom extension. |
| `regex.proto` | `RegexMatcher`, `RegexMatchAndSubstitute` | Regular expression matching with capture group substitution. |
| `value.proto` | `ValueMatcher`, `NullMatch` | Match protobuf Value types (null, double, string, bool, list). |
| `number.proto` | `DoubleMatcher` | Match double values by range or exact match. |
| `path.proto` | `PathMatcher` | Match URL path (wraps StringMatcher). |
| `metadata.proto` | `MetadataMatcher` | Match against dynamic metadata values. |
| `struct.proto` | `StructMatcher` | Match against Struct message fields by path. |
| `filter_state.proto` | `FilterStateMatcher` | Match against filter state objects. |
| `node.proto` | `NodeMatcher` | Match Envoy Node identity (id, cluster). |
| `http_inputs.proto` | `HttpRequestHeaderMatchInput`, `HttpResponseHeaderMatchInput`, etc. | HTTP-specific match inputs. |
| `status_code_input.proto` | `HttpResponseStatusCodeMatchInput`, `HttpResponseStatusCodeClassMatchInput` | Match on HTTP response status codes. |

#### StringMatcher

The most commonly used matcher:

| Field | Type | Description |
|-------|------|-------------|
| `exact` | `string` | Exact string match. |
| `prefix` | `string` | Prefix match. |
| `suffix` | `string` | Suffix match. |
| `safe_regex` | `RegexMatcher` | RE2 regex match. |
| `contains` | `string` | Substring match. |
| `custom` | `TypedExtensionConfig` | Custom string matcher extension. |
| `ignore_case` | `bool` | Case-insensitive matching (exact, prefix, suffix, contains). |

### Metadata (`type/metadata/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `metadata.proto` | `MetadataKey`, `MetadataKind` | Metadata lookup by filter name and path. MetadataKind specifies the source: Request, Route, Cluster, Host. |

#### MetadataKey

Identifies a metadata value:

| Field | Type | Description |
|-------|------|-------------|
| `key` | `string` | Filter namespace key (e.g., `envoy.lb`, `envoy.filters.http.ext_authz`). |
| `path` | `repeated PathSegment` | Nested path within the filter metadata. |

#### MetadataKind

Specifies where metadata comes from:

| Variant | Description |
|---------|-------------|
| `Request` | Dynamic metadata from the current request. |
| `Route` | Metadata from the matched route entry. |
| `Cluster` | Metadata from the upstream cluster. |
| `Host` | Metadata from the selected upstream host/endpoint. |

### HTTP Types (`type/http/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `cookie.proto` | `Cookie` | HTTP cookie: name, path, TTL. |
| `path_transformation.proto` | `PathTransformation` | URL path normalization and transformation operations. |

### Tracing Types (`type/tracing/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `custom_tag.proto` | `CustomTag` | Custom trace span tags sourced from: literal, environment variable, request header, or metadata. |

### Top-Level Type Files

These unversioned files are in `type/` directly:

| File | Message | Description |
|------|---------|-------------|
| `hash_policy.proto` | `HashPolicy` | Source-IP based hash policy. |
| `http.proto` | `CodecClientType` | HTTP1, HTTP2, HTTP3 codec types. |
| `http_status.proto` | `HttpStatus`, `StatusCode` | HTTP status code with full enum of all standard codes. |
| `percent.proto` | `Percent`, `FractionalPercent` | Same as v3 versions. |
| `range.proto` | `Int64Range`, `Int32Range`, `DoubleRange` | Same as v3 versions. |
| `semantic_version.proto` | `SemanticVersion` | Same as v3 version. |
| `token_bucket.proto` | `TokenBucket` | Same as v3 version. |

---

## Data Types (data/)

**Directory:** `api/envoy/data/`
**Packages:** `envoy.data.<area>.v3`

The `data/` directory contains event and data structures emitted by Envoy during operation. These are the "output" types — what Envoy produces as telemetry, not what it consumes as configuration.

### Access Log Data (`data/accesslog/v3/`)

Structured access log entries sent via gRPC ALS (Access Log Service):

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `accesslog.proto` | `TCPAccessLogEntry`, `HTTPAccessLogEntry`, `AccessLogCommon`, `ConnectionProperties` | Complete access log entries for TCP and HTTP. |

#### AccessLogCommon

Shared fields across all access log entry types:

| Field | Type | Description |
|-------|------|-------------|
| `downstream_remote_address` | `Address` | Client address. |
| `downstream_local_address` | `Address` | Local address the request arrived on. |
| `upstream_remote_address` | `Address` | Upstream host address. |
| `upstream_cluster` | `string` | Upstream cluster name. |
| `response_flags` | `ResponseFlags` | Response flags (timeout, reset, rate limit, etc.). |
| `metadata` | `Metadata` | Dynamic metadata. |
| `tls_properties` | `TLSProperties` | TLS connection properties (version, cipher, SNI, peer cert). |
| `start_time` | `Timestamp` | Request start time. |
| `time_to_last_rx_byte` | `Duration` | Time to receive complete request. |
| `time_to_first_upstream_tx_byte` | `Duration` | Time to first byte sent upstream. |
| `time_to_last_upstream_tx_byte` | `Duration` | Time to last byte sent upstream. |
| `time_to_first_upstream_rx_byte` | `Duration` | Time to first byte received from upstream. |
| `time_to_last_upstream_rx_byte` | `Duration` | Time to last byte received from upstream. |
| `time_to_first_downstream_tx_byte` | `Duration` | Time to first byte sent downstream. |
| `time_to_last_downstream_tx_byte` | `Duration` | Time to last byte sent downstream. |

#### HTTPRequestProperties / HTTPResponseProperties

HTTP-specific fields:

| Field (Request) | Description |
|-----------------|-------------|
| `request_method` | HTTP method (GET, POST, etc.). |
| `scheme` | Request scheme (http, https). |
| `authority` | Host/authority header. |
| `path` | Request path. |
| `user_agent` | User-Agent header. |
| `referer` | Referer header. |
| `forwarded_for` | X-Forwarded-For header. |
| `request_id` | x-request-id. |
| `request_headers_bytes` | Request header size. |
| `request_body_bytes` | Request body size. |
| `request_headers` | All request headers. |

| Field (Response) | Description |
|------------------|-------------|
| `response_code` | HTTP status code. |
| `response_headers_bytes` | Response header size. |
| `response_body_bytes` | Response body size. |
| `response_headers` | All response headers. |
| `response_trailers` | All response trailers. |
| `response_code_details` | Detailed reason for the response code. |

#### ResponseFlags

Bit flags indicating what happened during request processing:

| Flag | Description |
|------|-------------|
| `failed_local_healthcheck` | Local origin health check failed. |
| `no_healthy_upstream` | No healthy upstream. |
| `upstream_request_timeout` | Upstream request timeout. |
| `local_reset` | Connection locally reset. |
| `upstream_remote_reset` | Upstream remote reset. |
| `upstream_connection_failure` | Upstream connection failure. |
| `upstream_connection_termination` | Upstream connection terminated. |
| `upstream_overflow` | Upstream circuit breaker overflow. |
| `no_route_found` | No route found. |
| `delay_injected` | Fault delay injected. |
| `fault_injected` | Fault abort injected. |
| `rate_limited` | Rate limited. |
| `downstream_connection_termination` | Downstream connection terminated. |
| `upstream_retry_limit_exceeded` | Retry limit exceeded. |
| `no_cluster_found` | Cluster not found. |

### Cluster Data (`data/cluster/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `outlier_detection_event.proto` | `OutlierDetectionEvent`, `OutlierEjectionType`, `Action` | Outlier detection events (ejection/unejection). |

**OutlierEjectionType:** CONSECUTIVE_5XX, CONSECUTIVE_GATEWAY_FAILURE, SUCCESS_RATE, CONSECUTIVE_LOCAL_ORIGIN_FAILURE, SUCCESS_RATE_LOCAL_ORIGIN, FAILURE_PERCENTAGE, FAILURE_PERCENTAGE_LOCAL_ORIGIN

**Action:** EJECT, UNEJECT

### Core Data (`data/core/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `health_check_event.proto` | `HealthCheckEvent`, `HealthCheckFailure`, `HealthCheckerType` | Health check success/failure events. |
| `tlv_metadata.proto` | `TlvMetadata` | TLV (Type-Length-Value) metadata from PROXY protocol. |

### DNS Data (`data/dns/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `dns_table.proto` | `DnsTable`, `DnsVirtualDomain`, `DnsEndpoint` | DNS filter table configuration: virtual domains with address/service records. |

### Tap Data (`data/tap/v3/`)

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `wrapper.proto` | `TraceWrapper` | Tap trace wrapper: HTTP buffered/streamed trace or socket buffered/streamed trace. |
| `http.proto` | `HttpBufferedTrace`, `HttpStreamedTraceSegment` | HTTP request/response trace data. |
| `transport.proto` | `SocketBufferedTrace`, `SocketStreamedTraceSegment` | Raw socket data traces. |
| `common.proto` | `Body`, `Connection` | Shared tap types (body data, connection info). |

---

## Admin Interface (admin/)

**Directory:** `api/envoy/admin/v3/`
**Package:** `envoy.admin.v3`

The `admin/` directory contains types returned by Envoy's admin HTTP interface. These are response types only — they are not used for configuration input.

### Config Dump

**File:** `config_dump.proto` and related `*_config_dump.proto`

The `/config_dump` admin endpoint returns the complete configuration state:

| Proto File | Key Messages | Description |
|-----------|-------------|-------------|
| `config_dump.proto` | `ConfigDump` | Top-level config dump containing all config types. |
| `config_dump_shared.proto` | `UpdateFailureState`, `ListenersConfigDump.StaticListener`, etc. | Shared dump types with timestamps and version info. |

**ConfigDump** wraps all resources as `repeated Any` configs, which can include:

| Dump Type | Message | Description |
|-----------|---------|-------------|
| Bootstrap | `BootstrapConfigDump` | Original bootstrap configuration. |
| Listeners | `ListenersConfigDump` | Static and dynamic listeners with state (WARMING, ACTIVE, DRAINING). |
| Clusters | `ClustersConfigDump` | Static and dynamic clusters with state. |
| Routes | `RoutesConfigDump` | Static and dynamic route configurations. |
| Secrets | `SecretsConfigDump` | Static and dynamic secrets (redacted). |
| ECDS | `EcdsConfigDump` | Extension config discovery state. |
| Endpoints | `EndpointsConfigDump` | Static and dynamic endpoint configs. |

Each dynamic resource includes:
- `version_info` — xDS version
- `last_updated` — Timestamp of last update
- `error_state` — Last update failure details (if any)
- `client_status` — UNKNOWN, REQUESTED, DOES_NOT_EXIST, ACKED, NACKED

### Cluster Status

**File:** `clusters.proto`

The `/clusters` admin endpoint:

| Message | Description |
|---------|-------------|
| `Clusters` | Collection of all cluster statuses. |
| `ClusterStatus` | Single cluster: name, success/error/timeout rates, circuit breaker stats. |
| `HostStatus` | Per-host: address, health status, weight, locality, priority, stats. |
| `HostHealthStatus` | Detailed health: failed_active_health_check, failed_outlier_check, eds_health_status, failed_active_degraded_check. |

### Server Info

**File:** `server_info.proto`

The `/server_info` admin endpoint:

| Message | Description |
|---------|-------------|
| `ServerInfo` | Server version, state (LIVE, DRAINING, PRE_INITIALIZING, INITIALIZING), uptime, hot restart version, command line options. |
| `CommandLineOptions` | All CLI flags: config path, log level, concurrency, admin address, service cluster/node, drain time, etc. |

### Other Admin Types

| File | Messages | Endpoint |
|------|----------|----------|
| `listeners.proto` | `Listeners`, `ListenerStatus` | `/listeners` |
| `memory.proto` | `Memory` | `/memory` — allocated, heap size, page heap stats |
| `metrics.proto` | `SimpleMetric` (COUNTER, GAUGE) | Various stats endpoints |
| `mutex_stats.proto` | `MutexStats` | `/contention` — mutex contention stats |
| `tap.proto` | `TapRequest` | `/tap` — admin tap interface |
| `certs.proto` | `Certificates`, `Certificate` | `/certs` — TLS certificate details |
| `init_dump.proto` | `UnreadyTargetsDumps` | `/init_dump` — uninitialized targets |

---

## Watchdog (watchdog/)

**Directory:** `api/envoy/watchdog/v3/`
**Package:** `envoy.watchdog.v3`

### AbortActionConfig

**File:** `abort_action.proto`

A watchdog action that terminates the process when a thread is stuck:

| Field | Type | Description |
|-------|------|-------------|
| `wait_duration` | `Duration` | Wait time before killing the stuck thread (default 0s, recommended 5s). Allows time for other watchdog actions (e.g., profiling) to complete. |

This is used in the `Bootstrap.Watchdogs` configuration to define what happens when a thread exceeds the watchdog timeout. When triggered, Envoy sends SIGABRT to produce a core dump for debugging.

---

## Annotations (annotations/)

**Directory:** `api/envoy/annotations/`

Proto annotations provide metadata about API fields and services.

### Resource Annotation

**File:** `resource.proto`

Extends `google.protobuf.ServiceOptions` to declare the xDS resource type for a service:

| Field | Type | Description |
|-------|------|-------------|
| `type` | `string` | xDS resource type (e.g., `envoy.config.cluster.v3.Cluster`). |

Used on xDS service definitions to bind them to their resource type.

### Deprecation Annotation

**File:** `deprecation.proto`

Extends `google.protobuf.FieldOptions` and `google.protobuf.EnumValueOptions`:

| Field | Type | Description |
|-------|------|-------------|
| `disallowed_by_default` | `bool` | Field is deprecated and disabled by default. Runtime flag required to use it. |
| `deprecated_at_minor_version` | `string` | Minor version when the field was deprecated. |

Fields annotated with `disallowed_by_default = true` will cause a configuration error unless explicitly enabled via runtime flags, enforcing deprecation timelines.

---

## Additional Config Areas

These areas under `config/` are not core xDS resources but provide important supporting configuration.

### Access Log Configuration (`config/accesslog/v3/`)

| Message | Description |
|---------|-------------|
| `AccessLog` | Access logger reference (name + typed_config pointing to an access logger extension). |
| `AccessLogFilter` | Filter for conditional logging: status code, duration, not health check, traceable, runtime, header, response flag, gRPC status, metadata, log type, CEL expression. |
| `ComparisonFilter` | Comparison operator (EQ, GE) with runtime value. |
| `StatusCodeFilter` | Filter by HTTP status code. |
| `DurationFilter` | Filter by request duration. |
| `HeaderFilter` | Filter by header presence/value. |
| `ResponseFlagFilter` | Filter by response flags. |

### Metrics Configuration (`config/metrics/v3/`)

| Message | Description |
|---------|-------------|
| `StatsConfig` | Global stats configuration: tag specifiers, stats matcher, histogram bucket boundaries. |
| `StatsMatcher` | Control which stats are instantiated (inclusion/exclusion lists). |
| `TagSpecifier` | Extract tags from stat names via regex. |
| `StatsSink` | Stats sink reference (name + typed_config pointing to a stat sink extension). |

### Overload Manager (`config/overload/v3/`)

| Message | Description |
|---------|-------------|
| `OverloadManager` | Resource monitors and overload actions. |
| `ResourceMonitor` | Resource monitor reference (name + typed_config). |
| `OverloadAction` | Action triggered at resource thresholds (e.g., stop accepting connections, disable HTTP keepalive). |
| `ScaleTimersOverloadActionConfig` | Scale request timeouts based on resource pressure. |
| `BufferFactoryConfig` | Buffer allocation limits under overload. |

### Tracing Configuration (`config/trace/v3/`)

| Message | Description |
|---------|-------------|
| `Tracing` | Top-level tracing config (from Bootstrap, deprecated in favor of HCM-level tracing). |
| `Tracing.Http` | HTTP tracer reference (name + typed_config pointing to a tracer extension). |

### RBAC Configuration (`config/rbac/v3/`)

| Message | Description |
|---------|-------------|
| `RBAC` | Role-based access control: action (ALLOW, DENY, LOG) and policies map. |
| `Policy` | Permissions (what) + principals (who) + condition (CEL). |
| `Permission` | Match: any, header, URL path, metadata, destination IP/port, requested server name, or combinations (and/or/not). |
| `Principal` | Match: any, authenticated (principal name), source IP, direct remote IP, remote IP, header, metadata, or combinations. |

### Rate Limit Configuration (`config/ratelimit/v3/`)

| Message | Description |
|---------|-------------|
| `RateLimitServiceConfig` | Rate limit service reference: gRPC service, transport API version. |

### Tap Configuration (`config/tap/v3/`)

| Message | Description |
|---------|-------------|
| `TapConfig` | Tap/trace configuration: match config and output config. |
| `MatchPredicate` | What to tap: or/and/not combinations of HTTP request/response header match, generic body match. |
| `OutputConfig` | Where to send traces: streaming or buffered, to file or gRPC admin endpoint. |

### Common Configuration (`config/common/`)

| Area | Description |
|------|-------------|
| `dynamic_forward_proxy/` | DNS cache config shared between DFP cluster and HTTP filter. |
| `key_value/` | Key-value store config for xDS caching. |
| `matcher/` | Matcher action helpers for the xDS matching API. |
| `mutation_rules/` | Header mutation rules (allowed/disallowed headers). |
| `tap/` | Common tap configuration. |

### Upstream Configuration (`config/upstream/`)

| Area | Description |
|------|-------------|
| `local_address_selector/v3/` | Configures how Envoy selects a local address for upstream connections. |
