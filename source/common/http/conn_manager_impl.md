# ConnectionManagerImpl

**File:** `source/common/http/conn_manager_impl.h` / `.cc`  
**Size:** ~34 KB header, ~118 KB implementation  
**Namespace:** `Envoy::Http`

## Overview

`ConnectionManagerImpl` is the central hub of Envoy's HTTP processing pipeline. It is a `Network::ReadFilter` installed on every HTTP listener connection and is responsible for:

- Detecting and instantiating the appropriate HTTP codec (H1/H2/H3)
- Creating and lifecycle-managing one `ActiveStream` per request/push
- Routing decoded data through the downstream filter chain
- Enforcing connection-level policies (drain, overload, idle timeouts, flood protection)

## Class Hierarchy

```mermaid
classDiagram
    class ConnectionManagerImpl {
        +onData(data, end_stream)
        +onNewConnection()
        +newStream(encoder)
        +onGoAway(error_code)
        +onEvent(event)
        -codec_: ServerConnectionPtr
        -streams_: list~ActiveStream~
        -config_: ConnectionManagerConfigSharedPtr
    }

    class NetworkReadFilter["Network::ReadFilter"] {
        <<interface>>
        +onData()
        +onNewConnection()
        +initializeReadFilterCallbacks()
    }

    class ServerConnectionCallbacks {
        <<interface>>
        +newStream()
    }

    class NetworkConnectionCallbacks["Network::ConnectionCallbacks"] {
        <<interface>>
        +onEvent()
        +onAboveWriteBufferHighWatermark()
        +onBelowWriteBufferLowWatermark()
    }

    class HttpApiListener["Http::ApiListener"] {
        <<interface>>
    }

    NetworkReadFilter <|-- ConnectionManagerImpl
    ServerConnectionCallbacks <|-- ConnectionManagerImpl
    NetworkConnectionCallbacks <|-- ConnectionManagerImpl
    HttpApiListener <|-- ConnectionManagerImpl
```

## Key Inner Types

### `ActiveStream`

Each HTTP request/response pair is represented by an `ActiveStream`, a private nested struct inside `ConnectionManagerImpl`. It is the single object that bridges the downstream network layer, the filter chain, and the upstream router.

```mermaid
classDiagram
    class ActiveStream {
        +state_: StreamState
        +filter_manager_: DownstreamFilterManager
        +stream_info_: StreamInfo
        +request_headers_: RequestHeaderMapPtr
        +response_headers_: ResponseHeaderMapPtr
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
        +encodeTrailers(trailers)
    }

    class RequestDecoder {
        <<interface>>
    }

    class FilterManagerCallbacks {
        <<interface>>
    }

    class DownstreamStreamFilterCallbacks {
        <<interface>>
    }

    class RouteCache {
        <<interface>>
    }

    class TracingConfig["Tracing::Config"] {
        <<interface>>
    }

    RequestDecoder <|-- ActiveStream
    FilterManagerCallbacks <|-- ActiveStream
    DownstreamStreamFilterCallbacks <|-- ActiveStream
    RouteCache <|-- ActiveStream
    TracingConfig <|-- ActiveStream
```

## Request Lifecycle

```mermaid
sequenceDiagram
    participant Net as Network Layer
    participant CM as ConnectionManagerImpl
    participant Codec as ServerCodec (H1/H2/H3)
    participant AS as ActiveStream
    participant FM as DownstreamFilterManager
    participant Router as Router Filter

    Net->>CM: onData(bytes)
    CM->>CM: autoCreateCodec() if needed
    CM->>Codec: dispatch(bytes)
    Codec->>CM: newStream(response_encoder)
    CM->>AS: create ActiveStream
    AS->>FM: create DownstreamFilterManager
    Codec->>AS: decodeHeaders(headers)
    AS->>FM: decodeHeaders(headers)
    FM->>FM: iterate decoder filter chain A→B→C
    FM->>Router: decodeHeaders(headers)
    Router->>Net: upstream request
    Net-->>Router: upstream response headers
    Router->>FM: encode response headers
    FM->>FM: iterate encoder filter chain C→B→A
    FM->>AS: encodeHeaders(headers)
    AS->>Codec: encodeHeaders(response)
    Codec->>Net: write bytes
```

## Connection State Machine

```mermaid
stateDiagram-v2
    [*] --> Idle : onNewConnection()
    Idle --> CodecCreated : onData() / autoCreateCodec()
    CodecCreated --> StreamActive : newStream()
    StreamActive --> StreamActive : multiple concurrent streams (H2/H3)
    StreamActive --> Draining : drain_close_ signal
    StreamActive --> Idle : stream complete
    Draining --> [*] : all streams complete
    CodecCreated --> [*] : codec error / protocol violation
    StreamActive --> [*] : connection close event
```

## Codec Auto-Detection

```mermaid
flowchart TD
    A[Raw bytes arrive] --> B{Is H3 via QUIC?}
    B -->|Yes| C[Create Http3::ServerConnection]
    B -->|No| D{Magic bytes = PRI *}
    D -->|Yes, HTTP/2 preface| E[Create Http2::ServerConnection]
    D -->|No| F{ALPN = h2?}
    F -->|Yes| E
    F -->|No| G[Create Http1::ServerConnection]
```

## Flood Protection

`ConnectionManagerImpl` tracks several counters to detect and mitigate protocol-level flooding:

| Counter | Threshold Source | Action |
|---------|-----------------|--------|
| `PrematureResetTotalStreamCountKey` | Runtime flag | Close connection if too many premature resets |
| `MaxRequestsPerIoCycle` | Runtime flag | Limit streams processed per I/O event |
| Downstream overload | `OverloadManager` | Shed load via 503 or connection close |
| Idle stream timeout | Config | Reset idle streams |

## Stats Generated

```mermaid
mindmap
  root((ConnectionManager Stats))
    Requests
      downstream_rq_total
      downstream_rq_active
      downstream_rq_1xx / 2xx / 3xx / 4xx / 5xx
      downstream_rq_time
    Connections
      downstream_cx_total
      downstream_cx_active
      downstream_cx_protocol_error
      downstream_cx_destroy_*
    Upgrades
      downstream_cx_upgrades_total
    Tracing
      tracing_random_sampling
      tracing_service_forced
      tracing_client_enabled
```

## Key Static Helpers

| Method | Purpose |
|--------|---------|
| `generateStats(prefix, scope)` | Creates all `ConnectionManagerStats` counters |
| `generateTracingStats(prefix, scope)` | Creates tracing-specific counters |
| `chargeTracingStats(reason, stats)` | Increments the correct tracing counter for a sampling reason |
| `generateListenerStats(prefix, scope)` | Creates listener-scoped downstream stats |
| `continueHeader()` | Returns a cached `100 Continue` response header map |

## Thread Safety

`ConnectionManagerImpl` runs entirely on a single `Event::Dispatcher` thread (the worker thread). All callbacks (`onData`, `onEvent`, `newStream`) are dispatched on that thread. Cross-thread communication (e.g., with the main thread for RDS updates) uses thread-local slot caches.

## Key Configuration Points (`ConnectionManagerConfig`)

| Config Field | Effect |
|-------------|--------|
| `http1Settings` | H1 codec options (header case, allow chunked length) |
| `http2Options` | H2 codec options (SETTINGS, max concurrent streams) |
| `http3Options` | H3 / QUIC codec options |
| `streamIdleTimeout` | Idle timer per stream |
| `requestHeadersTimeout` | Timer from connection accept to headers received |
| `maxRequestHeadersKb` | Maximum size of request headers |
| `localReply` | Custom local reply formatting |
| `tracingConfig` | Tracer, sampling decisions |
| `routeConfigProvider` | RDS or static route table |
