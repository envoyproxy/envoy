# Part 3: Envoy API — xDS Discovery Services

## Table of Contents
1. [Overview](#overview)
2. [Discovery Protocol Fundamentals](#discovery-protocol-fundamentals)
3. [Base Discovery Types](#base-discovery-types)
4. [Core xDS Services](#core-xds-services)
5. [Aggregated Discovery Service (ADS)](#aggregated-discovery-service-ads)
6. [Auxiliary Services](#auxiliary-services)
7. [xDS Resource Ordering and Dependencies](#xds-resource-ordering-and-dependencies)
8. [Streaming Modes Summary](#streaming-modes-summary)

## Overview

The `service/` directory (40 `.proto` files) defines the gRPC service interfaces that a control plane implements to dynamically configure Envoy. These are collectively called the **xDS APIs** (x Discovery Service).

All service definitions live under `api/envoy/service/` and follow the package pattern `envoy.service.<area>.v3`.

```
api/envoy/service/
├── discovery/v3/     →  Base types: DiscoveryRequest/Response, ADS
├── listener/v3/      →  LDS (Listener Discovery Service)
├── cluster/v3/       →  CDS (Cluster Discovery Service)
├── route/v3/         →  RDS (Route Discovery Service) + VHDS
├── endpoint/v3/      →  EDS (Endpoint Discovery Service)
├── secret/v3/        →  SDS (Secret Discovery Service)
├── runtime/v3/       →  RTDS (Runtime Discovery Service)
├── health/v3/        →  HDS (Health Discovery Service)
├── status/v3/        →  CSDS (Client Status Discovery Service)
├── auth/v3/          →  External Authorization
├── ext_proc/v3/      →  External Processing
├── ratelimit/v3/     →  Rate Limit Service
├── accesslog/v3/     →  Access Log Service (ALS)
├── metrics/v3/       →  Metrics Service
├── tap/v3/           →  Tap Sink Service
├── load_stats/v3/    →  Load Reporting Service
└── event_reporting/v3/ → Event Reporting
```

---

## Discovery Protocol Fundamentals

Envoy's xDS protocol operates in two modes:

### State-of-the-World (SotW)

The control plane sends the **complete set** of resources on every update. Envoy sends a `DiscoveryRequest`, the control plane responds with a `DiscoveryResponse` containing all resources of that type.

- Simple to implement
- Higher bandwidth usage (sends everything on every change)
- Used via `Stream*` RPCs or `Fetch*` RPCs

### Delta (Incremental)

The control plane sends only **changed or removed** resources. Envoy subscribes/unsubscribes to specific resources. The control plane responds with individual resource additions and a list of removed resources.

- More efficient for large resource sets
- More complex to implement
- Used via `Delta*` RPCs

### Protocol Flow (SotW)

```
Envoy                              Control Plane
  │                                      │
  │──── DiscoveryRequest ───────────────▶│  (version_info="", resource_names=[...])
  │                                      │
  │◀─── DiscoveryResponse ──────────────│  (version_info="v1", resources=[...], nonce="a")
  │                                      │
  │──── DiscoveryRequest ───────────────▶│  (version_info="v1", response_nonce="a")  ← ACK
  │                                      │
  │◀─── DiscoveryResponse ──────────────│  (version_info="v2", resources=[...], nonce="b")
  │                                      │
  │──── DiscoveryRequest ───────────────▶│  (version_info="v1", response_nonce="b",
  │                                      │   error_detail=...)                       ← NACK
```

### Protocol Flow (Delta)

```
Envoy                              Control Plane
  │                                      │
  │── DeltaDiscoveryRequest ────────────▶│  (resource_names_subscribe=["x","y"])
  │                                      │
  │◀── DeltaDiscoveryResponse ──────────│  (resources=[{name:"x",...},{name:"y",...}],
  │                                      │   nonce="a")
  │                                      │
  │── DeltaDiscoveryRequest ────────────▶│  (response_nonce="a")                     ← ACK
  │                                      │
  │◀── DeltaDiscoveryResponse ──────────│  (resources=[{name:"x",...}],
  │                                      │   removed_resources=["y"], nonce="b")
  │                                      │
  │── DeltaDiscoveryRequest ────────────▶│  (resource_names_unsubscribe=["x"])
```

---

## Base Discovery Types

**File:** `service/discovery/v3/discovery.proto`
**Package:** `envoy.service.discovery.v3`

These messages are used by all xDS services.

### DiscoveryRequest (SotW)

Sent by Envoy to request or ACK/NACK resources:

| Field | Type | Description |
|-------|------|-------------|
| `version_info` | `string` | Last accepted version (empty on first request). ACK = same version, NACK = previous version. |
| `node` | `Node` | Envoy node identity. |
| `resource_names` | `repeated string` | Resources to subscribe to (empty = wildcard). |
| `resource_locators` | `repeated ResourceLocator` | `xdstp://` resource locators (alternative to names). |
| `type_url` | `string` | Resource type URL (e.g., `type.googleapis.com/envoy.config.cluster.v3.Cluster`). |
| `response_nonce` | `string` | Nonce from the most recent response being ACK/NACKed. |
| `error_detail` | `google.rpc.Status` | Error detail when NACKing a response. |

### DiscoveryResponse (SotW)

Sent by the control plane with resources:

| Field | Type | Description |
|-------|------|-------------|
| `version_info` | `string` | Version of this resource set. |
| `resources` | `repeated Any` | Serialized resources (e.g., Cluster, Listener). |
| `type_url` | `string` | Resource type URL. |
| `nonce` | `string` | Unique response identifier for ACK/NACK. |
| `control_plane` | `ControlPlane` | Control plane identifier. |
| `resource_errors` | `repeated ResourceError` | Per-resource errors. |
| `canary` | `bool` | Whether this is a canary response. |

### DeltaDiscoveryRequest (Delta)

Sent by Envoy for incremental resource management:

| Field | Type | Description |
|-------|------|-------------|
| `node` | `Node` | Envoy node identity. |
| `type_url` | `string` | Resource type URL. |
| `resource_names_subscribe` | `repeated string` | New resources to subscribe to. |
| `resource_names_unsubscribe` | `repeated string` | Resources to unsubscribe from. |
| `resource_locators_subscribe` | `repeated ResourceLocator` | `xdstp://` locators to subscribe. |
| `resource_locators_unsubscribe` | `repeated ResourceLocator` | `xdstp://` locators to unsubscribe. |
| `initial_resource_versions` | `map<string, string>` | Known resource versions on reconnect. |
| `response_nonce` | `string` | Nonce from response being ACK/NACKed. |
| `error_detail` | `google.rpc.Status` | Error on NACK. |

### DeltaDiscoveryResponse (Delta)

Sent by the control plane with incremental updates:

| Field | Type | Description |
|-------|------|-------------|
| `system_version_info` | `string` | System-level version (informational). |
| `resources` | `repeated Resource` | Added or updated resources. |
| `type_url` | `string` | Resource type URL. |
| `removed_resources` | `repeated string` | Names of removed resources. |
| `removed_resource_names` | `repeated ResourceName` | Structured names of removed resources. |
| `nonce` | `string` | Response identifier for ACK/NACK. |
| `control_plane` | `ControlPlane` | Control plane identifier. |

### Resource (Delta)

A single resource in a delta response:

| Field | Type | Description |
|-------|------|-------------|
| `name` | `string` | Resource name. |
| `resource_name` | `ResourceName` | Structured resource name. |
| `aliases` | `repeated string` | Resource name aliases. |
| `version` | `string` | Resource version. |
| `resource` | `Any` | Serialized resource. |
| `ttl` | `Duration` | Resource time-to-live. |
| `cache_control` | `CacheControl` | Caching directives. |

---

## Core xDS Services

Each core xDS service follows the same pattern: three RPCs (Stream, Delta, Fetch) using the base discovery types.

### Listener Discovery Service (LDS)

**File:** `service/listener/v3/lds.proto`
**Resource type:** `envoy.config.listener.v3.Listener`

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `StreamListeners` | Bidirectional streaming | `stream DiscoveryRequest` | `stream DiscoveryResponse` |
| `DeltaListeners` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |
| `FetchListeners` | Unary | `DiscoveryRequest` | `DiscoveryResponse` |

Discovers listener configurations. Each listener defines an address, filter chains, and listener filters.

### Cluster Discovery Service (CDS)

**File:** `service/cluster/v3/cds.proto`
**Resource type:** `envoy.config.cluster.v3.Cluster`

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `StreamClusters` | Bidirectional streaming | `stream DiscoveryRequest` | `stream DiscoveryResponse` |
| `DeltaClusters` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |
| `FetchClusters` | Unary | `DiscoveryRequest` | `DiscoveryResponse` |

Discovers upstream cluster configurations. Each cluster defines discovery type, LB policy, health checks, and circuit breakers.

### Route Discovery Service (RDS)

**File:** `service/route/v3/rds.proto`
**Resource type:** `envoy.config.route.v3.RouteConfiguration`

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `StreamRoutes` | Bidirectional streaming | `stream DiscoveryRequest` | `stream DiscoveryResponse` |
| `DeltaRoutes` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |
| `FetchRoutes` | Unary | `DiscoveryRequest` | `DiscoveryResponse` |

Discovers HTTP route configurations referenced by the HTTP Connection Manager.

### Virtual Host Discovery Service (VHDS)

**File:** `service/route/v3/rds.proto` (same file as RDS)
**Resource type:** `envoy.config.route.v3.VirtualHost`

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `DeltaVirtualHosts` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |

On-demand virtual host discovery. Only supports delta mode. Virtual hosts are discovered lazily when a request arrives for an unknown domain.

### Endpoint Discovery Service (EDS)

**File:** `service/endpoint/v3/eds.proto`
**Resource type:** `envoy.config.endpoint.v3.ClusterLoadAssignment`

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `StreamEndpoints` | Bidirectional streaming | `stream DiscoveryRequest` | `stream DiscoveryResponse` |
| `DeltaEndpoints` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |
| `FetchEndpoints` | Unary | `DiscoveryRequest` | `DiscoveryResponse` |

Discovers endpoint assignments for clusters. Each `ClusterLoadAssignment` contains endpoints grouped by locality and priority.

### Secret Discovery Service (SDS)

**File:** `service/secret/v3/sds.proto`
**Resource type:** TLS certificates and keys

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `StreamSecrets` | Bidirectional streaming | `stream DiscoveryRequest` | `stream DiscoveryResponse` |
| `DeltaSecrets` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |
| `FetchSecrets` | Unary | `DiscoveryRequest` | `DiscoveryResponse` |

Discovers TLS certificates, private keys, and validation contexts. Enables certificate rotation without restarts.

### Runtime Discovery Service (RTDS)

**File:** `service/runtime/v3/rtds.proto`
**Resource type:** `envoy.service.runtime.v3.Runtime`

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `StreamRuntime` | Bidirectional streaming | `stream DiscoveryRequest` | `stream DiscoveryResponse` |
| `DeltaRuntime` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |
| `FetchRuntime` | Unary | `DiscoveryRequest` | `DiscoveryResponse` |

Discovers runtime feature flag layers. The `Runtime` resource contains a name and a `Struct` layer of key-value pairs.

---

## Aggregated Discovery Service (ADS)

**File:** `service/discovery/v3/ads.proto`
**Package:** `envoy.service.discovery.v3`

ADS multiplexes all xDS resource types over a **single gRPC stream**, providing ordering guarantees across resource types.

| RPC | Mode | Request | Response |
|-----|------|---------|----------|
| `StreamAggregatedResources` | Bidirectional streaming | `stream DiscoveryRequest` | `stream DiscoveryResponse` |
| `DeltaAggregatedResources` | Bidirectional streaming | `stream DeltaDiscoveryRequest` | `stream DeltaDiscoveryResponse` |

### Why ADS?

Without ADS, each resource type uses a separate gRPC stream. This can cause ordering issues:
- CDS delivers a new cluster, but EDS hasn't delivered its endpoints yet
- RDS references a cluster that CDS hasn't delivered yet

ADS solves this by serializing all updates on one stream, allowing the control plane to ensure correct ordering.

### ADS Configuration

```yaml
dynamic_resources:
  lds_config:
    ads: {}
    resource_api_version: V3
  cds_config:
    ads: {}
    resource_api_version: V3
  ads_config:
    api_type: GRPC
    transport_api_version: V3
    grpc_services:
    - envoy_grpc:
        cluster_name: xds_cluster
```

---

## Auxiliary Services

These services are not xDS discovery services but provide auxiliary functionality.

### External Authorization Service

**File:** `service/auth/v3/external_auth.proto`
**Package:** `envoy.service.auth.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `Check` | Unary | Authorize a request/connection |

**CheckRequest:** Contains `AttributeContext` with request attributes (source, destination, headers, etc.).
**CheckResponse:** Returns status, optional denied/ok HTTP response modifications, and dynamic metadata.

### External Processing Service

**File:** `service/ext_proc/v3/external_processor.proto`
**Package:** `envoy.service.ext_proc.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `Process` | Bidirectional streaming | Process request/response headers, body, trailers |

**ProcessingRequest (oneof):** `request_headers`, `response_headers`, `request_body`, `response_body`, `request_trailers`, `response_trailers`
**ProcessingResponse (oneof):** `request_headers`, `response_headers`, `request_body`, `response_body`, `request_trailers`, `response_trailers`, `immediate_response`

Enables an external service to inspect and modify HTTP requests/responses as they flow through Envoy, with streaming support for body processing.

### Rate Limit Service

**File:** `service/ratelimit/v3/rls.proto`
**Package:** `envoy.service.ratelimit.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `ShouldRateLimit` | Unary | Check if a request should be rate limited |

**RateLimitRequest:** Contains `domain` and `descriptors` (key-value pairs identifying the rate limit).
**RateLimitResponse:** Returns `overall_code` (OK, OVER_LIMIT), per-descriptor statuses, and optional response headers.

### Access Log Service (ALS)

**File:** `service/accesslog/v3/als.proto`
**Package:** `envoy.service.accesslog.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `StreamAccessLogs` | Client-streaming | Stream access log entries to sink |

**StreamAccessLogsMessage:** Contains an `Identifier` (node, log name) and either `HTTPAccessLogEntries` or `TCPAccessLogEntries`.

### Health Discovery Service (HDS)

**File:** `service/health/v3/hds.proto`
**Package:** `envoy.service.health.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `StreamHealthCheck` | Bidirectional streaming | Delegated health checking |
| `FetchHealthCheck` | Unary | One-shot health check assignment |

The management server assigns health check responsibilities to Envoy instances. Envoy performs the checks and reports results back.

### Client Status Discovery Service (CSDS)

**File:** `service/status/v3/csds.proto`
**Package:** `envoy.service.status.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `StreamClientStatus` | Bidirectional streaming | Query xDS client config status |
| `FetchClientStatus` | Unary | One-shot client status query |

**ClientStatusRequest:** Contains `node_matchers` to filter which clients to query.
**ClientStatusResponse:** Returns `ClientConfig` with the current xDS configuration state per client, including config status (SYNCED, NOT_SENT, STALE, ERROR).

### Metrics Service

**File:** `service/metrics/v3/metrics_service.proto`
**Package:** `envoy.service.metrics.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `StreamMetrics` | Client-streaming | Stream metrics to sink |

### Tap Sink Service

**File:** `service/tap/v3/tapds.proto`
**Package:** `envoy.service.tap.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `StreamTaps` | Client-streaming | Stream tap traces to sink |

### Load Reporting Service

**File:** `service/load_stats/v3/lrs.proto`
**Package:** `envoy.service.load_stats.v3`

| RPC | Mode | Description |
|-----|------|-------------|
| `StreamLoadStats` | Bidirectional streaming | Report upstream load statistics |

---

## xDS Resource Ordering and Dependencies

Resources have dependencies that must be respected during configuration updates:

```
LDS (Listeners)
 │
 ├── References RDS route_config_name
 │    │
 │    └── RDS (RouteConfigurations)
 │         │
 │         ├── References cluster names in RouteAction
 │         │
 │         └── VHDS (VirtualHosts) — on-demand
 │
 └── References ECDS for dynamic filter configs

CDS (Clusters)
 │
 ├── References EDS service_name
 │    │
 │    └── EDS (ClusterLoadAssignments)
 │
 └── References SDS for TLS certificates
      │
      └── SDS (Secrets)

RTDS (Runtime) — independent
```

**Ordering guarantees with ADS:**
1. CDS updates are delivered before EDS (so endpoints reference existing clusters)
2. CDS updates are delivered before RDS (so routes reference existing clusters)
3. If a cluster is removed, it is removed from RDS/EDS first, then from CDS

**Without ADS**, each resource type has its own stream and no cross-type ordering is guaranteed. Envoy handles temporary inconsistencies gracefully (e.g., routes referencing unknown clusters return 503).

---

## Streaming Modes Summary

| Service | SotW Stream | Delta Stream | Unary Fetch |
|---------|:-----------:|:------------:|:-----------:|
| **LDS** | StreamListeners | DeltaListeners | FetchListeners |
| **CDS** | StreamClusters | DeltaClusters | FetchClusters |
| **RDS** | StreamRoutes | DeltaRoutes | FetchRoutes |
| **VHDS** | — | DeltaVirtualHosts | — |
| **EDS** | StreamEndpoints | DeltaEndpoints | FetchEndpoints |
| **SDS** | StreamSecrets | DeltaSecrets | FetchSecrets |
| **RTDS** | StreamRuntime | DeltaRuntime | FetchRuntime |
| **ADS** | StreamAggregatedResources | DeltaAggregatedResources | — |
| **HDS** | StreamHealthCheck | — | FetchHealthCheck |
| **CSDS** | StreamClientStatus | — | FetchClientStatus |
| **Auth** | — | — | Check |
| **ExtProc** | Process (bidi) | — | — |
| **RLS** | — | — | ShouldRateLimit |
| **ALS** | StreamAccessLogs (client) | — | — |
| **Metrics** | StreamMetrics (client) | — | — |
| **Tap** | StreamTaps (client) | — | — |
| **LRS** | StreamLoadStats (bidi) | — | — |
