# Part 7: HTTP Connection Manager (HCM) as a Network Filter

## Overview

The HTTP Connection Manager (`ConnectionManagerImpl`) is the most important class in Envoy's HTTP processing. It acts as a **terminal network read filter**, bridging Layer 4 (raw bytes on a connection) to Layer 7 (HTTP requests and responses). Every HTTP request Envoy handles passes through this class.

## HCM in the Filter Stack

```mermaid
graph TB
    subgraph "Network Filter Chain"
        NF1["RBAC Filter<br/>(ReadFilter)"]
        NF2["Rate Limit Filter<br/>(ReadFilter)"]
        HCM["HTTP Connection Manager<br/>(ReadFilter — TERMINAL)"]
    end
    
    subgraph "HTTP Layer (inside HCM)"
        Codec["HTTP Codec<br/>(HTTP/1, HTTP/2, HTTP/3)"]
        S1["ActiveStream (Request 1)"]
        S2["ActiveStream (Request 2)"]
        S3["ActiveStream (Request N)"]
    end
    
    NF1 -->|"onData()"| NF2
    NF2 -->|"onData()"| HCM
    HCM --> Codec
    Codec --> S1
    Codec --> S2
    Codec --> S3
    
    style HCM fill:#c8e6c9,stroke:#2e7d32,stroke-width:3px
```

## HCM Class Hierarchy

```mermaid
classDiagram
    class ConnectionManagerImpl {
        +onData(buffer, end_stream) FilterStatus
        +onNewConnection() FilterStatus
        +newStream(encoder) RequestDecoder
        -codec_ : ServerConnectionPtr
        -streams_ : LinkedList~ActiveStream~
        -config_ : ConnectionManagerConfig
        -read_callbacks_ : ReadFilterCallbacks
    }
    class NetworkReadFilter {
        <<interface>>
        +onData(buffer, end_stream) FilterStatus
        +onNewConnection() FilterStatus
    }
    class ServerConnectionCallbacks {
        <<interface>>
        +newStream(encoder) RequestDecoder
    }
    class ConnectionCallbacks {
        <<interface>>
        +onEvent(ConnectionEvent)
        +onAboveWriteBufferHighWatermark()
        +onBelowWriteBufferLowWatermark()
    }

    ConnectionManagerImpl ..|> NetworkReadFilter
    ConnectionManagerImpl ..|> ServerConnectionCallbacks
    ConnectionManagerImpl ..|> ConnectionCallbacks
```

**Location:** `source/common/http/conn_manager_impl.h` (line 59)

HCM implements three interfaces simultaneously:
- **`Network::ReadFilter`** — receives raw bytes from the connection
- **`ServerConnectionCallbacks`** — receives stream events from the HTTP codec
- **`Network::ConnectionCallbacks`** — handles connection-level events

## HCM Creation

HCM is created as a network filter by the `HttpConnectionManagerFilterConfigFactory`:

```mermaid
sequenceDiagram
    participant Config as HCM Config Factory
    participant FM as Network FilterManager
    participant HCM as ConnectionManagerImpl

    Note over Config: source/extensions/filters/network/<br/>http_connection_manager/config.cc

    Config->>Config: createFilterFactoryFromProto()
    Config->>Config: Create HttpConnectionManagerConfig (shared)
    Config->>FM: addReadFilter(new ConnectionManagerImpl(config, ...))
    FM->>HCM: initializeReadFilterCallbacks(callbacks)
    Note over HCM: Stores read_callbacks_ (connection ref)
```

```
File: source/extensions/filters/network/http_connection_manager/config.cc

The factory creates HttpConnectionManagerConfig at config time (shared across connections),
then for each new connection, creates a ConnectionManagerImpl instance.
```

## HCM Initialization

### initializeReadFilterCallbacks

When the HCM filter is added to a connection, it receives callbacks:

```
File: source/common/http/conn_manager_impl.cc (lines 122-156)

initializeReadFilterCallbacks(callbacks):
    1. Store read_callbacks_ (connection + dispatcher reference)
    2. Register as connection callback (onEvent, watermarks)
    3. Set up overload actions
    4. Initialize drain timer, idle timer
    5. Set up stats (e.g., downstream_cx_active)
```

### onNewConnection

Called after `initializeReadFilters()`. For HTTP/1 and HTTP/2, this is a no-op. For HTTP/3 (QUIC), the protocol is known at connection time, so the codec is created here:

```mermaid
flowchart TD
    A["onNewConnection()"] --> B{Protocol known?}
    B -->|"HTTP/3 (QUIC)"| C["createCodec(empty_buffer)"]
    B -->|"HTTP/1, HTTP/2"| D["Return Continue<br/>(wait for onData)"]
    C --> E["Return Continue"]
```

## The Core Loop: onData()

`onData()` is where raw HTTP bytes enter the HTTP layer:

```mermaid
flowchart TD
    A["onData(buffer, end_stream)"] --> B{codec_ exists?}
    B -->|No| C["createCodec(buffer)"]
    B -->|Yes| D["codec_->dispatch(buffer)"]
    C --> D
    D --> E{Dispatch result?}
    E -->|OK| F{Buffer drained?}
    E -->|Error| G["Send error response<br/>(400, 431, etc.)"]
    F -->|Yes| H["Return StopIteration"]
    F -->|No| I["Error: codec didn't consume all bytes"]
    H --> J["Wait for next onData() call"]
    
    style D fill:#fff9c4
```

```
File: source/common/http/conn_manager_impl.cc (lines 415-467)

onData(data, end_stream):
    1. If no codec → createCodec(data)
    2. Try: codec_->dispatch(data)
       - Codec parses HTTP frames
       - Calls back into HCM via ServerConnectionCallbacks
    3. Catch codec errors → sendLocalReply(400/431/etc.)
    4. Return StopIteration (HCM consumes all data)
```

### Why StopIteration?

HCM always returns `FilterStatus::StopIteration` because it **consumes** all data from the buffer. The HTTP codec parses the bytes into structured HTTP messages — there's nothing left for any subsequent network filter.

## Codec Creation

```mermaid
flowchart TD
    A["createCodec(data)"] --> B["config_->createCodec(connection, data, *this, overload)"]
    B --> C{codec_type_}
    C -->|HTTP1| D["new Http1::ServerConnectionImpl"]
    C -->|HTTP2| E["new Http2::ServerConnectionImpl"]
    C -->|HTTP3| F["createQuicHttpServerConnectionImpl"]
    C -->|AUTO| G["ConnectionManagerUtility::autoCreateCodec()"]
    G --> H{Peek at first byte}
    H -->|"PRI * HTTP/2"| E
    H -->|"GET / HTTP/1"| D
```

```
File: source/extensions/filters/network/http_connection_manager/config.cc (lines 537-556)

createCodec(connection, data, callbacks, overload):
    switch(codec_type_):
        HTTP1 → Http1::ServerConnectionImpl
        HTTP2 → Http2::ServerConnectionImpl  
        HTTP3 → QUIC server connection
        AUTO  → autoCreateCodec() (peek at bytes to decide)
```

## ActiveStream — Per-Request Object

When the codec encounters a new HTTP request, it calls `newStream()` on HCM:

```mermaid
sequenceDiagram
    participant Codec as HTTP Codec
    participant HCM as ConnectionManagerImpl
    participant AS as ActiveStream

    Codec->>HCM: newStream(response_encoder)
    HCM->>AS: new ActiveStream(*this, response_encoder)
    HCM->>HCM: streams_.pushFront(active_stream)
    AS->>AS: Set up timers (idle, request, max duration)
    AS->>AS: Set up access log flush
    HCM-->>Codec: return *active_stream (as RequestDecoder)
    
    Note over Codec: Codec now calls active_stream->decodeHeaders()
    Note over Codec: active_stream->decodeData()
    Note over Codec: active_stream->decodeTrailers()
```

```
File: source/common/http/conn_manager_impl.cc (lines 324-379)

newStream(response_encoder, is_internally_created):
    1. Create ActiveStream with response_encoder reference
    2. Wire up stream callbacks (reset, watermarks)
    3. Add to streams_ linked list
    4. Return ActiveStream as RequestDecoder
```

### What ActiveStream Implements

```mermaid
classDiagram
    class ActiveStream {
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        -request_headers_
        -response_encoder_
        -filter_manager_ : DownstreamFilterManager
        -state_ : State
    }
    class RequestDecoder {
        <<interface>>
        +decodeHeaders()
        +decodeData()
        +decodeTrailers()
    }
    class StreamCallbacks {
        <<interface>>
        +onResetStream()
        +onAboveWriteBufferHighWatermark()
    }
    class FilterManagerCallbacks {
        <<interface>>
        +encodeHeaders()
        +encodeData()
        +encodeTrailers()
    }
    class DownstreamStreamFilterCallbacks {
        <<interface>>
        +route()
        +clusterInfo()
        +streamInfo()
    }

    ActiveStream ..|> RequestDecoder
    ActiveStream ..|> StreamCallbacks
    ActiveStream ..|> FilterManagerCallbacks
    ActiveStream ..|> DownstreamStreamFilterCallbacks
```

`ActiveStream` is a junction point connecting:
1. **Codec** (upstream from it) — via `RequestDecoder` interface
2. **HTTP Filter Chain** (downstream from it) — via `DownstreamFilterManager`
3. **Connection** (for encoding responses) — via `response_encoder_`

## Multiple Streams on One Connection

HTTP/2 supports multiplexing. A single HCM manages multiple concurrent `ActiveStream` objects:

```mermaid
graph TD
    subgraph "ConnectionManagerImpl"
        Codec["HTTP/2 Codec"]
        SL["streams_ (LinkedList)"]
        
        AS1["ActiveStream 1<br/>GET /api/users"]
        AS2["ActiveStream 2<br/>POST /api/orders"]
        AS3["ActiveStream 3<br/>GET /static/logo.png"]
        
        SL --> AS1
        SL --> AS2
        SL --> AS3
    end
    
    Codec -->|"stream 1"| AS1
    Codec -->|"stream 3"| AS2
    Codec -->|"stream 5"| AS3
```

For HTTP/1, there is at most one active stream at a time (request-response, then the next).

## Connection vs Stream Lifecycle

```mermaid
stateDiagram-v2
    state "Connection Level (HCM)" as ConnLevel {
        [*] --> Active : connection created
        Active --> Draining : drain timeout / GOAWAY
        Active --> Closing : connection error
        Draining --> Closing : all streams done
        Closing --> [*]
    }
    
    state "Stream Level (ActiveStream)" as StreamLevel {
        [*] --> DecodingHeaders : newStream()
        DecodingHeaders --> DecodingBody : decodeHeaders() if !end_stream
        DecodingHeaders --> EncodingHeaders : decodeHeaders() if end_stream
        DecodingBody --> DecodingTrailers : decodeData()
        DecodingBody --> EncodingHeaders : decodeData() if end_stream
        DecodingTrailers --> EncodingHeaders : decodeTrailers()
        EncodingHeaders --> EncodingBody : encodeHeaders()
        EncodingBody --> Complete : encodeData() if end_stream
        EncodingHeaders --> Complete : encodeHeaders() if end_stream
        Complete --> [*] : stream destroyed
    }
```

## HCM Configuration

`ConnectionManagerConfig` (`source/common/http/conn_manager_config.h`) provides all HCM settings:

```mermaid
graph TD
    CMC["ConnectionManagerConfig"]
    CMC --> CC["createCodec() — codec factory"]
    CMC --> CFC["createFilterChain() — HTTP filter chain"]
    CMC --> RC["routeConfig() — route table"]
    CMC --> Timeouts["requestTimeout()<br/>idleTimeout()<br/>maxStreamDuration()"]
    CMC --> Limits["maxRequestHeadersKb()<br/>maxRequestHeadersCount()"]
    CMC --> Features["serverName()<br/>proxy100Continue()<br/>streamErrorOnInvalidHttpMessage()"]
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `source/common/http/conn_manager_impl.h` | 59-130 | `ConnectionManagerImpl` class |
| `source/common/http/conn_manager_impl.h` | 123-220 | `ActiveStream` class |
| `source/common/http/conn_manager_impl.cc` | 122-156 | HCM initialization |
| `source/common/http/conn_manager_impl.cc` | 324-379 | `newStream()` creates ActiveStream |
| `source/common/http/conn_manager_impl.cc` | 394-414 | `createCodec()` |
| `source/common/http/conn_manager_impl.cc` | 415-467 | `onData()` main entry point |
| `source/common/http/conn_manager_config.h` | 222-231 | `createCodec()` interface |
| `source/extensions/filters/network/http_connection_manager/config.cc` | 537-556 | Codec creation by protocol |
| `source/extensions/filters/network/http_connection_manager/config.h` | — | `HttpConnectionManagerConfig` |

---

**Previous:** [Part 6 — Transport Sockets and TLS](06-transport-sockets.md)  
**Next:** [Part 8 — HTTP Codec Layer: Protocol Parsing](08-http-codec.md)
