# Part 10: Router Filter and Upstream Request Flow

## Overview

The Router filter is the **terminal decoder filter** in the HTTP filter chain. It is responsible for resolving routes, selecting upstream clusters and hosts, creating connection pools, and forwarding the request upstream. It is the bridge between the downstream HTTP filter chain and the upstream network.

## Router's Role in the Architecture

```mermaid
graph LR
    subgraph "Downstream"
        HF["HTTP Filters<br/>(decode path)"]
    end
    
    subgraph "Router Filter"
        RR["Route Resolution"]
        CS["Cluster Selection"]
        HS["Host Selection<br/>(Load Balancer)"]
        CP["Connection Pool"]
        UR["UpstreamRequest"]
    end
    
    subgraph "Upstream"
        UC["Upstream Connection"]
        UCodec["Upstream Codec"]
        Backend["Backend Service"]
    end
    
    HF --> RR --> CS --> HS --> CP --> UR --> UC --> UCodec --> Backend
    
    style RR fill:#fff9c4
    style UR fill:#c8e6c9
```

## Router Filter Class

```mermaid
classDiagram
    class Filter {
        +decodeHeaders(headers, end_stream) FilterHeadersStatus
        +decodeData(data, end_stream) FilterDataStatus
        +decodeTrailers(trailers) FilterTrailersStatus
        +onUpstreamHeaders(headers, end_stream)
        +onUpstreamData(data, end_stream)
        +onUpstreamTrailers(trailers)
        -route_ : RouteConstSharedPtr
        -route_entry_ : RouteEntry*
        -upstream_requests_ : list~UpstreamRequestPtr~
        -callbacks_ : StreamDecoderFilterCallbacks*
        -config_ : FilterConfig
    }
    class UpstreamRequest {
        -parent_ : Filter
        -conn_pool_ : GenericConnPool
        -upstream_filter_manager_
        +acceptHeadersFromRouter(end_stream)
        +acceptDataFromRouter(data, end_stream)
        +acceptTrailersFromRouter(trailers)
        +onPoolReady(encoder, host)
        +onPoolFailure(reason)
    }
    class GenericConnPool {
        <<interface>>
        +newStream(GenericConnectionPoolCallbacks) void
        +cancelAnyPendingStream()
    }

    Filter --> UpstreamRequest : "upstream_requests_"
    UpstreamRequest --> GenericConnPool : "conn_pool_"
    Filter ..|> StreamDecoderFilter
```

**Location:** `source/common/router/router.h` (lines 262-434)

## Route Resolution

### How Routes Are Found

```mermaid
flowchart TD
    A["Router::decodeHeaders()"] --> B["callbacks_->route()"]
    B --> C{Route found?}
    C -->|No| D["sendLocalReply(404)<br/>No route found"]
    C -->|Yes| E{Direct response?}
    E -->|Yes| F["Send redirect/direct response"]
    E -->|No| G["route_entry_ = route->routeEntry()"]
    G --> H["cluster_name = route_entry_->clusterName()"]
    H --> I["cluster = cm_.getThreadLocalCluster(name)"]
    I --> J{Cluster exists?}
    J -->|No| K["sendLocalReply(503)<br/>No cluster"]
    J -->|Yes| L["Continue to host selection"]
```

Route resolution happens earlier in `ActiveStream::decodeHeaders()` via `refreshCachedRoute()`, but the Router filter accesses the cached route via `callbacks_->route()`.

```
File: source/common/router/router.cc (lines 364-448)

decodeHeaders():
    1. route_ = callbacks_->route()
    2. if (!route_) → 404
    3. Check for direct response → handle redirect
    4. route_entry_ = route_->routeEntry()
    5. cluster = cm_.getThreadLocalCluster(route_entry_->clusterName())
    6. if (!cluster) → 503
```

### Route Interfaces

```mermaid
classDiagram
    class Route {
        <<interface>>
        +routeEntry() RouteEntry*
        +directResponseEntry() DirectResponseEntry*
        +decorator() Decorator*
        +typedMetadata() TypedMetadata
    }
    class RouteEntry {
        <<interface>>
        +clusterName() string
        +timeout() Duration
        +retryPolicy() RetryPolicy
        +rateLimitPolicy() RateLimitPolicy
        +hashPolicy() HashPolicy
        +priority() RoutePriority
        +virtualHost() VirtualHost
    }
    class DirectResponseEntry {
        <<interface>>
        +responseCode() Code
        +responseBody() string
    }

    Route --> RouteEntry
    Route --> DirectResponseEntry
```

## Host Selection and Load Balancing

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant Cluster as ThreadLocalCluster
    participant LB as LoadBalancer
    participant Host as Selected Host

    Router->>Cluster: chooseHost(load_balancer_context)
    Cluster->>LB: chooseHost(context)
    Note over LB: Algorithm: round_robin,<br/>least_request, ring_hash, etc.
    LB->>LB: Apply zone-aware routing
    LB->>LB: Check host health
    LB-->>Cluster: selected host
    Cluster-->>Router: host (or nullptr)
    
    alt No healthy host
        Router->>Router: sendLocalReply(503, "no healthy upstream")
    else Host selected
        Router->>Router: createConnPool(cluster, host)
    end
```

## Creating the Upstream Request

### Connection Pool and UpstreamRequest Creation

```mermaid
flowchart TD
    A["Router::decodeHeaders()"] --> B["createConnPool(cluster, host)"]
    B --> C["GenericConnPoolFactory::createGenericConnPool()"]
    C --> D["Returns GenericConnPool (HTTP/TCP/UDP)"]
    D --> E["new UpstreamRequest(router, conn_pool, ...)"]
    E --> F["upstream_requests_.push_front(upstream_request)"]
    F --> G["upstream_request->acceptHeadersFromRouter(end_stream)"]
    G --> H["conn_pool_->newStream(this)"]
    
    H --> I{Connection ready?}
    I -->|"Immediately"| J["onPoolReady(encoder, host)"]
    I -->|"Queued"| K["Wait for connection"]
    K --> J
    I -->|"Failed"| L["onPoolFailure(reason)"]
```

```
File: source/common/router/router.cc (lines 624-749)

continueDecodeHeaders():
    1. createConnPool(cluster, host) → GenericConnPool
    2. new UpstreamRequest(this, conn_pool, ...)
    3. upstream_requests_.push_front(request)
    4. request->acceptHeadersFromRouter(end_stream)
       → conn_pool_->newStream(this)  // starts connection pooling
```

### UpstreamRequest and Upstream Filter Chain

`UpstreamRequest` has its own filter chain for upstream processing:

```mermaid
graph TD
    subgraph "UpstreamRequest"
        UFM["UpstreamFilterManager"]
        UF1["Upstream Filter 1"]
        UF2["Upstream Filter 2"]
        UCF["UpstreamCodecFilter<br/>(terminal)"]
    end
    
    Router["Router Filter"] -->|"acceptHeadersFromRouter()"| UFM
    UFM --> UF1 --> UF2 --> UCF
    UCF -->|"encode to upstream"| Codec["Upstream HTTP Codec"]
    
    style UCF fill:#ffcdd2
```

```
File: source/common/router/upstream_request.h (lines 42-66)

Payload arrives via accept[X]fromRouter functions:
    → Passed to UpstreamFilterManager
    → Filters process (if any upstream HTTP filters configured)
    → UpstreamCodecFilter (terminal) encodes to upstream codec
```

## onPoolReady — Connection Available

When a connection is available from the pool:

```mermaid
sequenceDiagram
    participant CP as Connection Pool
    participant UR as UpstreamRequest
    participant UCF as UpstreamCodecFilter
    participant Enc as RequestEncoder (upstream)

    CP->>UR: onPoolReady(encoder, host, stream_info, protocol)
    UR->>UR: Store encoder reference
    UR->>UCF: Create and wire up codec filter
    UR->>UCF: encodeHeaders(request_headers, end_stream)
    UCF->>Enc: encodeHeaders(headers, end_stream)
    Note over Enc: Serializes to upstream wire format
    
    alt Has body
        UR->>UCF: encodeData(buffered_data, end_stream)
        UCF->>Enc: encodeData(data, end_stream)
    end
    
    alt Has trailers
        UR->>UCF: encodeTrailers(trailers)
        UCF->>Enc: encodeTrailers(trailers)
    end
```

## Retry Handling

The Router filter implements retry logic:

```mermaid
flowchart TD
    A["Upstream response received"] --> B{Retry condition met?}
    B -->|"5xx, reset, timeout,<br/>retriable headers"| C{Retries remaining?}
    B -->|No| D["Forward response downstream"]
    C -->|Yes| E["Reset current UpstreamRequest"]
    C -->|No| D
    E --> F["Create new UpstreamRequest"]
    F --> G["Select host (may exclude previous)"]
    G --> H["Create new connection pool"]
    H --> I["Send request again"]
    I --> A
```

```mermaid
graph TD
    subgraph "Router with Retries"
        UR1["UpstreamRequest #1<br/>→ 503 (retryable)"]
        UR2["UpstreamRequest #2<br/>→ 200 OK ✓"]
    end
    
    UR1 -->|"retry"| UR2
    UR2 -->|"forward"| Downstream["Downstream"]
```

## Shadowing (Traffic Mirroring)

The Router can also shadow requests to additional clusters:

```mermaid
graph LR
    Router["Router Filter"]
    
    Router -->|"Primary"| Main["Production Cluster"]
    Router -->|"Shadow"| Shadow["Shadow Cluster<br/>(response discarded)"]
    
    Main --> Response["Response to client"]
```

## Router decodeHeaders() — Complete Flow

```mermaid
flowchart TD
    Start["decodeHeaders(headers, end_stream)"] --> ValidateRoute["Get and validate route"]
    ValidateRoute --> ConfigTimeout["Configure timeouts<br/>(route, cluster)"]
    ConfigTimeout --> SetupRetry["Setup retry state"]
    SetupRetry --> Hedge{Hedging enabled?}
    
    Hedge -->|Yes| CreateMultiple["Create multiple UpstreamRequests"]
    Hedge -->|No| CreateOne["Create single UpstreamRequest"]
    
    CreateOne --> ConnPool["createConnPool()"]
    ConnPool --> URCreate["new UpstreamRequest"]
    URCreate --> Accept["acceptHeadersFromRouter()"]
    Accept --> NewStream["conn_pool->newStream()"]
    
    NewStream --> Result{Result?}
    Result -->|"Pool ready"| SendHeaders["Send headers upstream"]
    Result -->|"Pool queued"| Wait["Wait for connection"]
    Result -->|"Pool overflow"| Retry{Can retry?}
    Retry -->|Yes| CreateOne
    Retry -->|No| Error503["sendLocalReply(503)"]
    
    SendHeaders --> Return["Return StopIteration<br/>(or Continue if more data)"]
```

## Data and Trailers Forwarding

After headers, the Router forwards body and trailers:

```mermaid
sequenceDiagram
    participant FM as HTTP FilterManager
    participant Router as Router Filter
    participant UR as UpstreamRequest

    FM->>Router: decodeData(body_chunk, false)
    alt Upstream ready
        Router->>UR: acceptDataFromRouter(body_chunk, false)
    else Upstream not ready (buffering)
        Router->>Router: Buffer data locally
    end
    
    FM->>Router: decodeData(last_chunk, true)
    Router->>UR: acceptDataFromRouter(last_chunk, true)
    Note over UR: end_stream=true signals end of request
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `source/common/router/router.h` | 262-434 | Router `Filter` class |
| `source/common/router/router.cc` | 364-432 | `decodeHeaders()` — route resolution |
| `source/common/router/router.cc` | 585 | Host selection |
| `source/common/router/router.cc` | 624-749 | `continueDecodeHeaders()` — conn pool + upstream request |
| `source/common/router/router.cc` | 752-786 | `createConnPool()` |
| `source/common/router/upstream_request.h` | 42-66 | `UpstreamRequest` class |
| `source/common/router/upstream_request.cc` | ~376 | `acceptHeadersFromRouter()` → `newStream()` |
| `source/extensions/filters/http/router/config.h` | 23 | Router is terminal filter |
| `source/extensions/filters/http/router/config.cc` | 15-21 | Router factory |
| `envoy/router/router.h` | 636-848 | Route interfaces |

---

**Previous:** [Part 9 — HTTP Filter Manager](09-http-filter-manager.md)  
**Next:** [Part 11 — Connection Pools and Upstream Connections](11-connection-pools.md)
