# Part 7: `source/common/router/` — Router Filter, Upstream Request, and Retry

## Overview

This document covers the runtime side of routing: the Router filter that forwards requests upstream, the `UpstreamRequest` that manages a single upstream attempt, the upstream codec filter, retry logic, and shadow writing.

## Router Filter Architecture

```mermaid
classDiagram
    class Filter {
        -route_ : RouteConstSharedPtr
        -route_entry_ : RouteEntry*
        -upstream_requests_ : list~UpstreamRequestPtr~
        -callbacks_ : StreamDecoderFilterCallbacks*
        -config_ : FilterConfigSharedPtr
        -retry_state_ : RetryStatePtr
        -downstream_headers_ : RequestHeaderMap*
        +decodeHeaders(headers, end_stream) FilterHeadersStatus
        +decodeData(data, end_stream) FilterDataStatus
        +decodeTrailers(trailers) FilterTrailersStatus
        +onUpstreamHeaders(headers, end_stream)
        +onUpstreamData(data, end_stream)
        +onUpstreamTrailers(trailers)
        +onUpstreamReset(reset_reason)
    }
    class FilterConfig {
        -stats_ : FilterStats
        -cm_ : ClusterManager
        -shadow_writer_ : ShadowWriter
        -upstream_http_filter_factories_ : vector
        +cm() ClusterManager
        +shadowWriter() ShadowWriter
    }
    class ProdFilter {
        +createRetryState() RetryState
    }
    class FilterUtility {
        +finalTimeout() TimeoutData
        +shouldShadow() bool
        +setUpstreamScheme()
    }

    ProdFilter --|> Filter
    Filter --> FilterConfig
    Filter ..> FilterUtility : "uses"
```

## Router Filter — decodeHeaders() Deep Dive

```mermaid
flowchart TD
    Start["decodeHeaders(headers, end_stream)"] --> GetRoute["route_ = callbacks_->route()"]
    GetRoute --> RouteCheck{route_ exists?}
    RouteCheck -->|No| Send404["sendLocalReply(404)\nNo route found"]
    RouteCheck -->|Yes| DirectResp{Direct response?}
    DirectResp -->|Yes| HandleDirect["Send redirect/direct response"]
    DirectResp -->|No| GetEntry["route_entry_ = route_->routeEntry()"]
    GetEntry --> GetCluster["cluster = cm_.getThreadLocalCluster(name)"]
    GetCluster --> ClusterCheck{Cluster exists?}
    ClusterCheck -->|No| Send503["sendLocalReply(503)\nNo cluster"]
    ClusterCheck -->|Yes| SetupTimeout["Configure timeouts"]
    SetupTimeout --> SetupRetry["Create retry state"]
    SetupRetry --> SelectHost["Choose host via load balancer"]
    SelectHost --> CreatePool["createConnPool(cluster, host)"]
    CreatePool --> PoolCheck{Pool created?}
    PoolCheck -->|No| Send503_2["sendLocalReply(503)\nNo healthy upstream"]
    PoolCheck -->|Yes| CreateUR["new UpstreamRequest(pool)"]
    CreateUR --> AddToList["upstream_requests_.push_front(ur)"]
    AddToList --> Accept["ur->acceptHeadersFromRouter(end_stream)"]
    Accept --> NewStream["conn_pool_->newStream(callbacks)"]
```

## UpstreamRequest — Per-Attempt Request

```mermaid
classDiagram
    class UpstreamRequest {
        -parent_ : RouterFilterInterface&
        -conn_pool_ : GenericConnPoolPtr
        -upstream_filter_manager_ : UpstreamFilterManager
        -upstream_host_ : HostDescriptionConstSharedPtr
        -calling_encode_headers_ : bool
        -encode_complete_ : bool
        -decode_complete_ : bool
        +acceptHeadersFromRouter(end_stream)
        +acceptDataFromRouter(data, end_stream)
        +acceptTrailersFromRouter(trailers)
        +onPoolReady(encoder, host, info, protocol)
        +onPoolFailure(reason, host)
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +resetStream()
    }
    class UpstreamFilterManager {
        +decodeHeaders()
        +decodeData()
        +decodeTrailers()
    }
    class UpstreamRequestFilterManagerCallbacks {
        +encodeHeaders() → upstream_request_.decodeHeaders()
        +encodeData() → upstream_request_.decodeData()
        +encodeTrailers() → upstream_request_.decodeTrailers()
    }

    UpstreamRequest --> UpstreamFilterManager
    UpstreamRequest --> UpstreamRequestFilterManagerCallbacks
```

### UpstreamRequest Data Flow

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant UR as UpstreamRequest
    participant UFM as UpstreamFilterManager
    participant UCF as UpstreamCodecFilter
    participant Enc as Upstream RequestEncoder
    participant Dec as Upstream ResponseDecoder (CodecBridge)

    Note over Router,Enc: REQUEST PATH
    Router->>UR: acceptHeadersFromRouter(end_stream)
    UR->>UR: conn_pool_->newStream(this)
    Note over UR: Wait for pool ready...
    UR->>UR: onPoolReady(encoder, host)
    UR->>UFM: encodeHeaders(headers, end_stream)
    UFM->>UCF: encodeHeaders(headers, end_stream)
    UCF->>Enc: encodeHeaders(headers, end_stream)
    
    Router->>UR: acceptDataFromRouter(data, end_stream)
    UR->>UFM: encodeData(data, end_stream)
    UFM->>UCF: encodeData(data, end_stream)
    UCF->>Enc: encodeData(data, end_stream)
    
    Note over Dec,Router: RESPONSE PATH
    Dec->>UCF: onUpstreamHeaders(headers)
    UCF->>UFM: decoder_callbacks_->encodeHeaders()
    UFM->>UR: FilterManagerCallbacks::encodeHeaders()
    UR->>Router: parent_.onUpstreamHeaders(headers)
    Router->>Router: callbacks_->encodeHeaders(headers)
```

## UpstreamCodecFilter and CodecBridge

```mermaid
classDiagram
    class UpstreamCodecFilter {
        -bridge_ : CodecBridgePtr
        +encodeHeaders(headers, end_stream) FilterHeadersStatus
        +encodeData(data, end_stream) FilterDataStatus
        +encodeTrailers(trailers) FilterTrailersStatus
        +onUpstreamHeaders(headers, end_stream)
        +onUpstreamData(data, end_stream)
        +onUpstreamTrailers(trailers)
    }
    class CodecBridge {
        -filter_ : UpstreamCodecFilter&
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
    }
    class UpstreamToDownstreamImplBase {
        +decodeHeaders()
        +decodeData()
        +decodeTrailers()
    }

    UpstreamCodecFilter --> CodecBridge : "owns"
    CodecBridge --|> UpstreamToDownstreamImplBase
```

`UpstreamCodecFilter` is the terminal upstream filter — it encodes requests to the upstream codec and receives responses from it.

## Retry Logic

### RetryStateImpl

```mermaid
classDiagram
    class RetryState {
        <<interface>>
        +shouldRetryHeaders(headers, callback) RetryDecision
        +shouldRetryReset(reason, callback) RetryDecision
        +onHostAttempted(host)
        +shouldSelectAnotherHost(host) bool
        +hostSelectionMaxAttempts() uint32_t
    }
    class RetryStateImpl {
        -retry_on_ : uint32_t
        -retries_remaining_ : uint32_t
        -backoff_strategy_ : BackoffStrategy
        -host_predicates_ : vector
        -retry_priority_ : RetryPriority
        -retry_host_predicate_configs_ : vector
        +shouldRetryHeaders(headers, callback) RetryDecision
        +shouldRetryReset(reason, callback) RetryDecision
    }

    RetryStateImpl ..|> RetryState
```

### Retry Decision Flow

```mermaid
flowchart TD
    A["Upstream response received"] --> B{Check retry conditions}
    
    B --> C{5xx response?}
    C -->|"Yes & retry_on has 5xx"| Retry
    C -->|No| D{Connection reset?}
    D -->|"Yes & retry_on has reset"| Retry
    D -->|No| E{Gateway error?}
    E -->|"Yes & retry_on has gateway-error"| Retry
    E -->|No| F{Retriable status code?}
    F -->|"Yes & code in retriable_status_codes"| Retry
    F -->|No| G{Retriable header match?}
    G -->|"Yes & header matches"| Retry
    G -->|No| Forward["Forward response downstream"]
    
    Retry --> H{Retries remaining?}
    H -->|No| Forward
    H -->|Yes| I["Calculate backoff delay"]
    I --> J["Schedule retry timer"]
    J --> K["Reset upstream request"]
    K --> L["Select new host\n(may exclude previous)"]
    L --> M["Create new UpstreamRequest"]
    M --> N["Send request to new host"]
```

### Retry Backoff Strategy

```mermaid
graph LR
    subgraph "Exponential Backoff"
        R1["Attempt 1: 25ms"] --> R2["Attempt 2: 50ms"]
        R2 --> R3["Attempt 3: 100ms"]
        R3 --> R4["Attempt 4: 200ms"]
        R4 --> Rn["... up to max_interval"]
    end
    
    Note["base_interval = 25ms (default)\nmax_interval = 10 * base_interval\nJitter applied to each interval"]
```

### Host Selection During Retries

```mermaid
flowchart TD
    A["Retry: select host"] --> B["Load balancer chooses host"]
    B --> C{Host predicates?}
    C -->|Yes| D{Previous host excluded?}
    D -->|Yes| E["Try another host"]
    D -->|No| F{Canary host excluded?}
    F -->|Yes| E
    F -->|No| G["Use this host"]
    E --> B
    C -->|No| G
    
    G --> H{Max selection attempts reached?}
    H -->|Yes| I["Use last selected host"]
    H -->|No| G
```

## Shadow Writing (Traffic Mirroring)

```mermaid
classDiagram
    class ShadowWriter {
        <<interface>>
        +shadow(cluster, message, options)
    }
    class ShadowWriterImpl {
        -cm_ : ClusterManager&
        +shadow(cluster, message, options)
    }

    ShadowWriterImpl ..|> ShadowWriter
```

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant SW as ShadowWriterImpl
    participant AC as AsyncClient

    Router->>Router: decodeHeaders() → check shadow policies
    
    alt Shadow enabled for this route
        Router->>SW: shadow(shadow_cluster, request_copy, options)
        SW->>AC: send(request_copy, null_callbacks)
        Note over AC: Fire-and-forget\nResponse discarded
    end
    
    Router->>Router: Continue with primary upstream
```

## Hedging

```mermaid
graph LR
    subgraph "Hedged Request"
        UR1["UpstreamRequest #1\n→ Host A"]
        UR2["UpstreamRequest #2\n→ Host B"]
    end
    
    UR1 -->|"First response wins"| Response["Response to downstream"]
    UR2 -->|"Cancelled"| Cancel["Cancel other request"]
```

When hedging is enabled, the Router creates multiple `UpstreamRequest` objects simultaneously. The first successful response is forwarded; others are cancelled.

## File Catalog

| File | Key Classes | Purpose |
|------|-------------|---------|
| `router.h/cc` | `Filter`, `ProdFilter`, `FilterConfig`, `FilterUtility` | Router HTTP filter |
| `upstream_request.h/cc` | `UpstreamRequest`, `UpstreamFilterManager`, callbacks | Per-attempt upstream request |
| `upstream_codec_filter.h/cc` | `UpstreamCodecFilter`, `CodecBridge` | Terminal upstream filter |
| `upstream_to_downstream_impl_base.h` | `UpstreamToDownstreamImplBase` | Response bridge base |
| `retry_state_impl.h/cc` | `RetryStateImpl` | Retry logic |
| `retry_policy_impl.h/cc` | `RetryPolicyImpl` | Retry policy from config |
| `reset_header_parser.h/cc` | `ResetHeaderParserImpl` | Retry-After/X-RateLimit-Reset parsing |
| `shadow_writer_impl.h/cc` | `ShadowWriterImpl` | Traffic mirroring |
| `debug_config.h/cc` | `DebugConfig` | Router debug filter state |
| `string_accessor_impl.h` | `StringAccessorImpl` | String filter state |

---

**Previous:** [Part 6 — Routing Engine and Configuration](06-router-engine-config.md)  
**Next:** [Part 8 — Cluster Manager and Clusters](08-upstream-cluster-manager.md)
