# Envoy HTTP Layer — Overview Part 1: Architecture & Request Pipeline

**Directory:** `source/common/http/`  
**Part:** 1 of 4 — Architecture, Request Pipeline, Connection Manager, Filter System

---

## Table of Contents

1. [High-Level Architecture](#1-high-level-architecture)
2. [Component Map](#2-component-map)
3. [End-to-End Request Flow](#3-end-to-end-request-flow)
4. [ConnectionManagerImpl Deep Dive](#4-connectionmanagerimpl-deep-dive)
5. [FilterManager and the Filter Chain](#5-filtermanager-and-the-filter-chain)
6. [FilterChainHelper — Building the Chain](#6-filterchainhelper--building-the-chain)
7. [Key Design Patterns](#7-key-design-patterns)

---

## 1. High-Level Architecture

**This diagram shows the complete HTTP request/response pipeline through Envoy:**

**Downstream Path (Client Request → Envoy):**

**1. Network Layer → HTTP Layer Transition:**
- Raw TCP/QUIC bytes arrive on network connection
- `ConnectionManagerImpl` is a **Network::ReadFilter** - bridges network and HTTP layers
- It's the entry point from network layer into HTTP processing

**2. Protocol Detection & Codec Creation:**
- `ConnectionManagerUtility` sniffs first bytes to detect protocol (HTTP/1.1, HTTP/2, HTTP/3)
- Creates appropriate codec: `Http1::ServerConnection`, `Http2::ServerConnection`, or `Http3::ServerConnection`
- Codec parses raw bytes into HTTP semantic elements (headers, data, trailers)

**3. Stream Creation (Per Request):**
- Codec creates `ActiveStream` for each HTTP request
- One connection can have multiple concurrent streams (HTTP/2, HTTP/3)
- HTTP/1.1 has one stream per connection at a time

**4. Filter Chain Execution:**
- `DownstreamFilterManager` owns the HTTP filter chain
- Filters execute in order: A → B → C → Router
- Each filter can:
  - **Inspect**: Read headers/data
  - **Modify**: Change headers, body, trailers
  - **Halt**: Stop for async operations (database lookup, external auth)
  - **Generate**: Create local response (auth failure, rate limit)

**Upstream Path (Envoy → Backend):**

**5. Routing & Connection Pool:**
- Router filter determines upstream cluster based on routing rules
- Connection pool manages pooled connections to upstream hosts
- Pools are protocol-specific: HTTP/1.1, HTTP/2, HTTP/3, or Grid (mixed)

**6. CodecClient & Upstream Connection:**
- `CodecClient` wraps upstream connection with codec
- Sends HTTP request to upstream server
- Manages request/response correlation

**Response Path (Reverse):**
- Response flows back through same components in reverse
- Upstream → CodecClient → Pool → Router → Filter C → B → A → Codec → Client
- Encoder filters process response (reverse order from decoder)

**Key Design Points:**
- **Separation of concerns**: Network layer, protocol layer, application layer
- **Protocol abstraction**: Filters don't care about HTTP/1 vs HTTP/2 vs HTTP/3
- **Bidirectional filter chain**: Decoder filters for requests, encoder filters for responses
- **Async support**: Filters can pause processing for async operations

```mermaid
graph TB
    subgraph "Downstream (Client)"
        DS_Net["TCP/UDP/QUIC\nNetwork Connection"]
    end

    subgraph "source/common/http — Core Pipeline"
        CMI["ConnectionManagerImpl\n(Network::ReadFilter)"]
        CMU["ConnectionManagerUtility\n(protocol detection, header mutation)"]
        Codec["ServerCodec\n(H1 / H2 / H3)"]
        AS["ActiveStream\n(per request)"]
        DFM["DownstreamFilterManager\n(filter chain engine)"]

        subgraph "Filter Chain"
            FA["Filter A\n(e.g. JWT auth)"]
            FB["Filter B\n(e.g. rate limit)"]
            FC["Filter C\n(Router)"]
        end
    end

    subgraph "Upstream Infrastructure"
        Pool["Connection Pool\n(H1/H2/H3/Grid)"]
        CC["CodecClient"]
        US_Net["Upstream TCP/QUIC"]
    end

    DS_Net -->|raw bytes| CMI
    CMI -->|protocol sniff| CMU
    CMU -->|creates| Codec
    Codec -->|streams| AS
    AS --> DFM
    DFM --> FA --> FB --> FC
    FC -->|route + LB| Pool
    Pool --> CC --> US_Net

    US_Net -->|response| CC --> Pool --> FC
    FC --> FB --> FA --> DFM --> AS --> Codec --> DS_Net
```

---

## 2. Component Map

```mermaid
mindmap
  root((source/common/http))
    Connection Management
      conn_manager_impl.h
      conn_manager_config.h
      conn_manager_utility.h
    Filter System
      filter_manager.h
      filter_chain_helper.h
      dependency_manager.h
    Codecs
      codec_client.h
      codec_helper.h
      codec_wrappers.h
      http1/
      http2/
      http3/
    Header System
      header_map_impl.h
      header_utility.h
      header_mutation.h
      headers.h
    Connection Pools
      conn_pool_base.h
      conn_pool_grid.h
      mixed_conn_pool.h
    Async Client
      async_client_impl.h
      null_route_impl.h
    Utilities
      utility.h
      path_utility.h
      hash_policy.h
      codes.h
      status.h
    HTTP/3 Properties
      http_server_properties_cache_impl.h
      http3_status_tracker_impl.h
```

---

## 3. End-to-End Request Flow

**This sequence diagram traces a complete HTTP request from client bytes to upstream response:**

**Phase 1: Connection & Protocol Setup (Steps 1-3):**
- Client sends raw TCP bytes (HTTP request)
- `ConnectionManagerImpl` receives bytes as network filter
- `ConnectionManagerUtility::autoCreateCodec()` examines first bytes:
  - HTTP/1.1: Looks for `GET`, `POST`, etc.
  - HTTP/2: Looks for connection preface (`PRI * HTTP/2.0`)
  - HTTP/3: Uses QUIC stream types
- Appropriate server codec is created and stored

**Phase 2: Stream Creation (Steps 4-6):**
- Codec parses request and calls `newStream()`
- `ConnectionManagerImpl` creates `ActiveStream` for this request
- `ActiveStream` creates `DownstreamFilterManager` for filter chain execution
- Request tracking begins (timing, metrics)

**Phase 3: Header Processing (Steps 7-13):**
- Codec calls `decodeHeaders()` with parsed headers
- `ConnectionManagerUtility` performs header mutations:
  - Add `x-forwarded-for`, `x-envoy-*` headers
  - Sanitize headers for security
  - Set request ID for tracing
- Filter chain begins: A → B → C → Router
- Filter B returns `StopIteration` (e.g., waiting for rate limit check)
- Eventually `continueDecoding()` is called to resume

**Phase 4: Routing & Upstream Selection (Steps 14-16):**
- Router filter matches route based on headers (path, host, method)
- Selects upstream cluster
- Applies load balancing to choose specific host
- Gets connection from pool (may create new connection)

**Phase 5: Upstream Request (Steps 17-18):**
- Request sent to upstream backend
- Connection pool manages connection lifecycle
- Request queued if no connection available

**Phase 6: Response Processing (Steps 19-26):**
- Upstream responds with headers
- Response flows through encoder filters (reverse order): Router → C → B → A
- Each encoder filter can modify response
- Codec encodes response back to wire format
- Bytes sent to client

**Key Behavioral Notes:**

**Async Filter Operations:**
- When filter returns `StopIteration`, request processing pauses
- Filter performs async work (auth check, rate limit, etc.)
- Filter calls `continueDecoding()` or `continueEncoding()` when ready
- Processing resumes from where it stopped

**Header Mutations:**
- Happen before filter chain (request) and after filter chain (response)
- Add operational headers: request ID, trace context, proxy info
- Remove internal headers before sending to client
- Normalize headers for consistency

**Connection Pooling Benefits:**
- Reuses connections → reduces latency and overhead
- HTTP/2 multiplexing → multiple requests on one connection
- Connection limits prevent resource exhaustion
- Health checking ensures connections are valid

```mermaid
sequenceDiagram
    autonumber
    participant Client
    participant CMI as ConnectionManagerImpl
    participant CMU as ConnectionManagerUtility
    participant Codec as ServerCodec
    participant AS as ActiveStream
    participant DFM as DownstreamFilterManager
    participant FA as Filter A (e.g. Auth)
    participant FB as Filter B (e.g. RateLimit)
    participant Router as Router Filter
    participant Pool as ConnPool
    participant Upstream

    Client->>CMI: TCP bytes
    CMI->>CMU: autoCreateCodec(bytes)
    CMU-->>CMI: Http1::ServerConnection or Http2::ServerConnection

    Codec->>CMI: newStream(response_encoder)
    CMI->>AS: create ActiveStream
    AS->>DFM: create DownstreamFilterManager

    Codec->>AS: decodeHeaders(request_headers, end_stream)
    AS->>CMU: mutateRequestHeaders()
    CMU-->>AS: mutated headers

    AS->>DFM: decodeHeaders(headers)
    DFM->>FA: decodeHeaders(headers)
    FA-->>DFM: Continue
    DFM->>FB: decodeHeaders(headers)
    FB-->>DFM: StopIteration (async check)
    Note over FB: Rate limit check in progress...
    FB->>DFM: continueDecoding()
    DFM->>Router: decodeHeaders(headers)

    Router->>Pool: newStream(decoder, callbacks)
    Pool-->>Router: Cancellable*

    Pool->>Upstream: HTTP request
    Upstream-->>Pool: HTTP response headers
    Pool->>Router: onPoolReady + encodeHeaders(response)

    Router->>DFM: encodeHeaders(response_headers)
    DFM->>FB: encodeHeaders(response_headers)
    FB-->>DFM: Continue
    DFM->>FA: encodeHeaders(response_headers)
    FA-->>DFM: Continue

    AS->>CMU: mutateResponseHeaders()
    CMU-->>AS: mutated response headers
    AS->>Codec: encodeHeaders(mutated_response)
    Codec->>Client: TCP bytes (response)
```

---

## 4. ConnectionManagerImpl Deep Dive

### Responsibilities

```mermaid
mindmap
  root((ConnectionManagerImpl))
    Protocol
      Codec creation via CMU
      H1 / H2 / H3 dispatch
      GOAWAY handling
    Stream Lifecycle
      ActiveStream per request
      Idle timeout enforcement
      Request header timeout
    Security
      XFF / trusted hop validation
      Flood protection
      Premature reset detection
    Overload
      OverloadManager integration
      MaxRequestsPerIoCycle
      Load shedding 503s
    Drain
      Drain decision integration
      Graceful connection drain
    Tracing
      Sampling decisions
      Tracing header injection
    Stats
      downstream_rq_*
      downstream_cx_*
```

### `ActiveStream` — The Request Object

```mermaid
classDiagram
    class ActiveStream {
        -state_: uint32_t bitfield
        -filter_manager_: DownstreamFilterManager
        -stream_info_: StreamInfoImpl
        -request_headers_: RequestHeaderMapPtr
        -response_headers_: ResponseHeaderMapPtr
        -cached_route_: RouteConstSharedPtr
        -access_log_flush_timer_: TimerPtr
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
        +encodeTrailers(trailers)
        +onStreamComplete()
        +resetStream(reset_code)
    }

    class RequestDecoder { <<interface>> }
    class FilterManagerCallbacks { <<interface>> }
    class DownstreamStreamFilterCallbacks { <<interface>> }
    class RouteCache { <<interface>> }

    RequestDecoder <|-- ActiveStream
    FilterManagerCallbacks <|-- ActiveStream
    DownstreamStreamFilterCallbacks <|-- ActiveStream
    RouteCache <|-- ActiveStream
```

### Connection Manager State Machine

```mermaid
stateDiagram-v2
    [*] --> Init : onNewConnection()
    Init --> Running : onData() + codec created
    Running --> DrainInitiated : drain_close_ triggered
    DrainInitiated --> Running : new requests still possible (H2)
    Running --> Closing : onEvent(RemoteClose/LocalClose)
    DrainInitiated --> Closing : all streams done
    Closing --> [*] : connection destroyed
    Running --> Running : active streams processed
```

---

## 5. FilterManager and the Filter Chain

### Filter Registration

Filters are registered at configuration load time by `FilterChainHelper`. At runtime, `FilterManager` holds the instantiated filter chain:

```mermaid
flowchart TD
    Config["HCM Config\n(static + ECDS filters)"] --> FCH["FilterChainHelper\n(processes factories)"]
    FCH --> FL["FilterFactoriesList\n(ordered list of factory functions)"]
    FL -->|per request| FM["FilterManager\n(instantiates filters)"]
    FM --> DFs["StreamDecoderFilters\n[A, B, Router]"]
    FM --> EFs["StreamEncoderFilters\n[Router, B, A] (reversed)"]
```

### Iteration State Machine

```mermaid
stateDiagram-v2
    [*] --> Continue
    Continue --> StopIteration : filter returns StopIteration
    Continue --> StopAllBuffer : returns StopAllIterationAndBuffer
    Continue --> StopAllWatermark : returns StopAllIterationAndWatermark
    StopIteration --> Continue : continueDecoding/Encoding()
    StopAllBuffer --> Continue : continueDecoding/Encoding()
    StopAllWatermark --> Continue : continueDecoding/Encoding()
    Continue --> [*] : reached end of chain
```

### Buffering During Stop

When a filter returns `StopAllIterationAndBuffer`, subsequent data is accumulated in the filter's `buffered_body_`:

```
Request Data Flow with StopAllBuffer:
─────────────────────────────────────
Filter A: decodeHeaders() → Continue
Filter B: decodeHeaders() → StopAllIterationAndBuffer
  [data chunks arrive]
  FilterManager: append to B.buffered_body_
  [B finishes async work]
  B: continueDecoding()
  FilterManager: replay buffered_body_ to subsequent filters
Filter C (Router): decodeHeaders() + decodeData(buffered_body_)
```

### Local Reply Shortcut

```mermaid
flowchart TD
    FA["Filter A calls\nsendLocalReply(403, 'Forbidden')"] --> LR["FilterManager::sendLocalReply()"]
    LR --> B{Response started?}
    B -->|No| C["Create synthetic 403 headers"]
    B -->|Yes| D["Reset stream"]
    C --> E["Run encoder filters in reverse\nC → B → A"]
    E --> F["Encode to downstream codec"]
    F --> G["Record LocalReplyOwnerObject\nin FilterState"]
```

---

## 6. FilterChainHelper — Building the Chain

```mermaid
flowchart TD
    subgraph "Config Load Time"
        ECDS["ECDS (dynamic filter config)"]
        Static["Static filter_chains config"]
        FCH["FilterChainHelper::processFilters()"]
        FCH2["FilterChainUtility::createFilterChainForFactories()"]
    end

    subgraph "Request Time"
        FM["FilterManager"]
        MissingCfg["MissingConfigFilter\n(returns 500 if ECDS not yet loaded)"]
    end

    Static --> FCH
    ECDS --> FCH
    FCH --> FCH2
    FCH2 -->|builds| FactoryList["FilterFactoriesList"]
    FactoryList -->|per request| FM
    FM -->|if ECDS missing| MissingCfg
```

**`FilterChainHelper<FilterCtx, Factory>`** is a template class parameterized over:
- `FilterCtx` — the filter context type (e.g., `Http::FilterChainFactoryCallbacks`)
- `Factory` — the factory type (e.g., `NamedHttpFilterConfigFactory`)

---

## 7. Key Design Patterns

### Pattern 1: Per-Filter Iteration State

Each `ActiveStreamFilterBase` tracks its own `IterationState`, allowing filters to independently pause and resume without a global lock or callback stack.

### Pattern 2: Deferred Deletion

All stream objects (`ActiveStream`, `AsyncStreamImpl`, `WrapperCallbacks`) implement `Event::DeferredDeletable`. Destruction is deferred to the next event loop iteration to prevent use-after-free when a callback deletes an object that's in the current call stack.

```mermaid
sequenceDiagram
    participant Filter
    participant AS as ActiveStream
    participant Dispatcher

    Filter->>AS: resetStream()
    AS->>AS: mark for deletion
    AS->>Dispatcher: deferredDelete(this)
    Note over AS: Object still alive for remainder of this I/O event
    Dispatcher->>AS: ~ActiveStream() [next event loop iteration]
```

### Pattern 3: absl::Status in Codecs

All codec operations return `absl::Status` instead of throwing exceptions. This keeps the codec paths exception-free and allows precise error propagation:

```
Codec::dispatch(data) → absl::Status
  OK              → continue
  CodecProtocolError → close connection with protocol error stats
  PrematureResponseError → reset stream
  InboundFramesWithEmptyPayload → flood protection
```

### Pattern 4: Thread-Local Route Caches

`ActiveStream` caches the resolved route in `cached_route_` and refreshes it only when `clearRouteCache()` is called. Route resolution is lock-free since each worker thread has its own snapshot of the route table via thread-local slots.

---

## Navigation

| Part | Topics |
|------|--------|
| **Part 1 (this file)** | Architecture, Request Pipeline, ConnectionManager, FilterSystem |
| [Part 2](OVERVIEW_PART2_codecs_and_pools.md) | Codecs (H1/H2/H3), Connection Pools, Protocol Details |
| [Part 3](OVERVIEW_PART3_headers_and_utilities.md) | Header System, Utilities, Path Normalization |
| [Part 4](OVERVIEW_PART4_async_and_advanced.md) | Async Client, HTTP/3, Server Properties, Advanced Topics |
