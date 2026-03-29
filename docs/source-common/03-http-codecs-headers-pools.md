# Part 3: `source/common/http/` — Codecs, Headers, and Connection Pools

## Overview

This document covers the three other major subsystems in `source/common/http/`: the HTTP codecs (HTTP/1.1, HTTP/2, HTTP/3), the header map implementation, and the connection pooling layer.

## HTTP Codecs

### Codec Class Hierarchy

```mermaid
classDiagram
    class ServerConnection {
        <<interface>>
        +dispatch(Buffer) Status
        +goAway()
        +protocol() Protocol
    }
    class ClientConnection {
        <<interface>>
        +newStream(ResponseDecoder) RequestEncoder
        +protocol() Protocol
    }
    
    class Http1_ServerConnectionImpl {
        -parser_ : Parser
        -active_request_ : ActiveRequest
        +dispatch(buffer) Status
        -onMessageBegin()
        -onHeadersComplete()
        -onBody()
        -onMessageComplete()
    }
    class Http1_ClientConnectionImpl {
        -pending_responses_ : list
        +newStream(decoder) RequestEncoder
    }
    
    class Http2_ServerConnectionImpl {
        -session_ : nghttp2_session
        -streams_ : map
        +dispatch(buffer) Status
        -onBeginHeaders(stream_id)
        -onHeader(stream_id, name, value)
        -onFrameRecv(frame)
        -onDataChunkRecv(stream_id, data)
    }
    class Http2_ClientConnectionImpl {
        +newStream(decoder) RequestEncoder
    }

    ServerConnection <|-- Http1_ServerConnectionImpl
    ServerConnection <|-- Http2_ServerConnectionImpl
    ClientConnection <|-- Http1_ClientConnectionImpl
    ClientConnection <|-- Http2_ClientConnectionImpl
```

### HTTP/1.1 Codec (`http1/`)

```mermaid
graph TD
    subgraph "http1/ Files"
        CI["codec_impl.h/cc\n— ServerConnectionImpl, ClientConnectionImpl\n— StreamEncoderImpl, ResponseEncoderImpl"]
        P["parser.h\n— Parser interface"]
        BP["balsa_parser.h/cc\n— BalsaParser (default)"]
        LP["legacy_parser_impl.h/cc\n— Legacy parser"]
        HF["header_formatter.h/cc\n— Header key formatting"]
        S["settings.h/cc\n— Http1Settings"]
        CS["codec_stats.h\n— CodecStats"]
        CP["conn_pool.h/cc\n— HTTP/1 connection pool"]
    end
```

#### HTTP/1 Parsing Flow

```mermaid
sequenceDiagram
    participant D as dispatch(buffer)
    participant P as Parser (Balsa/Legacy)
    participant Codec as ServerConnectionImpl
    participant HCM as ServerConnectionCallbacks

    D->>P: execute(data, length)
    P->>Codec: onMessageBegin()
    Codec->>HCM: newStream(response_encoder)
    HCM-->>Codec: request_decoder_
    
    P->>Codec: onHeaderField("Host")
    P->>Codec: onHeaderValue("example.com")
    P->>Codec: onHeadersComplete()
    Codec->>Codec: Build RequestHeaderMapImpl
    Codec-->>HCM: request_decoder_->decodeHeaders()
    
    P->>Codec: onBody(data, len)
    Codec-->>HCM: request_decoder_->decodeData()
    
    P->>Codec: onMessageComplete()
    Codec-->>HCM: end_stream = true
```

### HTTP/2 Codec (`http2/`)

```mermaid
graph TD
    subgraph "http2/ Files"
        CI2["codec_impl.h/cc\n— ConnectionImpl, ServerConnectionImpl\n— StreamImpl, ServerStreamImpl"]
        ME["metadata_encoder.h/cc\n— METADATA frame encoding"]
        MD["metadata_decoder.h/cc\n— METADATA frame decoding"]
        PC["protocol_constraints.h/cc\n— Flood protection"]
        NG["nghttp2.h/cc\n— nghttp2 integration"]
        CS2["codec_stats.h\n— CodecStats"]
        CP2["conn_pool.h/cc\n— HTTP/2 connection pool"]
    end
```

#### HTTP/2 Stream Management

```mermaid
graph LR
    subgraph "HTTP/2 ServerConnectionImpl"
        SM["stream_map_\n(stream_id → StreamImpl)"]
        
        S1["Stream 1\nGET /api"]
        S3["Stream 3\nPOST /data"]
        S5["Stream 5\nGET /health"]
        
        SM --> S1
        SM --> S3
        SM --> S5
    end
    
    nghttp2["nghttp2 session"] --> SM
```

### HTTP/3 Pool (`http3/`)

```mermaid
graph TD
    subgraph "http3/ Files"
        CP3["conn_pool.h/cc\n— Http3ConnPoolImpl, ActiveClient"]
        CS3["codec_stats.h\n— HTTP/3 stats"]
    end
    
    Note["HTTP/3 codec is in source/common/quic/\n(uses QUICHE library)"]
```

## CodecClient — Upstream HTTP Client

```mermaid
classDiagram
    class CodecClient {
        -connection_ : ClientConnectionPtr
        -codec_ : ClientConnectionPtr
        -active_requests_ : list~ActiveRequest~
        +newStream(ResponseDecoder) RequestEncoder
        +close()
        +protocol() Protocol
        +streamInfo() StreamInfo
        +numActiveRequests() uint64_t
    }
    class CodecClientProd {
        +CodecClientProd(type, connection, host, dispatcher, ...)
    }
    class ActiveRequest {
        -encoder_ : RequestEncoder&
        -decoder_ : ResponseDecoder&
        -response_decoder_ : ResponseDecoderWrapper
        -request_encoder_ : RequestEncoderWrapper
    }

    CodecClientProd --|> CodecClient
    CodecClient --> ActiveRequest : "active_requests_"
```

`CodecClient` wraps a network connection + HTTP codec into a single upstream HTTP client. It is used by connection pools to manage streams.

## Header Map Implementation

### HeaderMapImpl Architecture

```mermaid
classDiagram
    class HeaderMap {
        <<interface>>
        +addCopy(key, value)
        +addReference(key, value)
        +get(key) HeaderEntry
        +remove(key)
        +iterate(callback)
        +byteSize() uint64_t
    }
    class HeaderMapImpl {
        -headers_ : HeaderList
        -inline_headers_ : array (O(1) lookup)
        +addViaMove(key, value)
        +addReference(key, value)
        +addCopy(key, value)
        +setCopy(key, value)
        +get(key) HeaderEntry
        +remove(key)
    }
    class RequestHeaderMapImpl {
        +method()
        +host()
        +path()
        +scheme()
        +protocol()
    }
    class ResponseHeaderMapImpl {
        +status()
        +contentType()
        +contentLength()
    }
    class HeaderEntryImpl {
        -key_ : HeaderString
        -value_ : HeaderString
    }

    HeaderMap <|-- HeaderMapImpl
    HeaderMapImpl <|-- RequestHeaderMapImpl
    HeaderMapImpl <|-- ResponseHeaderMapImpl
    HeaderMapImpl *-- HeaderEntryImpl
```

### How Header Lookup Works

```mermaid
graph TD
    Get["get(':method')"] --> Check{Is this a\nknown header?}
    Check -->|Yes| Inline["O(1) inline lookup\n(static array index)"]
    Check -->|No| List["O(n) list scan\n(iterate headers_)"]
    
    Inline --> Result["Return HeaderEntry*"]
    List --> Result
    
    Note1["Well-known headers like :method, :path,\n:status, content-type get O(1) access\nvia compile-time registered slots"]
```

### Header Memory Layout

```mermaid
graph TD
    subgraph "HeaderMapImpl"
        InlineSlots["Inline Header Slots (O(1))\n:method | :path | :scheme | :authority\n:status | content-type | content-length\nx-forwarded-for | x-request-id | ..."]
        
        HeaderList["HeaderList (all headers)\nHost: example.com\nContent-Type: application/json\nX-Custom: value\nAuthorization: Bearer ..."]
        
        StaticLookup["StaticLookupTable\nMaps well-known names → inline slot index"]
    end
    
    StaticLookup -->|"constant-time"| InlineSlots
```

## Connection Pools

### Pool Class Hierarchy

```mermaid
classDiagram
    class ConnPoolImplBase {
        -ready_clients_ : list
        -busy_clients_ : list
        -connecting_clients_ : list
        -pending_streams_ : list
        +newStreamImpl(context)
        +tryCreateNewConnection()
        +onPoolReady(client)
        +onPoolFailure(reason)
    }
    class HttpConnPoolImplBase {
        +newStream(decoder, callbacks) Cancellable
        +onPoolReady(client, context)
    }
    class FixedHttpConnPoolImpl {
        +createCodecClient(data) CodecClientPtr
    }
    class ConnectivityGrid {
        -http3_pool_ : ConnectionPool
        -http2_pool_ : ConnectionPool
        +newStream(decoder, callbacks)
    }
    class HttpConnPoolImplMixed {
        +newStream()
    }

    HttpConnPoolImplBase --|> ConnPoolImplBase
    FixedHttpConnPoolImpl --|> HttpConnPoolImplBase
    ConnectivityGrid --|> ConnPoolImplBase
    HttpConnPoolImplMixed --|> HttpConnPoolImplBase
```

### Connection Pool Types

```mermaid
graph TD
    subgraph "Pool Selection"
        Proto{Upstream Protocol}
        Proto -->|"HTTP/1.1 only"| H1Pool["FixedHttpConnPoolImpl\n(HTTP/1)"]
        Proto -->|"HTTP/2 only"| H2Pool["FixedHttpConnPoolImpl\n(HTTP/2)"]
        Proto -->|"HTTP/1 or HTTP/2\n(ALPN-negotiated)"| MixPool["HttpConnPoolImplMixed"]
        Proto -->|"HTTP/3 with fallback"| Grid["ConnectivityGrid\n(HTTP/3 → HTTP/2)"]
    end
```

### ConnectivityGrid — HTTP/3 with Fallback

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant Grid as ConnectivityGrid
    participant H3Pool as HTTP/3 Pool
    participant H2Pool as HTTP/2 Pool

    Router->>Grid: newStream(decoder, callbacks)
    Grid->>H3Pool: newStream(wrapper_callbacks)
    
    alt HTTP/3 handshake succeeds
        H3Pool-->>Grid: onPoolReady(encoder)
        Grid-->>Router: callbacks.onPoolReady(encoder)
    else HTTP/3 fails / times out
        H3Pool-->>Grid: onPoolFailure()
        Grid->>Grid: markHttp3Broken()
        Grid->>H2Pool: newStream(wrapper_callbacks)
        H2Pool-->>Grid: onPoolReady(encoder)
        Grid-->>Router: callbacks.onPoolReady(encoder)
    end
```

### Pool Client Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Creating : tryCreateNewConnection()
    Creating --> Connecting : createCodecClient()
    Connecting --> Ready : TCP+TLS handshake complete
    Ready --> Busy : stream attached
    Busy --> Ready : stream done, capacity left
    Busy --> Draining : max_streams reached
    Ready --> Idle : no streams for idle_timeout
    Idle --> [*] : close
    Draining --> [*] : last stream done
    Connecting --> [*] : connection failure
```

## Async Client

### AsyncClientImpl

```mermaid
classDiagram
    class AsyncClientImpl {
        +send(request, callbacks, options) Request*
        +start(callbacks, options) Stream*
    }
    class AsyncStreamImpl {
        -router_ : Router::ProdFilter
        -route_ : NullRouteImpl
        -stream_info_ : StreamInfoImpl
        +sendHeaders(headers, end_stream)
        +sendData(data, end_stream)
        +sendTrailers(trailers)
        +reset()
    }
    class AsyncRequestImpl {
        -callbacks_ : AsyncClient::Callbacks&
        +onHeaders(headers, end_stream)
        +onData(data, end_stream)
        +onTrailers(trailers)
        +onComplete()
    }

    AsyncClientImpl --> AsyncStreamImpl
    AsyncRequestImpl --|> AsyncStreamImpl
```

The async client lets Envoy internals (e.g., ext_authz, rate_limit) make HTTP requests without going through the full listener/HCM path. It creates a synthetic route and uses the Router filter directly.

## File Catalog

| File | Key Classes | Purpose |
|------|-------------|---------|
| `codec_client.h/cc` | `CodecClient`, `CodecClientProd`, `ActiveRequest` | Upstream HTTP client |
| `codec_wrappers.h` | `ResponseDecoderWrapper`, `RequestEncoderWrapper` | Codec lifecycle hooks |
| `codec_helper.h` | `StreamCallbackHelper`, `MultiplexedStreamImplBase` | Stream callback base |
| `conn_pool_base.h/cc` | `HttpConnPoolImplBase`, `ActiveClient`, `FixedHttpConnPoolImpl` | HTTP connection pool base |
| `conn_pool_grid.h/cc` | `ConnectivityGrid`, `WrapperCallbacks` | HTTP/3→HTTP/2 failover pool |
| `mixed_conn_pool.h/cc` | `HttpConnPoolImplMixed` | ALPN-negotiated pool |
| `header_map_impl.h/cc` | `HeaderMapImpl`, `RequestHeaderMapImpl`, `ResponseHeaderMapImpl` | Header map with O(1) inline headers |
| `headers.h` | `HeaderValues`, `CustomHeaderValues` | Well-known header names |
| `header_utility.h/cc` | `HeaderUtility` | Header matching/manipulation |
| `header_mutation.h/cc` | Header mutation | Header mutation extensions |
| `hash_policy.h/cc` | `HashPolicyImpl` | Load balancer hash from headers |
| `async_client_impl.h/cc` | `AsyncClientImpl`, `AsyncStreamImpl` | Internal async HTTP client |
| `message_impl.h` | `RequestMessageImpl`, `ResponseMessageImpl` | HTTP message wrappers |
| `http_server_properties_cache_impl.h/cc` | `HttpServerPropertiesCacheImpl` | Alt-Svc, HTTP/3 status cache |
| `http3_status_tracker_impl.h/cc` | HTTP/3 status tracker | HTTP/3 connectivity tracking |
| `null_route_impl.h/cc` | `NullRouteImpl` | Null route for async client |
| `http1/codec_impl.h/cc` | `Http1::ServerConnectionImpl`, `ClientConnectionImpl` | HTTP/1.1 codec |
| `http1/balsa_parser.h/cc` | `BalsaParser` | HTTP/1.1 parser |
| `http1/conn_pool.h/cc` | HTTP/1 pool | HTTP/1.1 connection pool |
| `http2/codec_impl.h/cc` | `Http2::ServerConnectionImpl`, `StreamImpl` | HTTP/2 codec (nghttp2) |
| `http2/protocol_constraints.h/cc` | `ProtocolConstraints` | HTTP/2 flood mitigation |
| `http2/conn_pool.h/cc` | HTTP/2 pool | HTTP/2 connection pool |
| `http3/conn_pool.h/cc` | `Http3ConnPoolImpl` | HTTP/3 connection pool |

---

**Previous:** [Part 2 — HTTP Connection Management](02-http-connection-management.md)  
**Next:** [Part 4 — Network Connections, Sockets, and I/O](04-network-connections-sockets.md)
