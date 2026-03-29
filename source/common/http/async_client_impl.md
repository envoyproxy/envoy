# AsyncClientImpl

**File:** `source/common/http/async_client_impl.h` / `.cc`  
**Size:** ~18 KB header, ~21 KB implementation  
**Namespace:** `Envoy::Http`

## Overview

`AsyncClientImpl` is Envoy's in-process HTTP client, used for internal requests (e.g., rate-limit service calls, auth calls, health checks). It implements `Http::AsyncClient` and creates `AsyncStreamImpl` objects that plug into the full router/filter chain without going through the network layer on the downstream side.

## Class Hierarchy

```mermaid
classDiagram
    class AsyncClientImpl {
        +send(request, callbacks, options): Request*
        +startRequest(callbacks, options): RequestPtr
        +start(callbacks, options): Stream*
        -active_streams_: list~AsyncStreamImpl~
        -cluster_: ClusterInfoConstSharedPtr
        -config_: ConnectionManagerConfig
    }

    class AsyncClient {
        <<interface>>
        +send()
        +start()
    }

    class AsyncStreamImpl {
        +sendHeaders(headers, end_stream)
        +sendData(data, end_stream)
        +sendTrailers(trailers)
        +reset()
        -router_: Router::RouterFilter
        -null_route_impl_: NullRouteImpl
        -request_info_: StreamInfo
    }

    class AsyncRequestImpl {
        +cancel()
        -response_buffer_: Buffer
    }

    class StreamDecoderFilterCallbacks {
        <<interface>>
    }

    AsyncClient <|-- AsyncClientImpl
    AsyncClientImpl *-- AsyncStreamImpl : active_streams_
    AsyncStreamImpl <|-- AsyncRequestImpl
    StreamDecoderFilterCallbacks <|-- AsyncStreamImpl
```

## Architecture: How AsyncClient Bypasses the Network

`AsyncStreamImpl` masquerades as a `StreamDecoderFilterCallbacks` — the same interface that real HTTP filters implement — and plugs directly into the `Router::RouterFilter`. This means async client requests go through real routing, load balancing, retries, and shadowing, but bypass the downstream network layer.

```mermaid
flowchart TD
    subgraph InternalCaller["Internal Caller (e.g., RateLimit filter)"]
        Caller["AsyncClient::send(request, callbacks)"]
    end

    subgraph AsyncImpl["AsyncClientImpl / AsyncStreamImpl"]
        AS["AsyncStreamImpl\n(acts as StreamDecoderFilterCallbacks)"]
        NR["NullRouteImpl\n(fake route config)"]
    end

    subgraph RouterLayer["Router::RouterFilter"]
        RF["RouterFilter\n(real routing logic)"]
    end

    subgraph UpstreamInfra["Upstream Infrastructure"]
        Pool["Connection Pool\n(H1/H2/H3)"]
        CC["CodecClient"]
        Upstream["Real Upstream Service"]
    end

    Caller -->|sendHeaders| AS
    AS -->|decodeHeaders| RF
    RF -->|uses| NR
    RF -->|newStream| Pool
    Pool --> CC
    CC --> Upstream
    Upstream -->|response| CC
    CC -->|decodeHeaders| RF
    RF -->|encodeHeaders| AS
    AS -->|onHeaders| Caller
```

## Send Request Flow

```mermaid
sequenceDiagram
    participant Caller as Internal Caller
    participant ACI as AsyncClientImpl
    participant ASI as AsyncStreamImpl
    participant Router as Router::RouterFilter
    participant Pool as ConnPool
    participant Upstream

    Caller->>ACI: send(request_message, callbacks, options)
    ACI->>ASI: create AsyncRequestImpl
    ASI->>Router: decodeHeaders(request_headers)
    Router->>Pool: newStream(response_decoder, pool_callbacks)
    Pool-->>Router: Cancellable* (in-progress)
    Pool->>Upstream: TCP connect + encode request

    Upstream-->>Pool: response bytes
    Pool->>Router: onPoolReady + response headers
    Router->>ASI: encodeHeaders(response_headers)
    Router->>ASI: encodeData(body)
    Router->>ASI: encodeComplete()
    ASI->>Caller.callbacks_: onSuccess(response_message)

    alt Request cancelled
        Caller->>ASI: cancel()
        ASI->>Router: reset()
        Router->>Pool: cancel()
    end
```

## `AsyncStreamImpl` as Filter Callbacks

`AsyncStreamImpl` implements `StreamDecoderFilterCallbacks` to satisfy the `Router::RouterFilter`'s expectations. It provides stub/minimal implementations for most callbacks and real implementations for the ones the router actually uses:

| Callback | Implementation |
|----------|---------------|
| `streamInfo()` | Returns real `StreamInfo` for the async request |
| `dispatcher()` | Returns the cluster manager dispatcher |
| `connection()` | Returns `nullopt` (no real downstream connection) |
| `route()` | Returns `NullRouteImpl` (fake route) |
| `sendLocalReply(...)` | Converts to error response for the caller |
| `continueDecoding()` | Triggers router to proceed |
| `addDecodedData()` | Buffers decoded data |
| `encodeHeaders()` | Forwards to `AsyncClient::Callbacks::onHeaders()` |
| `encodeData()` | Accumulates or streams to caller |
| `encodeTrailers()` | Forwards to caller |

## `NullRouteImpl` — Fake Route for AsyncClient

Since async client requests don't have a real downstream connection with a route configuration, `NullRouteImpl` provides stub implementations for all `Router::Route`, `Router::RouteEntry`, and `Router::VirtualHost` interfaces.

```mermaid
classDiagram
    class NullRouteImpl {
        +virtualHost(): NullVirtualHost
        +routeEntry(): NullRouteEntry
        +decorator(): nullptr
        +metadata(): empty
    }

    class NullRouteEntry {
        +clusterName(): string  (set at construction)
        +timeout(): options.timeout
        +retryPolicy(): NullRetryPolicy
        +hedgePolicy(): NullHedgePolicy
        +rateLimitPolicy(): NullRateLimitPolicy
    }

    class NullVirtualHost {
        +name(): "async-client"
        +rateLimitPolicy(): NullRateLimitPolicy
    }

    NullRouteImpl *-- NullRouteEntry
    NullRouteImpl *-- NullVirtualHost
```

## Request vs. Stream API

| API | Class | Use Case |
|-----|-------|---------|
| `send(message, callbacks, options)` | `AsyncRequestImpl` | One-shot request/response (buffered) |
| `start(callbacks, options)` | `AsyncStreamImpl` | Streaming bidirectional (e.g., gRPC) |
| `startRequest(callbacks, options)` | `AsyncStreamImpl` | Streaming request (chunked upload) |

### Buffering Limits

| Direction | Default Limit |
|-----------|--------------|
| Request body (buffered before upstream ready) | 64 KB |
| Response body (buffered for `onSuccess`) | 32 MB |

## Retry and Shadow Support

`AsyncStreamImpl` uses the same `Router::RouterFilter` as normal requests, so it automatically supports:

- **Retries**: Configured via `AsyncClient::RequestOptions::retry_policy`
- **Shadowing**: Configured via `AsyncClient::RequestOptions::shadow_policy`
- **Timeouts**: `AsyncClient::RequestOptions::timeout`
- **Hash policy**: `AsyncClient::RequestOptions::hash_policy`

## Lifecycle & Cleanup

```mermaid
stateDiagram-v2
    [*] --> Active : AsyncStreamImpl created
    Active --> HeadersSent : sendHeaders(end_stream=false)
    Active --> Complete : sendHeaders(end_stream=true)
    HeadersSent --> DataSent : sendData(end_stream=false)
    HeadersSent --> Complete : sendData(end_stream=true)
    DataSent --> Complete : sendTrailers()
    Complete --> AwaitingResponse : router upstream in flight
    AwaitingResponse --> Done : encodeComplete() or local reply
    Active --> Cancelled : cancel() / reset()
    Cancelled --> [*] : DeferredDelete from active_streams_
    Done --> [*] : DeferredDelete from active_streams_
```

## Thread Safety

`AsyncClientImpl` is owned by the cluster manager and is used only on a single worker thread's event loop. There is no locking. All callbacks (`onSuccess`, `onFailure`, `onHeaders`, `onData`) are invoked synchronously on the same thread.
