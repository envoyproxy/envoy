# Connection Pool Base & Connectivity Grid

**Files:**  
- `source/common/http/conn_pool_base.h` / `.cc` (~11 KB / ~9.9 KB)  
- `source/common/http/conn_pool_grid.h` / `.cc` (~14 KB / ~25 KB)  
- `source/common/http/mixed_conn_pool.h` / `.cc` (~1.9 KB / ~4.6 KB)  
**Namespace:** `Envoy::Http`

---

## Overview

The connection pool stack manages upstream HTTP connections across all three protocol versions. `HttpConnPoolImplBase` is the shared base; protocol-specific subclasses (`http1`, `http2`, `http3`) extend it. `ConnectivityGrid` adds HTTP/3-first with automatic TCP fallback and IPv4/IPv6 happy eyeballs.

## Pool Hierarchy

```mermaid
classDiagram
    class ConnPoolImplBase["ConnectionPool::ConnPoolImplBase"] {
        <<abstract>>
        +newStream()
        +drainConnections()
    }

    class HttpConnPoolImplBase {
        +newStream(decoder, callbacks): Cancellable*
        -createActiveClient(): ActiveClientPtr
        -pending_streams_: list~HttpPendingStream~
    }

    class Http1ConnPoolImpl {
        -maxRequestsPerConnection(): 1
    }

    class Http2ConnPoolImpl {
        -maxRequestsPerConnection(): INF
    }

    class Http3ConnPoolImpl {
        -quiche_capacity_: uint32_t
    }

    class HttpConnPoolImplMixed {
        +newStream()
        -h1_pool_: Http1ConnPoolImpl
        -h2_pool_: Http2ConnPoolImpl
        -alpn_: AlpnResult
    }

    class ConnectivityGrid {
        +newStream()
        -pools_: vector~PoolWithHandle~
        -next_attempt_timer_: TimerPtr
        -http3_status_tracker_: Http3StatusTrackerImpl
    }

    ConnPoolImplBase <|-- HttpConnPoolImplBase
    HttpConnPoolImplBase <|-- Http1ConnPoolImpl
    HttpConnPoolImplBase <|-- Http2ConnPoolImpl
    HttpConnPoolImplBase <|-- Http3ConnPoolImpl
    HttpConnPoolImplBase <|-- HttpConnPoolImplMixed
    ConnectivityGrid *-- Http3ConnPoolImpl : primary
    ConnectivityGrid *-- Http2ConnPoolImpl : fallback
    ConnectivityGrid *-- HttpConnPoolImplMixed : fallback (ALPN)
```

## `HttpConnPoolImplBase` — Core Pool Logic

### Stream Lifecycle

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant Pool as HttpConnPoolImplBase
    participant AC as ActiveClient
    participant CC as CodecClient

    Router->>Pool: newStream(decoder, callbacks)
    Pool->>Pool: create HttpPendingStream
    alt Connection available
        Pool->>AC: attachRequestToClient(pending_stream)
        AC->>CC: newStream(pending_stream.decoder)
        CC-->>AC: RequestEncoder&
        AC-->>Pool: stream attached
        Pool-->>Router: Cancellable* (for cancellation)
    else No connection available
        Pool->>Pool: queue pending_stream in pending_streams_
        Pool->>AC: createActiveClient() (new upstream connection)
        Note over AC: TCP connect in progress
        AC->>CC: CodecClient constructed
        CC->>AC: onEvent(Connected)
        AC->>Pool: onUpstreamReady()
        Pool->>AC: attachRequestToClient(pending_stream)
    end
```

### `HttpPendingStream`

```mermaid
classDiagram
    class HttpPendingStream {
        +decoder_: ResponseDecoder*
        +callbacks_: ConnectionPool::Callbacks*
        +priority_: RequestPriority
    }
    class PendingStream["ConnectionPool::PendingStream"] {
        <<base>>
    }
    PendingStream <|-- HttpPendingStream
```

### `ActiveClient` in HTTP Pool

```mermaid
classDiagram
    class ActiveClient["HttpConnPoolImplBase::ActiveClient"] {
        +codec_client_: CodecClientPtr
        +numActiveRequests(): size_t
        +numPendingRequests(): size_t
        +onEvent(event)
        +onStreamDestroy()
        +onStreamReset(reason)
    }
    class ConnPoolActiveClient["ConnectionPool::ActiveClient"] {
        <<base>>
    }
    class CodecClientCallbacks {
        <<interface>>
    }
    ConnPoolActiveClient <|-- ActiveClient
    CodecClientCallbacks <|-- ActiveClient
    ActiveClient *-- CodecClientPtr
```

---

## `ConnectivityGrid` — HTTP/3-First with Fallback

`ConnectivityGrid` implements the "happy eyeballs" approach: try HTTP/3 first; start a TCP fallback after a timeout; use whichever succeeds first.

### Pool Architecture

```mermaid
flowchart TD
    subgraph Grid["ConnectivityGrid"]
        H3["HTTP/3 Pool\n(QUIC, IPv6)"]
        H3v4["HTTP/3 Pool\n(QUIC, IPv4)"]
        H2["HTTP/2/1 Pool\n(TCP, mixed ALPN)"]
    end

    NewStream["newStream()"] --> Grid
    Grid -->|attempt 1| H3
    Grid -->|attempt 2 (after timer)| H2
    H3 -->|success| Done["Stream established"]
    H2 -->|success| Done
    H3 -->|failure| H2
```

### `WrapperCallbacks` — Attempt Orchestration

`ConnectivityGrid` wraps the caller's `ConnectionPool::Callbacks` with `WrapperCallbacks` that intercepts success/failure and manages cancellation across simultaneous attempts:

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant Grid as ConnectivityGrid
    participant WC as WrapperCallbacks
    participant H3 as HTTP/3 Pool
    participant H2 as HTTP/2 Pool

    Router->>Grid: newStream(decoder, caller_callbacks)
    Grid->>WC: create WrapperCallbacks(caller_callbacks)
    Grid->>H3: newStream(decoder, WC)
    Grid->>Grid: start next_attempt_timer_ (300ms)

    alt HTTP/3 succeeds fast
        H3-->>WC: onPoolReady(encoder, host, proto)
        WC->>Grid: cancel H2 attempt (if started)
        WC->>Router.callbacks_: onPoolReady(encoder, host, proto)
    else Timer fires
        Grid->>H2: newStream(decoder, WC)
        H2-->>WC: onPoolReady(encoder, host, proto)
        WC->>Grid: cancel H3 attempt
        WC->>Router.callbacks_: onPoolReady(encoder, host, proto)
    else HTTP/3 fails
        H3-->>WC: onPoolFailure(reason, host)
        WC->>H2: already started or start now
        H2-->>WC: onPoolReady / onPoolFailure
    end
```

### HTTP/3 Status Tracking

`Http3StatusTrackerImpl` maintains per-origin HTTP/3 health with exponential backoff:

```mermaid
stateDiagram-v2
    [*] --> Pending : first attempt
    Pending --> Confirmed : successful H3 stream
    Pending --> Broken : H3 connection failed
    Confirmed --> Confirmed : normal H3 streams
    Confirmed --> Broken : H3 connection error
    Broken --> Pending : backoff timer expires (exp. backoff: 1s→5min)
    Broken --> Broken : within backoff window → skip H3
```

### Backoff Timing

| Failure Count | Backoff Duration |
|---------------|-----------------|
| 1 | 1 second |
| 2 | 2 seconds |
| 3 | 4 seconds |
| 4 | 8 seconds |
| ... | 2^n seconds |
| Max | 5 minutes |

---

## `HttpConnPoolImplMixed` — ALPN-Based H1/H2 Selection

```mermaid
sequenceDiagram
    participant Caller
    participant Mixed as HttpConnPoolImplMixed
    participant TLS as TLS Handshake
    participant H1Pool as Http1ConnPoolImpl
    participant H2Pool as Http2ConnPoolImpl

    Caller->>Mixed: newStream()
    Mixed->>TLS: connect (ALPN = ["h2", "http/1.1"])
    TLS-->>Mixed: ALPN result
    alt ALPN = "h2"
        Mixed->>H2Pool: attach stream
    else ALPN = "http/1.1" or no ALPN
        Mixed->>H1Pool: attach stream
    end
```

---

## Protocol-Specific Pool Capacities

| Pool | Max Streams / Connection | Connection Reuse |
|------|--------------------------|-----------------|
| `Http1ConnPoolImpl` | 1 (sequential) | Yes (keep-alive) |
| `Http2ConnPoolImpl` | `max_concurrent_streams` (default 2^31-1) | Yes (multiplexed) |
| `Http3ConnPoolImpl` | `quiche_capacity_` (dynamic) | Yes (multiplexed) |

## Drain and Shutdown

```mermaid
flowchart TD
    A[drainConnections(reason)] --> B{reason = DrainExistingConnections?}
    B -->|Yes| C[Mark all ActiveClients as draining\nNo new streams accepted]
    B -->|No - EvictIdleConnections| D[Close idle connections immediately]
    C --> E{All requests complete?}
    E -->|Yes| F[Close connection]
    E -->|No| G[Wait for in-flight streams]
    G --> E
```

## Related Files

| File | Purpose |
|------|---------|
| `http1/conn_pool.h` | HTTP/1.1 specific pool implementation |
| `http2/conn_pool.h` | HTTP/2 specific pool implementation |
| `http3/conn_pool.h` | HTTP/3 (QUIC) specific pool implementation |
| `http3_status_tracker_impl.h` | HTTP/3 broken/confirmed state machine |
| `http_server_properties_cache_impl.h` | Alt-Svc / QUIC RTT cache used by grid |
| `codec_client.h` | The upstream client each `ActiveClient` wraps |
