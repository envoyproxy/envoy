# Part 8: HTTP Codec Layer — Protocol Parsing

## Overview

The HTTP codec sits between raw bytes and structured HTTP messages. It parses incoming bytes into headers, body, and trailers, and serializes outgoing responses. Envoy supports HTTP/1.1, HTTP/2, and HTTP/3, each with a different codec implementation but the same interface.

## Codec Architecture

```mermaid
graph TB
    subgraph "Codec Interface Layer"
        SC["ServerConnection<br/>(interface)"]
        RE["ResponseEncoder<br/>(interface)"]
        RD["RequestDecoder<br/>(interface)"]
    end
    
    subgraph "HTTP/1.1"
        H1SC["Http1::ServerConnectionImpl"]
        H1RE["Http1::ResponseEncoderImpl"]
    end
    
    subgraph "HTTP/2"
        H2SC["Http2::ServerConnectionImpl"]
        H2RE["Http2::ResponseEncoderImpl"]
    end
    
    subgraph "HTTP/3"
        H3SC["QuicHttpServerConnectionImpl"]
        H3RE["QuicResponseEncoder"]
    end
    
    SC --> H1SC
    SC --> H2SC
    SC --> H3SC
    
    RE --> H1RE
    RE --> H2RE
    RE --> H3RE
    
    style SC fill:#e1f5fe
    style RE fill:#e1f5fe
    style RD fill:#e1f5fe
```

## Key Codec Interfaces

```mermaid
classDiagram
    class Connection {
        <<interface>>
        +dispatch(Buffer data) Status
        +goAway()
        +protocol() Protocol
        +wantWrite() bool
    }
    class ServerConnection {
        <<interface>>
    }
    class ServerConnectionCallbacks {
        <<interface>>
        +newStream(ResponseEncoder encoder) RequestDecoder
    }
    class RequestDecoder {
        <<interface>>
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +decodeMetadata(metadata)
    }
    class ResponseEncoder {
        <<interface>>
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
        +encodeTrailers(trailers)
        +getStream() Stream
    }
    class Stream {
        <<interface>>
        +addCallbacks(StreamCallbacks)
        +removeCallbacks(StreamCallbacks)
        +resetStream(reason)
        +readDisable(disable)
    }
    class StreamCallbacks {
        <<interface>>
        +onResetStream(reason)
        +onAboveWriteBufferHighWatermark()
        +onBelowWriteBufferLowWatermark()
    }

    ServerConnection --|> Connection
    Connection ..> ServerConnectionCallbacks : "uses"
    ServerConnectionCallbacks ..> RequestDecoder : "returns"
    ServerConnectionCallbacks ..> ResponseEncoder : "provides"
    ResponseEncoder --> Stream : "getStream()"
    Stream --> StreamCallbacks : "notifies"
```

**Interface location:** `envoy/http/codec.h`

- `Connection` (lines 428-450) — `dispatch()` is the main entry
- `ServerConnectionCallbacks` (lines 416-428) — HCM implements this
- `RequestDecoder` (lines 241-280) — `ActiveStream` implements this
- `ResponseEncoder` (lines 145-210) — codec provides this for each stream

## The dispatch() Call

The central method that drives HTTP parsing:

```mermaid
flowchart TD
    A["HCM: codec_->dispatch(buffer)"] --> B["Codec parses bytes"]
    B --> C{New request detected?}
    C -->|Yes| D["callbacks_.newStream(encoder)"]
    D --> E["HCM creates ActiveStream"]
    E --> F["Returns ActiveStream as RequestDecoder"]
    F --> G["Codec stores request_decoder_"]
    C -->|No| H["Continue parsing current request"]
    
    G --> I["Parse headers complete?"]
    H --> I
    I -->|Yes| J["request_decoder_->decodeHeaders(headers, end_stream)"]
    J --> K["Parse body chunks"]
    K --> L["request_decoder_->decodeData(data, end_stream)"]
    L --> M{Trailers?}
    M -->|Yes| N["request_decoder_->decodeTrailers(trailers)"]
    M -->|No| O["Done with this request"]
    N --> O
```

## HTTP/1.1 Codec Flow

### Parsing Architecture

HTTP/1.1 uses a streaming parser (based on llhttp/http-parser):

```mermaid
sequenceDiagram
    participant HCM as ConnectionManagerImpl
    participant Codec as Http1::ServerConnectionImpl
    participant Parser as HTTP Parser (llhttp)
    participant AS as ActiveStream

    HCM->>Codec: dispatch(buffer)
    Codec->>Parser: execute(buffer.data(), buffer.length())
    
    Note over Parser: Parser callbacks fire:
    Parser->>Codec: onMessageBegin()
    Codec->>HCM: newStream(response_encoder)
    HCM->>AS: new ActiveStream
    HCM-->>Codec: request_decoder (ActiveStream)
    
    Parser->>Codec: onHeaderField("Host")
    Parser->>Codec: onHeaderValue("example.com")
    Parser->>Codec: onHeadersComplete()
    Codec->>AS: decodeHeaders(headers, end_stream=false)
    
    Parser->>Codec: onBody(data, length)
    Codec->>AS: decodeData(data, end_stream=false)
    
    Parser->>Codec: onMessageComplete()
    Codec->>AS: decodeData(empty, end_stream=true)
```

```
File: source/common/http/http1/codec_impl.cc

Key flow:
- onMessageBeginBase() → creates ResponseEncoder, calls callbacks_.newStream()
- onHeadersCompleteBase() → builds HeaderMap, calls decodeHeaders()
- onBody() → wraps data in Buffer, calls decodeData()  
- onMessageComplete() → sets end_stream=true
```

### HTTP/1.1 One Request at a Time

```mermaid
graph LR
    subgraph "HTTP/1.1 Connection"
        R1["Request 1<br/>GET /api"] --> Resp1["Response 1<br/>200 OK"]
        Resp1 --> R2["Request 2<br/>POST /data"]
        R2 --> Resp2["Response 2<br/>201 Created"]
    end
    
    Note["Only one active_request_<br/>at a time"]
```

HTTP/1.1 codec tracks `active_request_` — a single request/response pair. The next request is only parsed after the current response is sent (unless pipelining, which is limited).

## HTTP/2 Codec Flow

### Multiplexed Streams

HTTP/2 uses nghttp2 (or oghttp2) to handle frame parsing:

```mermaid
sequenceDiagram
    participant HCM as ConnectionManagerImpl
    participant Codec as Http2::ServerConnectionImpl
    participant nghttp2 as nghttp2 Library
    participant S1 as Stream 1 (ActiveStream)
    participant S3 as Stream 3 (ActiveStream)

    HCM->>Codec: dispatch(buffer)
    Codec->>nghttp2: nghttp2_session_mem_recv(data)
    
    Note over nghttp2: Frame parsing callbacks:
    
    nghttp2->>Codec: on_begin_headers(stream_id=1)
    Codec->>HCM: newStream(encoder_1)
    HCM-->>Codec: request_decoder_1
    
    nghttp2->>Codec: on_header(stream_id=1, ":method", "GET")
    nghttp2->>Codec: on_header(stream_id=1, ":path", "/api")
    nghttp2->>Codec: on_frame_recv(HEADERS, stream_id=1, END_HEADERS)
    Codec->>S1: decodeHeaders(headers, end_stream=false)
    
    nghttp2->>Codec: on_begin_headers(stream_id=3)
    Codec->>HCM: newStream(encoder_3)
    HCM-->>Codec: request_decoder_3
    
    nghttp2->>Codec: on_data_chunk(stream_id=1, data)
    Codec->>S1: decodeData(data, end_stream=false)
    
    nghttp2->>Codec: on_header(stream_id=3, ":method", "POST")
    nghttp2->>Codec: on_frame_recv(HEADERS, stream_id=3, END_STREAM)
    Codec->>S3: decodeHeaders(headers, end_stream=true)
```

```mermaid
graph TD
    subgraph "HTTP/2 Connection"
        Codec["Http2::ServerConnectionImpl"]
        SM["Stream Map<br/>(stream_id → ServerStreamImpl)"]
        
        S1["Stream 1<br/>GET /api/users"]
        S3["Stream 3<br/>POST /api/orders"]
        S5["Stream 5<br/>GET /health"]
        
        SM --> S1
        SM --> S3
        SM --> S5
    end
    
    Codec --> SM
```

## From Codec to ActiveStream

### The Handoff Pattern

The codec-to-HCM interaction follows a producer-consumer pattern:

```mermaid
flowchart LR
    subgraph "Codec (Producer)"
        Parse["Parse bytes"]
        Enc["Provide ResponseEncoder"]
    end
    
    subgraph "HCM (Bridge)"
        NS["newStream()"]
        AS["Create ActiveStream"]
    end
    
    subgraph "ActiveStream (Consumer)"
        DH["decodeHeaders()"]
        DD["decodeData()"]
        DT["decodeTrailers()"]
    end
    
    Parse --> NS
    NS --> AS
    Enc --> AS
    AS --> DH
    DH --> DD
    DD --> DT
```

The key contract:
1. Codec calls `ServerConnectionCallbacks::newStream(encoder)` to get a `RequestDecoder`
2. Codec then calls `decodeHeaders()`, `decodeData()`, `decodeTrailers()` on that decoder
3. The decoder (ActiveStream) owns the `ResponseEncoder` for sending the response back

## ActiveStream::decodeHeaders() — The Big One

`decodeHeaders()` on `ActiveStream` is where HTTP processing truly begins:

```mermaid
flowchart TD
    A["ActiveStream::decodeHeaders(headers, end_stream)"] --> B["Validate headers<br/>(method, path, host)"]
    B --> C["ConnectionManagerUtility::mutateRequestHeaders()"]
    C --> D["Add x-forwarded-for, x-request-id, etc."]
    D --> E["Snap route configuration"]
    E --> F["refreshCachedRoute()<br/>→ find matching route"]
    F --> G["createFilterChain()<br/>→ build HTTP filter chain"]
    G --> H{Defer to next IO cycle?}
    H -->|No| I["filter_manager_.decodeHeaders(headers, end_stream)"]
    H -->|Yes| J["Schedule for next dispatcher iteration"]
    I --> K["HTTP filters process headers"]
    
    style G fill:#fff9c4
    style I fill:#c8e6c9
```

```
File: source/common/http/conn_manager_impl.cc (lines ~1260-1340)

decodeHeaders(headers, end_stream):
    1. Request validation (method, path, content-length)
    2. mutateRequestHeaders() → add proxy headers
    3. Snap route config (RDS/scoped routes)
    4. refreshCachedRoute() → route resolution
    5. createFilterChain() → build HTTP filter chain
    6. filter_manager_.decodeHeaders() → start filter iteration
```

## Response Encoding

When a response is ready (from upstream or local reply), the encode path goes through the codec:

```mermaid
sequenceDiagram
    participant HFM as HTTP FilterManager
    participant AS as ActiveStream
    participant RE as ResponseEncoder
    participant Codec as HTTP Codec
    participant Conn as Connection

    HFM->>AS: encodeHeaders(response_headers, end_stream)
    AS->>RE: encodeHeaders(headers, end_stream)
    RE->>Codec: Serialize headers
    Codec->>Conn: Write serialized bytes
    
    alt Has body
        HFM->>AS: encodeData(data, end_stream)
        AS->>RE: encodeData(data, end_stream)
        RE->>Codec: Serialize body
        Codec->>Conn: Write serialized bytes
    end
    
    alt Has trailers
        HFM->>AS: encodeTrailers(trailers)
        AS->>RE: encodeTrailers(trailers)
        RE->>Codec: Serialize trailers
        Codec->>Conn: Write serialized bytes
    end
```

## Protocol Detection (AUTO Codec)

When `codec_type: AUTO`, Envoy peeks at the first bytes to determine the protocol:

```mermaid
flowchart TD
    A["First bytes arrive"] --> B["Peek at buffer"]
    B --> C{Starts with<br/>'PRI * HTTP/2.0'}
    C -->|Yes| D["Create HTTP/2 codec"]
    C -->|No| E{Valid HTTP/1 method?}
    E -->|Yes| F["Create HTTP/1 codec"]
    E -->|No| G["Error: unknown protocol"]
    
    Note1["ALPN from TLS can also<br/>determine protocol before<br/>any data is read"]
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `envoy/http/codec.h` | 145-450 | All codec interfaces |
| `source/common/http/http1/codec_impl.h` | — | HTTP/1.1 server codec |
| `source/common/http/http1/codec_impl.cc` | 1144-1164 | HTTP/1 newStream and decodeHeaders |
| `source/common/http/http2/codec_impl.h` | — | HTTP/2 server codec |
| `source/common/http/http2/codec_impl.cc` | 675, 2200 | HTTP/2 stream creation |
| `source/common/http/conn_manager_impl.cc` | 324-379 | `newStream()` |
| `source/common/http/conn_manager_impl.cc` | 394-414 | `createCodec()` |
| `source/common/http/conn_manager_impl.cc` | 415-467 | `onData()` dispatches to codec |
| `source/common/http/conn_manager_impl.cc` | ~1260-1340 | `ActiveStream::decodeHeaders()` |

---

**Previous:** [Part 7 — HTTP Connection Manager](07-http-connection-manager.md)  
**Next:** [Part 9 — HTTP Filter Manager: Decode and Encode Paths](09-http-filter-manager.md)
