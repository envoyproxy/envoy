# Envoy HTTP Layer — Overview Part 2: Codecs & Connection Pools

**Directory:** `source/common/http/`  
**Part:** 2 of 4 — HTTP Codecs (H1/H2/H3), Connection Pools, Protocol Details

---

## Table of Contents

1. [Codec Architecture Overview](#1-codec-architecture-overview)
2. [HTTP/1.1 Codec (`http1/`)](#2-http11-codec-http1)
3. [HTTP/2 Codec (`http2/`)](#3-http2-codec-http2)
4. [HTTP/3 Codec (`http3/`)](#4-http3-codec-http3)
5. [CodecClient — Upstream Client](#5-codecclient--upstream-client)
6. [Connection Pool Architecture](#6-connection-pool-architecture)
7. [ConnectivityGrid — HTTP/3 Happy Eyeballs](#7-connectivitygrid--http3-happy-eyeballs)
8. [Mixed Pool — ALPN-Based Selection](#8-mixed-pool--alpn-based-selection)
9. [Pool Lifecycle & Drain](#9-pool-lifecycle--drain)

---

## 1. Codec Architecture Overview

Envoy's codec layer provides a unified streaming API (`RequestDecoder`/`ResponseDecoder` + `RequestEncoder`/`ResponseEncoder`) over different HTTP protocol versions. The codec handles framing, multiplexing, flow control, and protocol error detection.

```mermaid
flowchart TD
    subgraph "Codec Abstraction (envoy/http/codec.h)"
        ServerConn["ServerConnection\n(downstream, receives requests)"]
        ClientConn["ClientConnection\n(upstream, sends requests)"]
        SE["StreamEncoder\n(encode headers/data/trailers)"]
        SD["StreamDecoder\n(decode headers/data/trailers)"]
    end

    subgraph "HTTP/1.1 (http1/)"
        H1SC["Http1::ServerConnectionImpl"]
        H1CC["Http1::ClientConnectionImpl"]
        Parser["Parser\n(Balsa / LegacyParser)"]
    end

    subgraph "HTTP/2 (http2/)"
        H2SC["Http2::ServerConnectionImpl"]
        H2CC["Http2::ClientConnectionImpl"]
        OgHTTP2["oghttp2 / nghttp2 adapter"]
    end

    subgraph "HTTP/3 (http3/)"
        H3SC["Http3::ServerConnectionImpl"]
        H3CC["Http3::ClientConnectionImpl"]
        QUIC["QUIC (quiche)"]
    end

    ServerConn <|-- H1SC
    ServerConn <|-- H2SC
    ServerConn <|-- H3SC
    ClientConn <|-- H1CC
    ClientConn <|-- H2CC
    ClientConn <|-- H3CC

    H1SC --> Parser
    H2SC --> OgHTTP2
    H3SC --> QUIC
```

---

## 2. HTTP/1.1 Codec (`http1/`)

### Key Files

| File | Purpose |
|------|---------|
| `http1/codec_impl.h/.cc` | Main H1 codec implementation |
| `http1/parser.h` | Abstract `Parser` interface |
| `http1/balsa_parser.h/.cc` | Google Balsa (default) |
| `http1/legacy_parser_impl.h/.cc` | http-parser (legacy, deprecated) |
| `http1/header_formatter.h/.cc` | Optional header case preservation |
| `http1/conn_pool.h/.cc` | H1-specific connection pool |

### Protocol Flow

```mermaid
sequenceDiagram
    participant Net as Network
    participant H1 as Http1::ConnectionImpl
    participant Parser as BalsaParser
    participant Callbacks as StreamCallbacks

    Net->>H1: dispatch(raw_bytes)
    H1->>Parser: feed(raw_bytes)
    Parser->>H1: onHeaderField("content-type", "json")
    Parser->>H1: onHeaderValue("application/json")
    Parser->>H1: onHeadersComplete()
    H1->>Callbacks: decodeHeaders(header_map, has_body)
    Parser->>H1: onBody(chunk)
    H1->>Callbacks: decodeData(buffer, false)
    Parser->>H1: onMessageComplete()
    H1->>Callbacks: decodeData(empty, true)
```

### Parser Abstraction

```mermaid
classDiagram
    class Parser {
        <<interface>>
        +execute(data, length): size_t
        +resume()
        +pause()
        +getStatus(): ParserStatus
        +methodName(): absl::string_view
        +statusCode(): uint64_t
        +isChunked(): bool
    }

    class BalsaParser {
        -framer_: BalsaFrame
        -visitor_: BalsaVisitor
    }

    class LegacyParserImpl {
        -settings_: http_parser_settings
        -parser_: http_parser
    }

    Parser <|-- BalsaParser
    Parser <|-- LegacyParserImpl
```

### H1-Specific Features

| Feature | Implementation |
|---------|---------------|
| Keep-alive | `Connection: keep-alive` detection, connection reuse |
| Pipelining | Queue of pending requests (not concurrently processed) |
| Chunked transfer | `Transfer-Encoding: chunked` encode/decode |
| 100-Continue | `Expect: 100-continue` handling before body |
| WebSocket upgrade | Protocol upgrade via `Upgrade: websocket` |
| CONNECT tunneling | HTTP CONNECT method for tunneling |
| Header case | Optional preservation via `HeaderFormatter` |

### Header Formatting

```mermaid
flowchart LR
    Header["content-type: text/plain"] --> HF{HeaderFormatter mode?}
    HF -->|None| Lower["content-type: text/plain\n(lowercase)"]
    HF -->|ProperCase| Proper["Content-Type: text/plain\n(proper case)"]
    HF -->|Preserve| Orig["CONTENT-TYPE: text/plain\n(as sent by client)"]
```

---

## 3. HTTP/2 Codec (`http2/`)

### Key Files

| File | Purpose |
|------|---------|
| `http2/codec_impl.h/.cc` | Main H2 codec wrapping oghttp2/nghttp2 |
| `http2/protocol_constraints.h/.cc` | Flood/abuse detection |
| `http2/metadata_decoder.h/.cc` | Envoy METADATA frame extension |
| `http2/metadata_encoder.h/.cc` | Envoy METADATA frame encoding |
| `http2/conn_pool.h/.cc` | H2-specific multiplexed pool |

### Architecture

```mermaid
classDiagram
    class ConnectionImpl {
        -adapter_: Http2Adapter (oghttp2 or nghttp2)
        -active_streams_: map~int32_t, StreamImplPtr~
        -protocol_constraints_: ProtocolConstraints
        +dispatch(data): Status
        +sendSettings(options)
        +submitMetadata(stream_id, metadata)
    }

    class StreamImpl {
        -stream_id_: int32_t
        -decoder_: RequestDecoder*
        -encoder_: StreamEncoder*
        -pending_recv_data_: Buffer
        -metadata_decoder_: MetadataDecoder
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
    }

    class ProtocolConstraints {
        -inbound_headers_frames_: uint32_t
        -inbound_ping_frames_before_ack_: uint32_t
        -consecutive_inbound_frames_with_empty_payload_: uint32_t
        +trackInboundFrames(type, flags, length): Status
    }

    class Http2Adapter {
        <<interface>>
        +ProcessBytes(data)
        +SubmitResponse(stream_id, headers)
        +SubmitData(stream_id, data)
    }

    ConnectionImpl *-- StreamImpl
    ConnectionImpl *-- ProtocolConstraints
    ConnectionImpl --> Http2Adapter
```

### Stream Multiplexing

```mermaid
sequenceDiagram
    participant Net
    participant H2C as Http2::ConnectionImpl
    participant Adapter as oghttp2 Adapter
    participant S1 as Stream 1
    participant S2 as Stream 3

    Net->>H2C: dispatch(bytes with frames for stream 1 and 3)
    H2C->>Adapter: ProcessBytes(bytes)
    Adapter->>H2C: OnBeginHeadersForStream(1)
    H2C->>S1: decodeHeaders(headers)
    Adapter->>H2C: OnBeginHeadersForStream(3)
    H2C->>S2: decodeHeaders(headers)
    Adapter->>H2C: OnDataForStream(1, data_chunk)
    H2C->>S1: decodeData(data)
    Adapter->>H2C: OnDataForStream(3, data_chunk)
    H2C->>S2: decodeData(data)
```

### Protocol Constraints (Flood Protection)

`ProtocolConstraints` tracks frame rates to detect and block H2 flooding attacks:

| Frame Type | Default Limit | Condition Detected |
|-----------|---------------|-------------------|
| HEADERS frames | 100 / stream | Rapid header flood |
| PING frames before ACK | 10 | PING flood |
| DATA frames with empty payload | 1000 | Empty frame flood |
| PRIORITY frames | 2^31-1 (disabled) | PRIORITY flood |
| RST_STREAM frames | configurable | RST flood |

### METADATA Extension

Envoy supports a non-standard HTTP/2 METADATA frame (opaque key-value data attached to a stream, used by Istio for telemetry):

```mermaid
flowchart LR
    Filter["Filter calls\nencodeMetadata(metadata)"] --> ME["MetadataEncoder\n(serializes to METADATA frames)"]
    ME --> Adapter["oghttp2\n(frame transmission)"]
    Adapter --> Net["Network"]

    Net --> Adapter2["oghttp2\n(frame reception)"]
    Adapter2 --> MD["MetadataDecoder\n(deserializes)"]
    MD --> Filter2["Filter receives\nonMetadataDecoded(metadata_map)"]
```

---

## 4. HTTP/3 Codec (`http3/`)

### Architecture

```mermaid
flowchart TD
    subgraph "HTTP/3 Stack"
        H3["Http3::ServerConnectionImpl\n(HTTP/3 framing on QUIC streams)"]
        QUIC["quiche::QuicConnection\n(QUIC transport)"]
        UDP["UDP Socket"]
    end

    H3 -->|QUIC streams| QUIC
    QUIC --> UDP

    subgraph "HTTP/3 Pool"
        AC["Http3::ActiveClient\n(quiche_capacity_ tracking)"]
        CC["CodecClient (H3)"]
    end

    AC --> CC --> H3
```

### H3-Specific Features

| Feature | Detail |
|---------|--------|
| 0-RTT | Early data support for faster connection setup |
| `quiche_capacity_` | Dynamic stream limit from QUIC's MAX_STREAMS frame |
| Connection migration | IP/port change without reconnecting |
| No head-of-line blocking | Independent stream delivery (unlike H2 over TCP) |
| Alt-Svc integration | Discovered via `http_server_properties_cache` |

---

## 5. CodecClient — Upstream Client

`CodecClient` bridges the pool layer and the codec layer for upstream connections:

```mermaid
flowchart TD
    Pool["HttpConnPoolImplBase::ActiveClient"] -->|owns| CC["CodecClient"]
    CC -->|owns| Codec["ClientConnection (H1/H2/H3)"]
    CC -->|owns| NetConn["Network::ClientConnection"]

    CC -->|implements| ConnCB["Network::ConnectionCallbacks"]
    CC -->|implements| HttpCB["Http::ConnectionCallbacks"]
    CC -->|implements| Deferrable["Event::DeferredDeletable"]
```

### Connect Timeout Flow

```mermaid
sequenceDiagram
    participant Pool
    participant CC as CodecClient
    participant Net as Network

    Pool->>CC: new CodecClientProd(H2, connection, host, ...)
    CC->>CC: connect_timer_.enableTimer(connect_timeout)
    Net-->>CC: onEvent(Connected)
    CC->>CC: connect_timer_.disableTimer()
    CC-->>Pool: Ready for streams

    alt Timeout
        CC->>CC: connect_timer_ fires
        CC->>Net: close(NoFlush)
        CC-->>Pool: onEvent(RemoteClose) → pool removes client
    end
```

---

## 6. Connection Pool Architecture

### Pool Hierarchy

```mermaid
flowchart TD
    CMA["ClusterManager\n(owns pool map per cluster+priority+protocol)"]
    CMA --> Grid["ConnectivityGrid\n(H3 → H2/H1 fallback)"]
    CMA --> Mixed["HttpConnPoolImplMixed\n(ALPN H1/H2)"]
    CMA --> H2Pool["Http2ConnPoolImpl\n(pure H2)"]
    CMA --> H1Pool["Http1ConnPoolImpl\n(pure H1)"]

    Grid -->|primary attempt| H3Pool["Http3ConnPoolImpl"]
    Grid -->|fallback| Mixed

    H1Pool -->|1 stream/conn| CC1["CodecClient (H1)"]
    H2Pool -->|N streams/conn| CC2["CodecClient (H2)"]
    H3Pool -->|N streams/conn| CC3["CodecClient (H3)"]
```

### `HttpPendingStream` — Queued Request

When no connection is available, the request is queued as an `HttpPendingStream`:

```mermaid
classDiagram
    class HttpPendingStream {
        +decoder_: ResponseDecoder*
        +callbacks_: ConnectionPool::Callbacks*
        +cancel(): void
    }
    class PendingStream { <<base>> }
    PendingStream <|-- HttpPendingStream
```

### Pool Flow — Connection Available vs. Queue

```mermaid
flowchart TD
    NS["newStream(decoder, callbacks)"] --> A{Active connection\nwith capacity?}
    A -->|Yes| B["attachRequestToClient()\nCodecClient::newStream()"]
    A -->|No| C{Max connections\nreached?}
    C -->|No| D["createActiveClient()\n(new upstream TCP connect)"]
    C -->|Yes| E["Queue as HttpPendingStream"]
    D --> F["onEvent(Connected)"]
    F --> G["onUpstreamReady()\nDequeue pending stream"]
    G --> B
    E --> G
    B --> H["Return RequestEncoder to Router"]
```

### Max Requests Per Connection

| Protocol | Default | Config |
|----------|---------|--------|
| HTTP/1.1 | 1 (concurrent), unlimited (sequential) | `max_requests_per_connection` |
| HTTP/2 | unlimited (up to SETTINGS_MAX_CONCURRENT_STREAMS) | `max_requests_per_connection` |
| HTTP/3 | dynamic (`quiche_capacity_`) | QUIC MAX_STREAMS |

---

## 7. ConnectivityGrid — HTTP/3 Happy Eyeballs

```mermaid
sequenceDiagram
    participant Router
    participant Grid as ConnectivityGrid
    participant WC as WrapperCallbacks
    participant H3 as HTTP/3 Pool
    participant H2 as HTTP/2 Pool

    Router->>Grid: newStream(decoder, caller_cb)
    Grid->>WC: new WrapperCallbacks(caller_cb)
    Grid->>H3: newStream(decoder, WC)
    Grid->>Grid: next_attempt_timer_.enableTimer(300ms)

    alt HTTP/3 ready before timer
        H3-->>WC: onPoolReady(h3_encoder)
        WC->>Grid: cancelH2Attempt()
        WC->>Router: caller_cb.onPoolReady(h3_encoder)
    else Timer fires
        Grid->>H2: newStream(decoder, WC)
        alt H2 wins
            H2-->>WC: onPoolReady(h2_encoder)
            WC->>Grid: cancelH3Attempt()
            WC->>Router: caller_cb.onPoolReady(h2_encoder)
        else H3 wins (late)
            H3-->>WC: onPoolReady(h3_encoder)
            WC->>Grid: cancelH2Attempt()
            WC->>Router: caller_cb.onPoolReady(h3_encoder)
        end
    else H3 fails
        H3-->>WC: onPoolFailure(reason)
        Grid->>H2: newStream(decoder, WC) (if not started)
        H2-->>WC: onPoolReady(h2_encoder)
        WC->>Router: caller_cb.onPoolReady(h2_encoder)
    end
```

### H3 Status Backoff

```mermaid
stateDiagram-v2
    [*] --> Unknown
    Unknown --> H3Confirmed : first successful H3 stream
    Unknown --> H3Broken : H3 connection fails
    H3Confirmed --> H3Broken : H3 error during active use
    H3Broken --> Unknown : backoff expires
    H3Broken --> H3Broken : within backoff (skip H3 attempts)
    H3Confirmed --> H3Confirmed : normal operation
```

---

## 8. Mixed Pool — ALPN-Based Selection

`HttpConnPoolImplMixed` starts a TLS connection and defers protocol selection until ALPN negotiation completes:

```mermaid
sequenceDiagram
    participant Pool as HttpConnPoolImplMixed
    participant TLS as TLS Layer
    participant H1 as Http1ConnPoolImpl
    participant H2 as Http2ConnPoolImpl

    Pool->>TLS: connect (ALPN = ["h2", "http/1.1"])
    TLS-->>Pool: onEvent(Connected) + ALPN result
    alt ALPN = "h2"
        Pool->>H2: createCodecClient(H2)
        Pool->>H2: attach pending streams
    else ALPN = "http/1.1" or no ALPN
        Pool->>H1: createCodecClient(H1)
        Pool->>H1: attach pending streams
    end
```

---

## 9. Pool Lifecycle & Drain

```mermaid
stateDiagram-v2
    [*] --> Ready : Pool created
    Ready --> Draining : drainConnections(reason)
    Ready --> Ready : streams come and go
    Draining --> Draining : existing connections finish
    Draining --> [*] : all connections closed
    Ready --> [*] : cluster removed / Envoy shutdown
```

### Drain Reasons

| Reason | Behavior |
|--------|----------|
| `DrainExistingConnections` | No new streams on existing connections; create new connections for new streams |
| `EvictIdleConnections` | Close connections with no in-flight streams immediately |

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_request_pipeline.md) | Architecture, Request Pipeline, ConnectionManager, FilterSystem |
| **Part 2 (this file)** | Codecs (H1/H2/H3), Connection Pools, Protocol Details |
| [Part 3](OVERVIEW_PART3_headers_and_utilities.md) | Header System, Utilities, Path Normalization |
| [Part 4](OVERVIEW_PART4_async_and_advanced.md) | Async Client, HTTP/3, Server Properties, Advanced Topics |
