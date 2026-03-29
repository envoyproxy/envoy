# Envoy Router — Architecture Reference

> Documentation of the `source/common/router/` folder: routing, retries, shadowing, and upstream request handling.

---

## 1. Router Block Diagram

```mermaid
flowchart TB
    subgraph HTTP["HTTP Filter Layer"]
        Filter["Filter (Router::Filter)"]
        FilterConfig["FilterConfig"]
    end

    subgraph Route["Route Resolution"]
        ConfigImpl["ConfigImpl"]
        RouteEntry["RouteEntry"]
        VirtualHost["VirtualHost"]
        RdsImpl["RdsRouteConfigSubscription"]
    end

    subgraph Upstream["Upstream Request"]
        UpstreamRequest["UpstreamRequest"]
        RetryState["RetryStateImpl"]
        ConnPool["GenericConnPool"]
    end

    subgraph Support["Supporting"]
        ShadowWriter["ShadowWriter"]
        ConfigUtility["ConfigUtility"]
        HeaderParser["HeaderParser"]
    end

    Filter --> FilterConfig
    Filter --> UpstreamRequest
    Filter --> RetryState
    FilterConfig --> ConfigImpl
    ConfigImpl --> RouteEntry
    ConfigImpl --> RdsImpl
    RouteEntry --> VirtualHost
    UpstreamRequest --> ConnPool
    Filter --> ShadowWriter
```

---

## 2. Router Class Diagram (UML)

```mermaid
classDiagram
    class Filter {
        -config_: FilterConfigSharedPtr
        -route_: RouteConstSharedPtr
        -route_entry_: RouteEntry*
        -cluster_: ClusterInfoConstSharedPtr
        -upstream_requests_: list~UpstreamRequestPtr~
        -retry_state_: RetryStatePtr
        -callbacks_: StreamDecoderFilterCallbacks*
        +decodeHeaders(headers, end_stream) FilterHeadersStatus
        +decodeData(data, end_stream) FilterDataStatus
        +decodeTrailers(trailers) FilterTrailersStatus
        +onUpstreamHeaders(code, headers, upstream_request, end_stream) void
        +onUpstreamData(data, upstream_request, end_stream) void
        +onUpstreamReset(reason, transport_failure, upstream_request) void
        +onUpstreamComplete(upstream_request) void
    }

    class FilterConfig {
        -cm_: ClusterManager&
        -runtime_: Runtime::Loader&
        -shadow_writer_: ShadowWriterPtr
        -scope_: Stats::Scope&
        +createFilterChain(callbacks) bool
        +shadowWriter() ShadowWriter&
    }

    class UpstreamRequest {
        -parent_: RouterFilterInterface&
        -conn_pool_: GenericConnPool
        +acceptHeadersFromRouter(end_stream) void
        +acceptDataFromRouter(data, end_stream) void
        +acceptTrailersFromRouter(trailers) void
        +onPoolReady(upstream, host, ...) void
        +onPoolFailure(reason, transport_failure, host) void
        +decodeHeaders(headers, end_stream) void
        +decodeData(data, end_stream) void
    }

    class RetryStateImpl {
        -retry_on_: uint32_t
        -retries_remaining_: uint32_t
        +shouldRetryHeaders(headers, request, callback) RetryStatus
        +shouldRetryReset(reason, http3_used, callback, started) RetryStatus
        +shouldSelectAnotherHost(host) bool
        +onHostAttempted(host) void
    }

    class RouteEntry {
        <<interface>>
        +clusterName() string
        +virtualHost() VirtualHost
        +retryPolicy() RetryPolicy
        +timeout() optional~Duration~
        +hashPolicy() HashPolicy
        +metadataMatchCriteria() MetadataMatchCriteria
    }

    class ConfigImpl {
        -route_config_: RouteConfig
        -route_matcher_: RouteMatcher
        +route(headers, stream_info) RouteConstSharedPtr
    }

    Filter --> FilterConfig : uses
    Filter --> UpstreamRequest : owns
    Filter --> RetryStateImpl : owns
    Filter --> RouteEntry : route_entry_
    UpstreamRequest --> GenericConnPool : uses
    FilterConfig --> ConfigImpl : config
    ConfigImpl --> RouteEntry : resolves to
```

---

## 3. Router Request Flow Sequence Diagram

```mermaid
sequenceDiagram
    participant ConnManager as ConnectionManager
    participant Filter as Router::Filter
    participant Config as FilterConfig
    participant Route as RouteEntry
    participant ClusterMgr as ClusterManager
    participant UpstreamReq as UpstreamRequest
    participant ConnPool as ConnPool
    participant Upstream as Upstream Host

    ConnManager->>Filter: decodeHeaders(headers)
    Filter->>Config: get route config
    Filter->>Route: matches(headers)
    Route-->>Filter: route
    Filter->>ClusterMgr: getThreadLocalCluster(cluster_name)
    ClusterMgr-->>Filter: cluster
    Filter->>UpstreamReq: createUpstreamRequest()
    Filter->>UpstreamReq: acceptHeadersFromRouter()
    UpstreamReq->>ConnPool: newStream()
    ConnPool->>Upstream: createConnection()
    Upstream-->>UpstreamReq: onPoolReady()
    UpstreamReq->>Upstream: encodeHeaders()
    Upstream-->>UpstreamReq: decodeHeaders(response)
    UpstreamReq->>Filter: onUpstreamHeaders()
    Filter->>Filter: maybe retry?
    Filter->>ConnManager: encodeHeaders(downstream)
```

---

## 4. Retry Flow Sequence Diagram

```mermaid
sequenceDiagram
    participant Filter as Router::Filter
    participant UpstreamReq as UpstreamRequest
    participant RetryState as RetryStateImpl
    participant ConnPool as ConnPool

    UpstreamReq->>Filter: onUpstreamReset(reason)
    Filter->>RetryState: shouldRetryReset(reason, callback)
    RetryState->>RetryState: check retry_on policy
    RetryState->>RetryState: check retries_remaining_
    alt Retry allowed
        RetryState-->>Filter: RetryStatus::Yes
        Filter->>Filter: doRetry()
        Filter->>ConnPool: newStream()
        Filter->>UpstreamReq: createUpstreamRequest()
    else No retry
        RetryState-->>Filter: RetryStatus::No
        Filter->>Filter: onUpstreamAbort()
    end
```

---

## 5. Key Classes Reference

### 5.1 Router::Filter

**Location:** `source/common/router/router.h`

**Purpose:** Main HTTP router filter. Routes requests to upstream clusters, manages retries, shadowing, timeouts, and load balancing context.

| Method | Return | Description |
|--------|--------|-------------|
| `decodeHeaders()` | FilterHeadersStatus | Route lookup, cluster selection, start upstream request |
| `decodeData()` | FilterDataStatus | Forward body to upstream |
| `decodeTrailers()` | FilterTrailersStatus | Forward trailers |
| `onUpstreamHeaders()` | void | Handle upstream response headers |
| `onUpstreamData()` | void | Forward response body downstream |
| `onUpstreamReset()` | void | Handle upstream reset, trigger retry if applicable |
| `onUpstreamComplete()` | void | Finalize successful response |
| `onPerTryTimeout()` | void | Per-try timeout, may retry |
| `onGlobalTimeout()` | void | Global timeout, abort |

| Member | Type | Description |
|--------|------|-------------|
| `config_` | FilterConfigSharedPtr | Router config |
| `route_` | RouteConstSharedPtr | Matched route |
| `route_entry_` | RouteEntry* | Route entry (cluster, timeout, retry) |
| `cluster_` | ClusterInfoConstSharedPtr | Target cluster |
| `upstream_requests_` | list\<UpstreamRequestPtr\> | In-flight upstream requests |
| `retry_state_` | RetryStatePtr | Retry policy and state |
| `callbacks_` | StreamDecoderFilterCallbacks* | Downstream filter callbacks |

---

### 5.2 FilterConfig

**Location:** `source/common/router/router.h`

**Purpose:** Router filter configuration. Holds ClusterManager, Runtime, ShadowWriter, stats, and upstream filter factories.

| Member | Type | Description |
|--------|------|-------------|
| `cm_` | ClusterManager& | Upstream cluster manager |
| `runtime_` | Runtime::Loader& | Runtime flags |
| `shadow_writer_` | ShadowWriterPtr | Shadow request writer |
| `scope_` | Stats::Scope& | Stats scope |
| `default_stats_` | FilterStats | Router stats |
| `upstream_http_filter_factories_` | FilterFactoriesList | Upstream filter chain |

| Method | Description |
|--------|-------------|
| `createFilterChain()` | Create upstream filter chain |
| `shadowWriter()` | Access shadow writer |

---

### 5.3 UpstreamRequest

**Location:** `source/common/router/upstream_request.h`

**Purpose:** Represents a single upstream request. Owns connection pool stream, buffers data, forwards request/response, handles flow control.

| Method | Return | Description |
|--------|--------|-------------|
| `acceptHeadersFromRouter()` | void | Receive headers from router, send upstream |
| `acceptDataFromRouter()` | void | Forward body |
| `acceptTrailersFromRouter()` | void | Forward trailers |
| `onPoolReady()` | void | Connection ready, encode request |
| `onPoolFailure()` | void | Pool failure, notify router |
| `decodeHeaders()` | void | Receive response headers from upstream |
| `decodeData()` | void | Receive response body |
| `onResetStream()` | void | Upstream reset |
| `setupPerTryTimeout()` | void | Start per-try timeout timer |

| Member | Type | Description |
|--------|------|-------------|
| `parent_` | RouterFilterInterface& | Router filter |
| `conn_pool_` | GenericConnPool | Connection pool |
| `stream_options_` | StreamOptions | Upstream stream options |

---

### 5.4 RetryStateImpl

**Location:** `source/common/router/retry_state_impl.h`

**Purpose:** Retry policy and state. Decides whether to retry on reset/timeout/headers, host selection, and priority load for retries.

| Method | Return | Description |
|--------|--------|-------------|
| `shouldRetryHeaders()` | RetryStatus | Retry based on response headers |
| `shouldRetryReset()` | RetryStatus | Retry on stream reset |
| `shouldHedgeRetryPerTryTimeout()` | RetryStatus | Hedge on per-try timeout |
| `shouldSelectAnotherHost()` | bool | Exclude host on retry |
| `onHostAttempted()` | void | Notify retry predicates |
| `priorityLoadForRetry()` | HealthyAndDegradedLoad | Priority load for retries |

---

### 5.5 RouteEntry (Interface)

**Location:** `envoy/router/router.h`

**Purpose:** Route entry interface. Provides cluster name, virtual host, retry policy, timeout, hash policy, metadata match.

| Method | Return | Description |
|--------|--------|-------------|
| `clusterName()` | const std::string& | Target cluster |
| `virtualHost()` | const VirtualHost& | Virtual host |
| `retryPolicy()` | const RetryPolicy* | Retry policy |
| `timeout()` | absl::optional\<std::chrono::milliseconds\> | Route timeout |
| `hashPolicy()` | const HashPolicy* | Load balancer hash |
| `metadataMatchCriteria()` | const MetadataMatchCriteria* | Subset LB metadata |

---

### 5.6 ConfigImpl

**Location:** `source/common/router/config_impl.h`

**Purpose:** Route configuration implementation. Holds route config, virtual hosts, and route matcher.

| Method | Return | Description |
|--------|--------|-------------|
| `route()` | RouteConstSharedPtr | Match headers to route |

---

### 5.7 RdsRouteConfigSubscription

**Location:** `source/common/router/rds_impl.h`

**Purpose:** Fetches route config via RDS API and updates config providers.

| Method | Return | Description |
|--------|--------|-------------|
| `routeConfigUpdate()` | RouteConfigUpdatePtr& | Config update receiver |
| `updateOnDemand()` | void | Trigger on-demand update |

---

### 5.8 ShadowWriter

**Location:** `envoy/router/shadow_writer.h`

**Purpose:** Sends shadow (mirror) requests to alternate clusters. Used for traffic mirroring.

| Method | Return | Description |
|--------|--------|-------------|
| `submit()` | OngoingRequest* | Submit shadow request |

---

### 5.9 FilterUtility

**Location:** `source/common/router/router.h`

**Purpose:** Stateless router utilities: header validation, scheme setup, shadow decision, timeout parsing.

| Method | Description |
|--------|-------------|
| `setUpstreamScheme()` | Set :scheme from TLS/headers |
| `shouldShadow()` | Decide if request should be shadowed |
| `finalTimeout()` | Compute global and per-try timeouts |
| `checkHeader()` | Strict header validation |

---

## 6. Router Folder Structure

```
source/common/router/
├── router.h / router.cc           # Filter, FilterConfig, FilterUtility
├── upstream_request.h / .cc      # UpstreamRequest
├── retry_state_impl.h / .cc      # RetryStateImpl
├── config_impl.h / .cc           # ConfigImpl, RouteEntryImplBase, VirtualHost
├── rds_impl.h / .cc              # RdsRouteConfigSubscription
├── shadow_writer_impl.h / .cc    # ShadowWriterImpl
├── config_utility.h / .cc         # ConfigUtility
├── header_parser.h / .cc          # HeaderParser
├── router_ratelimit.h / .cc      # Rate limit integration
├── vhds.h / .cc                  # VHDS (Virtual Host Discovery)
├── scoped_rds.h / .cc            # Scoped RDS
└── ...
```

---

## 7. Router Component Dependency

```mermaid
graph TD
    Filter[Filter]
    FilterConfig[FilterConfig]
    UpstreamRequest[UpstreamRequest]
    RetryState[RetryStateImpl]
    RouteEntry[RouteEntry]
    ConfigImpl[ConfigImpl]
    ClusterManager[ClusterManager]
    ShadowWriter[ShadowWriter]
    ConnPool[GenericConnPool]

    Filter --> FilterConfig
    Filter --> UpstreamRequest
    Filter --> RetryState
    Filter --> RouteEntry
    Filter --> ClusterManager
    Filter --> ShadowWriter
    FilterConfig --> ConfigImpl
    UpstreamRequest --> ConnPool
    UpstreamRequest --> Filter
```

---

## 8. Related Paths

| Component | Path |
|-----------|------|
| Router filter | `source/common/router/` |
| Router interfaces | `envoy/router/` |
| HTTP connection manager | `source/common/http/conn_manager_impl.*` |
| Upstream clusters | `source/common/upstream/` |
| Connection pools | `source/common/http/conn_pool.*` |
