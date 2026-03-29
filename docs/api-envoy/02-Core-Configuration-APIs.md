# Part 2: Envoy API — Core Configuration APIs

## Table of Contents
1. [Overview](#overview)
2. [Bootstrap — Root Configuration](#bootstrap--root-configuration)
3. [Cluster — Upstream Cluster Configuration (CDS)](#cluster--upstream-cluster-configuration-cds)
4. [Listener — Listener Configuration (LDS)](#listener--listener-configuration-lds)
5. [Route — Route Configuration (RDS)](#route--route-configuration-rds)
6. [Endpoint — Endpoint Configuration (EDS)](#endpoint--endpoint-configuration-eds)
7. [Core Types](#core-types)
8. [Configuration Flow](#configuration-flow)

## Overview

The `config/` directory (151 `.proto` files) contains Envoy's core configuration types. These are the primary resources that define Envoy's behavior: what it listens on, how it routes traffic, which upstream clusters it connects to, and how those connections are managed.

All core configuration types live under `api/envoy/config/` and follow the package pattern `envoy.config.<area>.v3`.

```
api/envoy/config/
├── bootstrap/v3/     →  Bootstrap (root config)
├── cluster/v3/       →  Cluster (CDS resource)
├── listener/v3/      →  Listener (LDS resource)
├── route/v3/         →  RouteConfiguration (RDS resource)
├── endpoint/v3/      →  ClusterLoadAssignment (EDS resource)
├── core/v3/          →  Shared core types (Address, ConfigSource, HealthCheck)
├── accesslog/v3/     →  Access log configuration
├── metrics/v3/       →  Stats configuration
├── overload/v3/      →  Overload manager
├── trace/v3/         →  Tracing configuration
└── ...
```

---

## Bootstrap — Root Configuration

**File:** `config/bootstrap/v3/bootstrap.proto`
**Package:** `envoy.config.bootstrap.v3`

The `Bootstrap` message is the root of Envoy's configuration, supplied via the `-c` CLI flag. It ties together all other configuration areas.

### Bootstrap Message

| Field | Type | Description |
|-------|------|-------------|
| `node` | `core.v3.Node` | Node identity sent to the management server. Includes cluster, id, metadata, locality. |
| `static_resources` | `StaticResources` | Statically defined listeners, clusters, and secrets. |
| `dynamic_resources` | `DynamicResources` | Configuration sources for xDS (LDS, CDS, ADS). |
| `cluster_manager` | `ClusterManager` | Cluster manager settings: local cluster name, outlier detection, upstream bind config. |
| `admin` | `Admin` | Local admin HTTP server: bind address, access logs, socket options. |
| `layered_runtime` | `LayeredRuntime` | Runtime configuration layers (static, disk, admin, RTDS). |
| `stats_flush_interval` | `Duration` | Interval between stat flushes (default 5s). |
| `watchdogs` | `Watchdogs` | Per-subsystem watchdog timers (main thread, worker threads). |
| `typed_dns_resolver_config` | `TypedExtensionConfig` | DNS resolver extension configuration. |
| `bootstrap_extensions` | `repeated TypedExtensionConfig` | Bootstrap extensions instantiated at startup. |
| `overload_manager` | `overload.v3.OverloadManager` | Overload manager for resource-based load shedding. |
| `stats_sinks` | `repeated metrics.v3.StatsSink` | Stats sinks (StatsD, OpenTelemetry, etc.). |

### StaticResources

Resources defined directly in the config file (no xDS needed):

| Field | Type | Description |
|-------|------|-------------|
| `listeners` | `repeated Listener` | Statically defined listeners. |
| `clusters` | `repeated Cluster` | Statically defined upstream clusters. |
| `secrets` | `repeated Secret` | Statically defined TLS secrets. |

### DynamicResources

xDS configuration sources for dynamic resource discovery:

| Field | Type | Description |
|-------|------|-------------|
| `lds_config` | `ConfigSource` | LDS source for listener discovery. |
| `lds_resources_locator` | `string` | `xdstp://` URI locator for listeners. |
| `cds_config` | `ConfigSource` | CDS source for cluster discovery. |
| `cds_resources_locator` | `string` | `xdstp://` URI locator for clusters. |
| `ads_config` | `ApiConfigSource` | Aggregated Discovery Service source (must be GRPC type). |

### Usage Pattern

```yaml
# Minimal Bootstrap example (YAML representation of the proto)
node:
  cluster: my-cluster
  id: my-node

static_resources:
  listeners: [...]
  clusters: [...]

dynamic_resources:
  lds_config:
    ads: {}
  cds_config:
    ads: {}
  ads_config:
    api_type: GRPC
    grpc_services:
    - envoy_grpc:
        cluster_name: xds_cluster

admin:
  address:
    socket_address: { address: 0.0.0.0, port_value: 9901 }
```

---

## Cluster — Upstream Cluster Configuration (CDS)

**File:** `config/cluster/v3/cluster.proto`
**Package:** `envoy.config.cluster.v3`

A `Cluster` defines a group of upstream hosts that Envoy connects to. Clusters are the CDS resource type.

### Cluster Message

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Unique cluster name. |
| `type` | `DiscoveryType` | How endpoints are discovered. |
| `cluster_type` | `CustomClusterType` | Custom cluster type (alternative to `type`). |
| `eds_cluster_config` | `EdsClusterConfig` | EDS source configuration (for `EDS` type clusters). |
| `connect_timeout` | `Duration` | Timeout for new upstream connections (default 5s). |
| `lb_policy` | `LbPolicy` | Load balancing algorithm. |
| `load_assignment` | `ClusterLoadAssignment` | Inline endpoints (for STATIC/STRICT_DNS/LOGICAL_DNS). |
| `health_checks` | `repeated HealthCheck` | Active health checking configuration. |
| `circuit_breakers` | `CircuitBreakers` | Circuit breaking thresholds. |
| `transport_socket` | `TransportSocket` | Upstream transport socket (e.g., TLS). |
| `load_balancing_policy` | `LoadBalancingPolicy` | Extensible LB policy (overrides `lb_policy`). |
| `metadata` | `Metadata` | Cluster metadata for filter matching. |
| `dns_lookup_family` | `DnsLookupFamily` | DNS address resolution preference. |
| `outlier_detection` | `OutlierDetection` | Passive health checking / outlier detection. |

### Discovery Types

| Enum Value | Description |
|------------|-------------|
| `STATIC` | Endpoints specified inline in `load_assignment`. |
| `STRICT_DNS` | DNS resolution, all returned addresses are cluster members. |
| `LOGICAL_DNS` | DNS resolution, only first address used per connection. |
| `EDS` | Endpoints discovered via Endpoint Discovery Service. |
| `ORIGINAL_DST` | Endpoints determined from original destination address. |

### Load Balancing Policies

| Enum Value | Description |
|------------|-------------|
| `ROUND_ROBIN` | Rotating selection across healthy hosts. |
| `LEAST_REQUEST` | Pick host with fewest active requests (power of 2 choices). |
| `RING_HASH` | Consistent hashing (requires hash policy on route). |
| `RANDOM` | Random host selection. |
| `MAGLEV` | Maglev consistent hashing. |
| `CLUSTER_PROVIDED` | LB policy provided by cluster itself. |
| `LOAD_BALANCING_POLICY_CONFIG` | Use extensible `load_balancing_policy` field. |

### EdsClusterConfig

| Field | Type | Description |
|-------|------|-------------|
| `eds_config` | `ConfigSource` | EDS update source (ADS, gRPC, REST, filesystem). |
| `service_name` | `string` | EDS service name (defaults to cluster name). |

### LbSubsetConfig

Enables routing to a subset of endpoints based on metadata:

| Field | Type | Description |
|-------|------|-------------|
| `fallback_policy` | `LbSubsetFallbackPolicy` | Behavior when no subset matches (NO_FALLBACK, ANY_ENDPOINT, DEFAULT_SUBSET). |
| `default_subset` | `Struct` | Default subset for `DEFAULT_SUBSET` fallback. |
| `subset_selectors` | `repeated LbSubsetSelector` | Metadata keys defining subsets. |

### TransportSocketMatch

Per-endpoint transport socket selection based on metadata:

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Match name (for stats). |
| `match` | `Struct` | Metadata match criteria. |
| `transport_socket` | `TransportSocket` | Transport socket to use on match. |

---

## Listener — Listener Configuration (LDS)

**File:** `config/listener/v3/listener.proto`, `listener_components.proto`
**Package:** `envoy.config.listener.v3`

A `Listener` defines a network address where Envoy accepts connections and the filter chains that process them. Listeners are the LDS resource type.

### Listener Message

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Unique listener name. |
| `address` | `core.v3.Address` | Listen address (IP:port or Unix socket). |
| `additional_addresses` | `repeated AdditionalAddress` | Extra listen addresses for this listener. |
| `filter_chains` | `repeated FilterChain` | Filter chains matched against connections. |
| `default_filter_chain` | `FilterChain` | Default when no filter chain matches. |
| `filter_chain_matcher` | `Matcher` | Extensible matcher for filter chain selection. |
| `listener_filters` | `repeated ListenerFilter` | Pre-filter-chain filters (TLS inspector, proxy protocol). |
| `use_original_dst` | `BoolValue` | Redirect to listener matching original destination. |
| `bind_to_port` | `BoolValue` | Whether to bind to port (default true). |
| `enable_reuse_port` | `BoolValue` | Enable SO_REUSEPORT per worker. |
| `access_log` | `repeated AccessLog` | Listener-level access logs. |
| `api_listener` | `ApiListener` | Non-proxy API listener (e.g., Envoy Mobile). |
| `internal_listener` | `InternalListenerConfig` | Internal listener (no L4 address, in-process). |
| `drain_type` | `DrainType` | DEFAULT or MODIFY_ONLY drain behavior. |
| `connection_balance_config` | `ConnectionBalanceConfig` | Connection balancing across workers. |

### FilterChain

An ordered pipeline of network filters applied to matched connections:

| Field | Type | Description |
|-------|------|-------------|
| `filter_chain_match` | `FilterChainMatch` | Criteria for selecting this chain. |
| `filters` | `repeated Filter` | Network filters in execution order. |
| `transport_socket` | `TransportSocket` | Downstream transport socket (e.g., TLS). |
| `name` | `string` | Unique name for the filter chain. |
| `transport_socket_connect_timeout` | `Duration` | Timeout for transport negotiation (e.g., TLS handshake). |

### FilterChainMatch

Match criteria for selecting a filter chain:

| Field | Type | Description |
|-------|------|-------------|
| `destination_port` | `UInt32Value` | Match on destination port. |
| `prefix_ranges` | `repeated CidrRange` | Match on destination IP. |
| `server_names` | `repeated string` | Match on SNI server name. |
| `transport_protocol` | `string` | Match on transport protocol (e.g., `"tls"`). |
| `application_protocols` | `repeated string` | Match on ALPN (e.g., `"h2"`, `"http/1.1"`). |
| `source_prefix_ranges` | `repeated CidrRange` | Match on source IP. |
| `source_ports` | `repeated uint32` | Match on source port. |
| `source_type` | `ConnectionSourceType` | ANY, SAME_IP_OR_LOOPBACK, EXTERNAL. |

### Filter

A single network filter in the chain:

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Filter name (e.g., `envoy.filters.network.http_connection_manager`). |
| `typed_config` | `Any` | Filter-specific configuration (protobuf Any). |
| `config_discovery` | `ExtensionConfigSource` | Dynamic filter config via ECDS. |

---

## Route — Route Configuration (RDS)

**File:** `config/route/v3/route.proto`, `route_components.proto`
**Package:** `envoy.config.route.v3`

A `RouteConfiguration` defines HTTP routing rules for the HTTP Connection Manager. It is the RDS resource type.

### RouteConfiguration Message

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Route config name (referenced by HCM's RDS). |
| `virtual_hosts` | `repeated VirtualHost` | Virtual hosts with routing rules. |
| `vhds` | `Vhds` | Virtual Host Discovery Service for on-demand virtual hosts. |
| `internal_only_headers` | `repeated string` | Headers stripped from external requests. |
| `request_headers_to_add` | `repeated HeaderValueOption` | Request headers to add. |
| `response_headers_to_add` | `repeated HeaderValueOption` | Response headers to add. |
| `validate_clusters` | `BoolValue` | Validate route clusters exist at config load. |
| `typed_per_filter_config` | `map<string, Any>` | Per-filter config overrides. |

### VirtualHost

Groups routes under a set of domains:

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Logical name (used in stats). |
| `domains` | `repeated string` | Host/authority header domains to match (supports wildcards). |
| `routes` | `repeated Route` | Ordered routes (first match wins). |
| `require_tls` | `TlsRequirementType` | NONE, EXTERNAL_ONLY, ALL. |
| `retry_policy` | `RetryPolicy` | Default retry policy for all routes. |
| `rate_limits` | `repeated RateLimit` | Rate limit actions. |
| `typed_per_filter_config` | `map<string, Any>` | Per-filter config overrides. |
| `request_headers_to_add` | `repeated HeaderValueOption` | Request headers to add. |
| `response_headers_to_add` | `repeated HeaderValueOption` | Response headers to add. |
| `hedge_policy` | `HedgePolicy` | Request hedging policy. |

### Route

A single route entry matching requests to an action:

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Route name (for logging/debugging). |
| `match` | `RouteMatch` | Match criteria (path, headers, query params). |
| `route` | `RouteAction` | Forward to upstream cluster. |
| `redirect` | `RedirectAction` | Issue an HTTP redirect. |
| `direct_response` | `DirectResponseAction` | Return a direct HTTP response. |
| `metadata` | `Metadata` | Route metadata. |
| `typed_per_filter_config` | `map<string, Any>` | Per-filter config overrides. |

### RouteAction

How matched requests are forwarded upstream:

| Field | Type | Description |
|-------|------|-------------|
| `cluster` | `string` | Target upstream cluster. |
| `cluster_header` | `string` | Cluster name from a request header. |
| `weighted_clusters` | `WeightedCluster` | Traffic splitting across multiple clusters. |
| `cluster_specifier_plugin` | `string` | Cluster specifier plugin name. |
| `prefix_rewrite` | `string` | Rewrite the path prefix. |
| `regex_rewrite` | `RegexMatchAndSubstitute` | Regex-based path rewrite. |
| `host_rewrite_literal` | `string` | Rewrite Host header. |
| `auto_host_rewrite` | `BoolValue` | Set Host from upstream hostname. |
| `timeout` | `Duration` | Route-level request timeout. |
| `retry_policy` | `RetryPolicy` | Route-level retry configuration. |
| `hash_policy` | `repeated HashPolicy` | Hash policies for consistent load balancing. |
| `request_mirror_policies` | `repeated RequestMirrorPolicy` | Shadow/mirror traffic to other clusters. |
| `metadata_match` | `Metadata` | Endpoint metadata for subset load balancing. |

---

## Endpoint — Endpoint Configuration (EDS)

**File:** `config/endpoint/v3/endpoint.proto`, `endpoint_components.proto`
**Package:** `envoy.config.endpoint.v3`

A `ClusterLoadAssignment` is the EDS resource type, providing the list of endpoints for a cluster.

### ClusterLoadAssignment Message

| Field | Type | Description |
|-------|------|-------------|
| `cluster_name` | `string` | Cluster name (matches `EdsClusterConfig.service_name` or cluster name). |
| `endpoints` | `repeated LocalityLbEndpoints` | Endpoints grouped by locality. |
| `named_endpoints` | `map<string, Endpoint>` | Named endpoints (referenced by `endpoint_name`). |
| `policy` | `Policy` | Load balancing policy settings (drops, overprovisioning). |

### Policy

| Field | Type | Description |
|-------|------|-------------|
| `drop_overloads` | `repeated DropOverload` | Drop traffic by category and percentage. |
| `overprovisioning_factor` | `UInt32Value` | Overprovisioning factor (default 140 = 140%). |
| `endpoint_stale_after` | `Duration` | Time before endpoints are considered stale. |
| `weighted_priority_health` | `bool` | Use endpoint weights for priority-level health calculation. |

### LocalityLbEndpoints

Endpoints grouped by geographic locality:

| Field | Type | Description |
|-------|------|-------------|
| `locality` | `core.v3.Locality` | Region, zone, sub-zone. |
| `lb_endpoints` | `repeated LbEndpoint` | Endpoints in this locality. |
| `load_balancing_weight` | `UInt32Value` | Relative weight of this locality. |
| `priority` | `uint32` | Priority level (0 = highest, 1+ = failover). |

### LbEndpoint

A single load-balanceable endpoint:

| Field | Type | Description |
|-------|------|-------------|
| `endpoint` | `Endpoint` | Upstream host address. |
| `endpoint_name` | `string` | Reference to a named endpoint. |
| `health_status` | `HealthStatus` | EDS-reported health (UNKNOWN, HEALTHY, UNHEALTHY, DRAINING, TIMEOUT, DEGRADED). |
| `metadata` | `Metadata` | Endpoint metadata (for subset LB, transport socket match). |
| `load_balancing_weight` | `UInt32Value` | Endpoint weight within locality (min 1). |

### Endpoint

| Field | Type | Description |
|-------|------|-------------|
| `address` | `core.v3.Address` | Upstream host address (IP:port or pipe). |
| `health_check_config` | `HealthCheckConfig` | Health check port/address override. |
| `hostname` | `string` | Hostname (used for auto_host_rewrite, TLS SNI). |

---

## Core Types

**File:** `config/core/v3/config_source.proto` and related files
**Package:** `envoy.config.core.v3`

The `core/v3/` directory contains foundational types used across all configuration areas.

### ConfigSource

Specifies where Envoy obtains configuration:

| Field | Type | Description |
|-------|------|-------------|
| `path` | `string` | Filesystem path (deprecated in favor of `path_config_source`). |
| `path_config_source` | `PathConfigSource` | Filesystem path with watch options. |
| `api_config_source` | `ApiConfigSource` | REST or gRPC API source. |
| `ads` | `AggregatedConfigSource` | Use Aggregated Discovery Service. |
| `self` | `SelfConfigSource` | Same server as the ConfigSource user. |
| `initial_fetch_timeout` | `Duration` | Timeout for initial fetch (default 15s). |
| `resource_api_version` | `ApiVersion` | xDS API version (V3). |

### ApiConfigSource

How to contact an xDS API server:

| Field | Type | Description |
|-------|------|-------------|
| `api_type` | `ApiType` | API transport type. |
| `grpc_services` | `repeated GrpcService` | gRPC services to contact. |
| `cluster_names` | `repeated string` | Clusters for REST API. |
| `refresh_delay` | `Duration` | Polling interval for REST. |
| `request_timeout` | `Duration` | Request timeout (default 1s). |
| `rate_limit_settings` | `RateLimitSettings` | Rate limiting for discovery requests. |
| `set_node_on_first_message_only` | `bool` | Only send node info on first request. |

**ApiType enum:**

| Value | Description |
|-------|-------------|
| `REST` | REST polling (deprecated). |
| `GRPC` | State-of-the-world gRPC streaming. |
| `DELTA_GRPC` | Incremental/delta gRPC streaming. |
| `AGGREGATED_GRPC` | Aggregated state-of-the-world gRPC. |
| `AGGREGATED_DELTA_GRPC` | Aggregated delta gRPC. |

### Other Key Core Types

| Type | File | Description |
|------|------|-------------|
| `Address` | `address.proto` | Socket address (IP:port) or Unix domain socket. |
| `SocketAddress` | `address.proto` | IP address and port. |
| `Node` | `base.proto` | Envoy node identity (id, cluster, metadata, locality). |
| `Metadata` | `base.proto` | Typed metadata map (filter_metadata, typed_filter_metadata). |
| `HealthCheck` | `health_check.proto` | Health check configuration (HTTP, TCP, gRPC, custom). |
| `TransportSocket` | `base.proto` | Named transport socket with typed config. |
| `GrpcService` | `grpc_service.proto` | gRPC service reference (Envoy gRPC or Google gRPC). |
| `HeaderValue` | `base.proto` | HTTP header key-value pair. |
| `RuntimeFractionalPercent` | `base.proto` | Runtime-overridable fractional percentage. |

---

## Configuration Flow

The following shows how the core configuration types relate during request processing:

```
Bootstrap
 │
 ├── StaticResources / DynamicResources (xDS)
 │    │
 │    ├── Listener (LDS)
 │    │    │
 │    │    ├── ListenerFilter[]  (TLS inspector, proxy protocol)
 │    │    │
 │    │    └── FilterChain[]  (matched by SNI, ALPN, IP, port)
 │    │         │
 │    │         ├── Filter[]  (network filters, e.g. HCM)
 │    │         │    │
 │    │         │    └── HttpConnectionManager
 │    │         │         │
 │    │         │         ├── RouteConfiguration (inline or RDS)
 │    │         │         │    │
 │    │         │         │    └── VirtualHost[]
 │    │         │         │         │
 │    │         │         │         └── Route[]
 │    │         │         │              │
 │    │         │         │              └── RouteAction → Cluster name
 │    │         │         │
 │    │         │         └── HttpFilter[]  (ext_authz, jwt, router)
 │    │         │
 │    │         └── TransportSocket  (TLS context)
 │    │
 │    └── Cluster (CDS)
 │         │
 │         ├── ClusterLoadAssignment (inline or EDS)
 │         │    │
 │         │    └── LocalityLbEndpoints[]
 │         │         │
 │         │         └── LbEndpoint[]
 │         │              │
 │         │              └── Endpoint (address, health check)
 │         │
 │         ├── HealthCheck[]  (active health checking)
 │         ├── CircuitBreakers
 │         ├── OutlierDetection  (passive health checking)
 │         ├── TransportSocket  (upstream TLS)
 │         └── LoadBalancingPolicy
 │
 ├── Admin (admin HTTP server)
 ├── LayeredRuntime (runtime feature flags)
 └── OverloadManager (resource-based load shedding)
```

**Request lifecycle through these types:**

1. A connection arrives at a **Listener** address
2. **ListenerFilters** inspect the connection (detect TLS, extract SNI)
3. **FilterChainMatch** selects the appropriate **FilterChain**
4. Network **Filters** process the connection (the terminal filter is typically `HttpConnectionManager`)
5. The HCM uses the **RouteConfiguration** to match the request to a **VirtualHost** and **Route**
6. The **RouteAction** identifies the target **Cluster**
7. The **Cluster** selects an **Endpoint** using its load balancing policy
8. The connection is established through the cluster's **TransportSocket**
