# Envoy HTTP Layer — Overview Part 4: Async Client, HTTP/3 Properties & Advanced Topics

**Directory:** `source/common/http/`  
**Part:** 4 of 4 — Async HTTP Client, HTTP Server Properties Cache, HTTP/3 Tracking, Watermarks, Tracing, Overload, Misc Components

---

## Table of Contents

1. [Async HTTP Client](#1-async-http-client)
2. [HTTP Server Properties Cache](#2-http-server-properties-cache)
3. [HTTP/3 Status Tracker](#3-http3-status-tracker)
4. [Watermark and Backpressure System](#4-watermark-and-backpressure-system)
5. [Request ID Extension](#5-request-id-extension)
6. [Overload Manager Integration](#6-overload-manager-integration)
7. [Session Idle List](#7-session-idle-list)
8. [MuxDemux — Fan-Out Streaming](#8-muxdemux--fan-out-streaming)
9. [Tracing Integration](#9-tracing-integration)
10. [SSE Parser](#10-sse-parser)
11. [Matching Framework Integration](#11-matching-framework-integration)
12. [Component Interaction Summary](#12-component-interaction-summary)

---

## 1. Async HTTP Client

### Architecture Recap

`AsyncClientImpl` provides Envoy's in-process HTTP client for internal use by filters and extensions (rate limiting, auth, health checks, etc.). It reuses the full router/LB/pool infrastructure without a downstream network connection.

```mermaid
flowchart LR
    subgraph "Caller (e.g., RateLimit filter)"
        F["Filter::decodeHeaders()"]
    end

    subgraph "AsyncClientImpl"
        ACI["AsyncClientImpl"]
        ASI["AsyncStreamImpl\nimplements StreamDecoderFilterCallbacks"]
        NRI["NullRouteImpl\n(fake route for Router)"]
    end

    subgraph "Router + Upstream"
        RF["Router::RouterFilter"]
        Pool["ConnPool"]
        Upstream["Real Upstream"]
    end

    F -->|send(request, callbacks)| ACI
    ACI --> ASI
    ASI -->|decodeHeaders| RF
    RF -->|uses| NRI
    RF -->|newStream| Pool
    Pool --> Upstream
    Upstream -->|response| Pool
    Pool --> RF
    RF -->|encodeHeaders| ASI
    ASI -->|onHeaders/onSuccess| F
```

### Request API vs Stream API

```mermaid
flowchart TD
    A[AsyncClient::send()] --> B["AsyncRequestImpl\n(buffers full response\nup to 32 MB)"]
    B --> C["callbacks.onSuccess(response_message)\nor\ncallbacks.onFailure(reason)"]

    D[AsyncClient::start()] --> E["AsyncStreamImpl\n(streaming, no buffering)"]
    E --> F["callbacks.onHeaders(headers)\ncallbacks.onData(data, end_stream)\ncallbacks.onReset()"]
```

### Retry and Cancellation

```mermaid
sequenceDiagram
    participant Filter as Caller Filter
    participant ACI as AsyncClientImpl
    participant ASI as AsyncStreamImpl
    participant RF as Router::RouterFilter

    Filter->>ACI: send(request, callbacks, {timeout: 5s, retry_policy: 3})
    ACI->>ASI: create with options
    ASI->>RF: decodeHeaders (triggers upstream request)

    alt Upstream fails (1st attempt)
        RF->>RF: retry (retry policy from options)
        RF->>ASI: retry transparent to caller
    end

    alt Timeout
        ASI->>ASI: timeout timer fires
        ASI->>RF: reset()
        ASI->>Filter: callbacks.onFailure(RequestTimeout)
    end

    alt Caller cancels
        Filter->>ASI: cancel()
        ASI->>RF: reset()
    end
```

### `NullRouteImpl` Stubs

```mermaid
classDiagram
    class NullRouteImpl {
        +virtualHost(): NullVirtualHost&
        +routeEntry(): NullRouteEntry*
        +decorator(): nullptr
        +metadata(): empty_metadata
        +typedMetadata(): empty
    }

    class NullRouteEntry {
        +clusterName(): cluster_name_
        +timeout(): options.timeout
        +retryPolicy(): NullRetryPolicy
        +rateLimitPolicy(): NullRateLimitPolicy
        +shadowPolicies(): options.shadow_policies
        +hashPolicy(): options.hash_policy
    }

    class NullVirtualHost {
        +name(): "async-client"
        +rateLimitPolicy(): NullRateLimitPolicy
        +corsPolicy(): nullptr
    }

    NullRouteImpl *-- NullRouteEntry
    NullRouteImpl *-- NullVirtualHost
```

---

## 2. HTTP Server Properties Cache

**Files:** `http_server_properties_cache_impl.h/.cc`, `http_server_properties_cache_manager_impl.h/.cc`

The `HttpServerPropertiesCacheImpl` stores **per-origin** data that influences protocol selection, primarily to support HTTP/3 via Alt-Svc headers.

### What It Caches

```mermaid
mindmap
  root((HttpServerPropertiesCache))
    Alt-Svc Protocols
      h3=":443" header parsing
      AlternateProtocol objects
      expiry tracking
    QUIC RTT
      Smoothed RTT per origin
      Used for H3 happy eyeballs timing
    HTTP/3 Status
      Confirmed / Broken / Pending
      Linked to Http3StatusTracker
    Max Concurrent Streams
      Per-origin advertised max
    Canonical Suffix Matching
      All .example.com domains\nshare Alt-Svc from example.com
```

### Alt-Svc Processing Flow

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant Cache as HttpServerPropertiesCacheImpl
    participant Grid as ConnectivityGrid

    Router->>Cache: findAlternates(origin)
    Cache-->>Router: [] (no H3 known yet)
    Router->>Grid: use TCP pool

    Note over Router: Response arrives with Alt-Svc: h3=":443"; ma=86400
    Router->>Cache: setAlternates(origin, [h3:443, expires=+86400s])

    Router->>Cache: findAlternates(origin)
    Cache-->>Router: [AlternateProtocol{h3, port=443}]
    Router->>Grid: prefer H3 pool for this origin
```

### Cache Eviction

The cache is backed by `quiche::QuicheLinkedHashMap` (LRU order) with a configurable size limit. When the limit is reached, the least-recently-used origin entry is evicted:

```mermaid
flowchart TD
    A[setAlternates for new origin] --> B{Cache size == max_entries?}
    B -->|Yes| C[Evict LRU entry]
    B -->|No| D[Insert new entry]
    C --> D
    D --> E[Optionally persist to KeyValueStore]
```

### Persistence via `KeyValueStore`

If a `KeyValueStore` is configured, the cache serializes to disk on insert and restores on startup — enabling Alt-Svc knowledge to survive Envoy restarts without waiting for re-discovery:

```mermaid
flowchart LR
    A[Envoy startup] --> B[KeyValueStore::iterate()]
    B --> C[Deserialize entries into Cache]

    D[setAlternates(origin, protos)] --> E[Cache::storeAlternate()]
    E --> F[KeyValueStore::addOrUpdate(key, serialized)]
```

### Cache Manager

`HttpServerPropertiesCacheManagerImpl` is a singleton (per-thread-local) that manages one cache instance per configured cluster:

```mermaid
flowchart TD
    TLS["Thread-local slot"] --> Mgr["HttpServerPropertiesCacheManagerImpl"]
    Mgr --> C1["Cache for cluster: backend-h3"]
    Mgr --> C2["Cache for cluster: api-cluster"]
    Grid1["ConnectivityGrid (backend-h3)"] --> C1
    Grid2["ConnectivityGrid (api-cluster)"] --> C2
```

---

## 3. HTTP/3 Status Tracker

**Files:** `http3_status_tracker_impl.h/.cc`

`Http3StatusTrackerImpl` manages the per-origin HTTP/3 health state with exponential backoff to avoid hammering broken H3 endpoints.

### State Machine

```mermaid
stateDiagram-v2
    [*] --> Pending : Initial state (no H3 attempt yet)
    Pending --> Confirmed : First H3 stream succeeds
    Pending --> Broken : H3 connection fails
    Confirmed --> Broken : H3 error on confirmed origin
    Broken --> Pending : Backoff timer expires
    Broken --> Broken : Within backoff window
    Confirmed --> Confirmed : Normal H3 operation
```

### Exponential Backoff Schedule

```mermaid
flowchart TD
    F1["1st failure\nbackoff = 1s"] --> F2["2nd failure\nbackoff = 2s"]
    F2 --> F3["3rd failure\nbackoff = 4s"]
    F3 --> F4["4th failure\nbackoff = 8s"]
    F4 --> FN["Nth failure\nbackoff = min(2^n s, 5 min)"]
```

### Integration with ConnectivityGrid

```mermaid
sequenceDiagram
    participant Grid as ConnectivityGrid
    participant Tracker as Http3StatusTrackerImpl

    Grid->>Tracker: isHttp3Broken()
    Tracker-->>Grid: false (not broken)
    Grid->>H3Pool: attempt H3

    Note over Grid,Tracker: H3 connection fails
    Grid->>Tracker: onHttp3ConnectionFailure()
    Tracker->>Tracker: broken_ = true, start backoff timer

    Grid->>Tracker: isHttp3Broken()
    Tracker-->>Grid: true (within backoff window)
    Grid->>H2Pool: skip H3, use TCP directly

    Note over Tracker: Backoff expires
    Tracker->>Tracker: broken_ = false, state = Pending

    Grid->>Tracker: isHttp3Broken()
    Tracker-->>Grid: false
    Grid->>H3Pool: retry H3
```

---

## 4. Watermark and Backpressure System

Envoy uses a hierarchical watermark system to prevent buffer exhaustion at every level of the stack.

### Watermark Hierarchy

```mermaid
flowchart TD
    subgraph Downstream
        DS_Conn["Network Connection\nWrite Buffer"]
        DS_Stream["HTTP/2 Stream\nFlow Control"]
    end

    subgraph FilterChain
        FH["Filter A\nbuffered_body_"]
        FI["Filter B\nbuffered_body_"]
    end

    subgraph Upstream
        US_Stream["Upstream HTTP/2\nStream Flow Control"]
        US_Conn["Network Connection\nWrite Buffer"]
    end

    DS_Conn -->|high watermark| DS_Stream
    DS_Stream -->|propagate| FH
    FH -->|propagate| FI
    FI -->|stop reading upstream| US_Stream
    US_Stream -->|WINDOW_UPDATE blocked| US_Conn
```

### Watermark Events

| Event | Direction | Effect |
|-------|-----------|--------|
| `onAboveWriteBufferHighWatermark` | Downstream → Filter Chain | Pause encoding to downstream |
| `onBelowWriteBufferLowWatermark` | Downstream → Filter Chain | Resume encoding to downstream |
| `onDecoderFilterAboveWriteBufferHighWatermark` | Filter → Connection Manager | Pause reading from upstream |
| `onDecoderFilterBelowWriteBufferLowWatermark` | Filter → Connection Manager | Resume reading from upstream |
| `onEncoderFilterAboveWriteBufferHighWatermark` | Filter → Router | Pause upstream response buffering |

### `StreamFilterSidestreamWatermarkCallbacks`

Used by side-stream filters (filters that make their own upstream requests while processing the main stream):

```mermaid
classDiagram
    class StreamFilterSidestreamWatermarkCallbacks {
        +onSidestreamAboveHighWatermark()
        +onSidestreamBelowLowWatermark()
        -main_stream_callbacks_: StreamDecoderFilterCallbacks*
    }
```

---

## 5. Request ID Extension

**Files:** `request_id_extension_impl.h/.cc`

The default `RequestIDExtension` generates and propagates `x-request-id` (UUID v4):

```mermaid
sequenceDiagram
    participant CMU as ConnectionManagerUtility
    participant RID as RequestIDExtensionImpl
    participant Headers as Request Headers

    CMU->>RID: setInRequest(headers, random_generator)
    RID->>Headers: x-request-id present?
    alt No x-request-id
        RID->>RID: generate UUID v4
        RID->>Headers: set x-request-id: <uuid>
    else Already present
        RID->>Headers: keep existing (propagate)
    end

    CMU->>RID: setInResponse(request_headers, response_headers)
    RID->>response_headers: copy x-request-id from request
```

### Custom Implementations

The `RequestIDExtension` interface can be replaced (via `request_id_extension` config) with custom implementations that use different ID formats (e.g., trace IDs, W3C Trace Context).

---

## 6. Overload Manager Integration

`ConnectionManagerImpl` integrates with Envoy's `OverloadManager` to shed load when the process is under resource pressure (CPU, memory, file descriptors).

```mermaid
flowchart TD
    subgraph OverloadManager
        Action1["StopAcceptingRequests\n(stop new streams)"]
        Action2["StopAcceptingConnections\n(stop TCP accept)"]
        Action3["DisableHTTP1KeepAlive\n(close connections sooner)"]
        Action4["RejectIncomingConnections\n(reject at L3/L4)"]
    end

    subgraph ConnectionManagerImpl
        NewStream["newStream()"]
        OnData["onData()"]
        ActiveStream["ActiveStream::decodeHeaders()"]
    end

    Action1 --> NewStream
    Action1 --> ActiveStream
    Action2 --> OnData
    Action3 --> ActiveStream
```

### Overload Actions in Request Flow

```mermaid
sequenceDiagram
    participant Net
    participant CMI as ConnectionManagerImpl
    participant OLM as OverloadManager

    Net->>CMI: onData()
    CMI->>OLM: isOverloaded(StopAcceptingRequests)?
    alt Overloaded
        OLM-->>CMI: true
        CMI->>Net: send 503 + close (or drain)
    else Not overloaded
        OLM-->>CMI: false
        CMI->>CMI: process normally
    end
```

---

## 7. Session Idle List

**Files:** `session_idle_list.h/.cc`, `session_idle_list_interface.h`

`SessionIdleList` is an overload-aware data structure that tracks idle HTTP/2 and HTTP/3 streams and evicts them when Envoy is under memory pressure.

```mermaid
classDiagram
    class SessionIdleList {
        +add(stream): void
        +evictIdleSessions(count): void
        -sessions_: list~StreamRef~
        -overload_state_: OverloadState&
    }

    class SessionIdleListInterface {
        <<interface>>
        +add(stream): void
        +remove(stream): void
    }

    SessionIdleListInterface <|-- SessionIdleList
```

### Eviction Strategy

```mermaid
flowchart TD
    OLM["OverloadManager detects\nhigh memory pressure"] --> IL["SessionIdleList::evictIdleSessions(count)"]
    IL --> L["Walk list from oldest idle session"]
    L --> B{Session still idle?}
    B -->|Yes| R["Reset stream (RST_STREAM / RESET_STREAM)"]
    B -->|No - became active| Skip["Skip, move to next"]
    R --> C{count reached?}
    C -->|Yes| Done["Stop"]
    C -->|No| L
```

---

## 8. MuxDemux — Fan-Out Streaming

**Files:** `muxdemux.h/.cc`

`MuxDemux` enables a single upstream request to fan out to multiple `AsyncClient::Stream` subscribers. Used internally for features like mirroring and multi-consumer response streaming.

```mermaid
flowchart TD
    Upstream["Upstream Response\n(single stream)"] --> MuxDemux
    MuxDemux --> S1["Subscriber 1\n(primary consumer)"]
    MuxDemux --> S2["Subscriber 2\n(shadow / mirror)"]
    MuxDemux --> S3["Subscriber 3\n(analytics)"]
```

### `MultiStream` — Fan-Out Semantics

```mermaid
classDiagram
    class MuxDemux {
        +addStream(decoder): void
        +removeStream(decoder): void
        -streams_: list~ResponseDecoder*~
    }

    class MultiStream {
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        -streams_: list~ResponseDecoder*~
    }

    MultiStream --> MuxDemux : uses
```

---

## 9. Tracing Integration

`ConnectionManagerImpl` integrates tracing at the `ActiveStream` level:

```mermaid
sequenceDiagram
    participant AS as ActiveStream
    participant Tracer as Tracing::HttpTracerImpl
    participant Span as Tracing::Span

    AS->>AS: decodeHeaders()
    AS->>CMU: mutateTracingRequestHeader()
    CMU->>AS: sampling decision

    alt Sampled
        AS->>Tracer: startSpan(config, request_headers, stream_info, ...)
        Tracer-->>AS: Span (active)
        AS->>Span: setTag("http.method", "GET")
        AS->>Span: setTag("http.url", "/api/v1")
        Note over AS: Request processed...
        AS->>Span: setTag("http.status_code", "200")
        AS->>Span: finishSpan()
        AS->>Tracer: injectContext(span, request_headers)
    else Not sampled
        AS->>AS: NullSpan (no-op)
    end
```

### Tracing Stats Charged by `ConnectionManagerImpl`

| Sampling Reason | Stat |
|----------------|------|
| `RandomSampling` | `tracing.random_sampling` |
| `ServiceForced` | `tracing.service_forced` |
| `ClientForced` | `tracing.client_enabled` |
| `NotTraced` | `tracing.not_traceable` |
| `HealthCheck` | `tracing.health_check` |

---

## 10. SSE Parser

**Files:** `sse/sse_parser.h/.cc`

The SSE (Server-Sent Events) parser handles chunked streaming responses in the SSE format (`text/event-stream`):

```mermaid
flowchart TD
    DataChunk["HTTP response data chunk:\n'data: hello world\n\n'"] --> SSE["SseParser::decode(chunk)"]
    SSE --> E1["Event { type=message, data='hello world', id='' }"]
    SSE --> E2["... more events"]
    E1 --> CB["Callback::onEvent(event)"]
```

### SSE Event Format

```
data: <payload>\n
event: <optional event type>\n
id: <optional event id>\n
retry: <optional reconnect time ms>\n
\n
(blank line = event boundary)
```

---

## 11. Matching Framework Integration

**Directory:** `matching/`

The matching sub-directory provides input data sources for the Envoy unified Matcher API, allowing HTTP-specific attributes to be used in generic match trees.

```mermaid
classDiagram
    class HttpRequestHeadersDataInput {
        +get(data): RequestHeaderMap&
    }

    class HttpResponseHeadersDataInput {
        +get(data): ResponseHeaderMap&
    }

    class HttpResponseStatusCodeInput {
        +get(data): uint64_t (status code)
    }

    class HttpResponseStatusCodeClassInput {
        +get(data): StatusCodeClass (1xx/2xx/3xx/4xx/5xx)
    }

    class DataInput~T~ { <<interface>> }
    DataInput <|-- HttpRequestHeadersDataInput
    DataInput <|-- HttpResponseHeadersDataInput
    DataInput <|-- HttpResponseStatusCodeInput
    DataInput <|-- HttpResponseStatusCodeClassInput
```

### Matcher Usage in Filter Config

```yaml
# Example: per-filter match tree in HTTP filter config
http_filters:
  - name: envoy.filters.http.jwt_authn
    typed_config: ...
    config_discovery:
      apply_default_config_without_warming: true
    # Match tree: only apply JWT auth to /api/* paths
    config_discovery:
      ...
```

```mermaid
flowchart TD
    Req["Incoming Request"] --> MT["MatchTree::match(HttpMatchingDataImpl)"]
    MT --> Input1["HttpRequestHeadersDataInput\n(provides :path, :method, etc.)"]
    MT --> B{path matches /api/*?}
    B -->|Yes| Action["Apply JwtAuthn filter"]
    B -->|No| Skip["Skip filter"]
```

---

## 12. Component Interaction Summary

The following diagram shows how all major components in `source/common/http/` interact with each other:

```mermaid
graph TD
    subgraph "Entry Point"
        CMI["ConnectionManagerImpl"]
    end

    subgraph "Protocol Detection & Codec Creation"
        CMU["ConnectionManagerUtility"]
        H1["Http1::ServerConnectionImpl"]
        H2["Http2::ServerConnectionImpl"]
        H3["Http3::ServerConnectionImpl"]
    end

    subgraph "Per-Request"
        AS["ActiveStream"]
        DFM["DownstreamFilterManager"]
        FM_Filters["Filter Chain (A→B→C)"]
    end

    subgraph "Upstream"
        RF["Router::RouterFilter"]
        Pool["ConnPool\n(H1/H2/H3/Grid/Mixed)"]
        CC["CodecClient"]
        HSP["HttpServerPropertiesCache"]
        H3ST["Http3StatusTracker"]
    end

    subgraph "Headers"
        HMI["HeaderMapImpl"]
        HU["HeaderUtility"]
        HM["HeaderMutation"]
    end

    subgraph "Internal Client"
        ACI["AsyncClientImpl"]
        ASI["AsyncStreamImpl"]
        NRI["NullRouteImpl"]
    end

    subgraph "Advanced"
        OLM["OverloadManager"]
        SIL["SessionIdleList"]
        Tracer["Tracing::HttpTracerImpl"]
        RID["RequestIDExtension"]
    end

    CMI --> CMU
    CMU --> H1 & H2 & H3
    H1 & H2 & H3 --> AS
    AS --> DFM
    DFM --> FM_Filters
    FM_Filters --> RF
    RF --> Pool
    Pool --> CC
    Pool --> HSP
    HSP --> H3ST
    AS --> HMI
    HMI --> HU
    HU --> HM
    ACI --> ASI
    ASI --> RF
    ASI --> NRI
    CMI --> OLM
    CMI --> SIL
    AS --> Tracer
    CMU --> RID
```

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_request_pipeline.md) | Architecture, Request Pipeline, ConnectionManager, FilterSystem |
| [Part 2](OVERVIEW_PART2_codecs_and_pools.md) | Codecs (H1/H2/H3), Connection Pools, Protocol Details |
| [Part 3](OVERVIEW_PART3_headers_and_utilities.md) | Header System, Utilities, Path Normalization |
| **Part 4 (this file)** | Async Client, HTTP/3, Server Properties, Advanced Topics |

---

## Index of Individual File Documentation

| File | Individual Doc |
|------|---------------|
| `conn_manager_impl.h/.cc` | [conn_manager_impl.md](conn_manager_impl.md) |
| `filter_manager.h/.cc` | [filter_manager.md](filter_manager.md) |
| `codec_client.h/.cc` | [codec_client.md](codec_client.md) |
| `header_map_impl.h/.cc` | [header_map_impl.md](header_map_impl.md) |
| `conn_pool_base.h/.cc` + `conn_pool_grid.h/.cc` | [conn_pool_base_and_grid.md](conn_pool_base_and_grid.md) |
| `async_client_impl.h/.cc` | [async_client_impl.md](async_client_impl.md) |
| `conn_manager_utility.h/.cc` | [conn_manager_utility.md](conn_manager_utility.md) |
