# Part 12: Response Flow — Upstream to Downstream

## Overview

The response flow is the reverse of the request flow. When an upstream service sends a response, the data travels through the upstream codec, the upstream filter chain, the Router filter, the downstream HTTP filter chain (encoder path), and finally the downstream codec to the client. This document traces this complete return journey.

## End-to-End Response Path

```mermaid
graph RL
    subgraph "Upstream"
        Backend["Backend Service"]
        UConn["Upstream Connection"]
        UCodec["Upstream Codec<br/>(decode response)"]
    end
    
    subgraph "Upstream Filter Chain"
        UCF["UpstreamCodecFilter"]
        UBridge["CodecBridge"]
        UFM["UpstreamFilterManager<br/>(encode direction)"]
        URFMC["UpstreamRequest<br/>FilterManagerCallbacks"]
    end
    
    subgraph "Router"
        URDecode["UpstreamRequest::decode[X]()"]
        RouterCB["Router::onUpstream[X]()"]
    end
    
    subgraph "Downstream Filter Chain"
        HFM["HTTP FilterManager<br/>(encode path, reverse order)"]
        FC["Filter C"]
        FB["Filter B"]
        FA["Filter A"]
    end
    
    subgraph "Downstream"
        DCodec["Downstream Codec<br/>(encode response)"]
        DConn["Downstream Connection"]
        Client["Client"]
    end
    
    Backend --> UConn --> UCodec --> UCF
    UCF --> UBridge --> UFM --> URFMC
    URFMC --> URDecode --> RouterCB
    RouterCB --> HFM --> FC --> FB --> FA
    FA --> DCodec --> DConn --> Client
```

## Step 1: Upstream Bytes → Upstream Codec

When the upstream service sends response bytes:

```mermaid
sequenceDiagram
    participant Backend as Backend Service
    participant UConn as Upstream Connection
    participant UTS as Transport Socket
    participant UCC as CodecClient
    participant UCodec as Upstream HTTP Codec

    Backend->>UConn: Response bytes (encrypted)
    UConn->>UTS: doRead(buffer)
    UTS-->>UConn: Decrypted bytes
    UConn->>UCC: onData(buffer)
    UCC->>UCodec: dispatch(buffer)
    Note over UCodec: Parse response headers/body/trailers
```

The upstream `CodecClient` acts as the network read filter for the upstream connection. When bytes arrive, the HTTP codec parses them into structured response data.

## Step 2: Codec → UpstreamCodecFilter → CodecBridge

```mermaid
sequenceDiagram
    participant UCodec as Upstream Codec
    participant RD as ResponseDecoder (CodecBridge)
    participant UCF as UpstreamCodecFilter
    participant UFM as UpstreamFilterManager

    Note over UCodec: Response headers parsed
    UCodec->>RD: decodeHeaders(response_headers, end_stream)
    RD->>UCF: onUpstreamHeaders(headers, end_stream)
    UCF->>UFM: decoder_callbacks_->encodeHeaders(headers, end_stream)
    Note over UFM: Runs upstream HTTP filters (encode direction)
    
    UCodec->>RD: decodeData(body, end_stream)
    RD->>UCF: onUpstreamData(data, end_stream)
    UCF->>UFM: decoder_callbacks_->encodeData(data, end_stream)
    
    UCodec->>RD: decodeTrailers(trailers)
    RD->>UCF: onUpstreamTrailers(trailers)
    UCF->>UFM: decoder_callbacks_->encodeTrailers(trailers)
```

### CodecBridge

The `CodecBridge` (`source/common/router/upstream_codec_filter.h`) implements `ResponseDecoder` and bridges between the upstream codec and the upstream filter chain:

```mermaid
classDiagram
    class CodecBridge {
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        -filter_ : UpstreamCodecFilter
    }
    class UpstreamCodecFilter {
        +onUpstreamHeaders()
        +onUpstreamData()
        +onUpstreamTrailers()
        -decoder_callbacks_
    }
    class ResponseDecoder {
        <<interface>>
        +decodeHeaders()
        +decodeData()
        +decodeTrailers()
    }

    CodecBridge ..|> ResponseDecoder
    CodecBridge --> UpstreamCodecFilter
```

## Step 3: Upstream Filter Manager → UpstreamRequest

After upstream filters process the response, `UpstreamRequestFilterManagerCallbacks` receives it:

```mermaid
sequenceDiagram
    participant UFM as UpstreamFilterManager
    participant URFMC as FilterManagerCallbacks
    participant UR as UpstreamRequest
    participant Router as Router Filter

    UFM->>URFMC: encodeHeaders(response_headers, end_stream)
    URFMC->>UR: decodeHeaders(headers, end_stream)
    UR->>Router: parent_.onUpstreamHeaders(headers, end_stream, was_grpc)

    UFM->>URFMC: encodeData(body, end_stream)
    URFMC->>UR: decodeData(data, end_stream)
    UR->>Router: parent_.onUpstreamData(data, end_stream)

    UFM->>URFMC: encodeTrailers(trailers)
    URFMC->>UR: decodeTrailers(trailers)
    UR->>Router: parent_.onUpstreamTrailers(trailers)
```

```
File: source/common/router/upstream_request.h (lines 248-258)

UpstreamRequestFilterManagerCallbacks:
    encodeHeaders() → upstream_request_.decodeHeaders()
    encodeData()    → upstream_request_.decodeData()
    encodeTrailers() → upstream_request_.decodeTrailers()
```

## Step 4: Router → Downstream HTTP Filter Chain (Encode Path)

The Router filter receives the response and forwards it to the downstream filter chain:

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant DSFC as Downstream FilterCallbacks
    participant HFM as HTTP FilterManager (encode)
    participant FC as Filter C (encode)
    participant FB as Filter B (encode)
    participant FA as Filter A (encode)

    Router->>DSFC: callbacks_->encodeHeaders(headers, end_stream)
    DSFC->>HFM: encodeHeaders(headers, end_stream)
    
    Note over HFM: Reverse iteration
    HFM->>FC: encodeHeaders(headers, end_stream)
    FC-->>HFM: Continue
    HFM->>FB: encodeHeaders(headers, end_stream)
    FB-->>HFM: Continue
    HFM->>FA: encodeHeaders(headers, end_stream)
    FA-->>HFM: Continue
    
    HFM->>HFM: filter_manager_callbacks_.encodeHeaders()
    Note over HFM: → ActiveStream → ResponseEncoder → Codec
```

### Encoder Filter Iteration (Reverse Order)

```mermaid
graph RL
    subgraph "Encode Path (reverse order)"
        Router["Router<br/>(not an encoder filter)"] --> FC["Filter C<br/>encodeHeaders()"]
        FC --> FB["Filter B<br/>encodeHeaders()"]
        FB --> FA["Filter A<br/>encodeHeaders()"]
        FA --> Codec["Downstream Codec"]
    end
```

```
File: source/common/http/filter_manager.cc (lines 754-831)

encodeHeaders(headers, end_stream):
    For each encoder filter (REVERSE order):
        status = filter->handle_->encodeHeaders(headers, end_stream)
        If StopIteration → pause, wait for continueEncoding()
        If Continue → next filter
    When all done:
        filter_manager_callbacks_.encodeHeaders(headers, end_stream)
```

## Step 5: ActiveStream → ResponseEncoder → Downstream Codec

```mermaid
sequenceDiagram
    participant HFM as HTTP FilterManager
    participant AS as ActiveStream
    participant RE as ResponseEncoder
    participant DCodec as Downstream Codec
    participant DConn as Downstream Connection

    HFM->>AS: filter_manager_callbacks_.encodeHeaders(headers, end_stream)
    AS->>RE: response_encoder_->encodeHeaders(headers, end_stream)
    RE->>DCodec: Serialize response headers
    DCodec->>DConn: connection_.write(serialized_bytes)
    
    HFM->>AS: filter_manager_callbacks_.encodeData(data, end_stream)
    AS->>RE: response_encoder_->encodeData(data, end_stream)
    RE->>DCodec: Serialize response body
    DCodec->>DConn: connection_.write(serialized_bytes)
    
    DConn->>DConn: transport_socket_->doWrite(buffer)
    Note over DConn: TLS encryption (if configured)
    DConn->>DConn: Send to client socket
```

## Step 6: Stream Completion

After the response is fully sent:

```mermaid
flowchart TD
    A["Last encodeData or encodeTrailers with end_stream=true"] --> B["ResponseEncoder signals stream complete"]
    B --> C["ActiveStream::onEncodeComplete()"]
    C --> D["Log access log entries"]
    D --> E["FilterManager::onDestroy()"]
    E --> F["Call onDestroy() on every filter"]
    F --> G["ActiveStream removed from streams_ list"]
    G --> H["Deferred deletion"]
    
    H --> I{HTTP/1.1?}
    I -->|Yes| J{Keep-alive?}
    J -->|Yes| K["Ready for next request"]
    J -->|No| L["Close connection"]
    I -->|HTTP/2| M["Stream freed, connection continues"]
```

## Local Reply Flow

When a filter generates a response locally (without going upstream):

```mermaid
sequenceDiagram
    participant AuthFilter as Auth Filter (decode)
    participant FM as FilterManager
    participant EncChain as Encoder Filter Chain
    participant AS as ActiveStream
    participant RE as ResponseEncoder

    AuthFilter->>FM: sendLocalReply(403, "Forbidden", ...)
    FM->>FM: Build 403 response headers
    FM->>EncChain: encodeHeaders(403_headers, end_stream=false)
    Note over EncChain: Encoder filters run in reverse
    EncChain->>AS: encodeHeaders()
    AS->>RE: encodeHeaders(403_headers)
    
    FM->>EncChain: encodeData(body, end_stream=true)
    EncChain->>AS: encodeData()
    AS->>RE: encodeData(body, end_stream=true)
    
    Note over FM: Decode iteration stops<br/>Router never sees request
```

## Complete Data Flow Diagram

```mermaid
graph TB
    subgraph "Request Path (→)"
        C1["Client"] -->|"1"| DConn1["Downstream Connection"]
        DConn1 -->|"2"| DTS1["Transport Socket (decrypt)"]
        DTS1 -->|"3"| NFM1["Network Filters"]
        NFM1 -->|"4"| HCM1["HCM onData()"]
        HCM1 -->|"5"| DCodec1["Downstream Codec (parse)"]
        DCodec1 -->|"6"| AS1["ActiveStream"]
        AS1 -->|"7"| DHFM1["HTTP Filters (decode →)"]
        DHFM1 -->|"8"| Router1["Router"]
        Router1 -->|"9"| CP1["Connection Pool"]
        CP1 -->|"10"| UConn1["Upstream Connection"]
        UConn1 -->|"11"| UTS1["Transport Socket (encrypt)"]
        UTS1 -->|"12"| Backend["Backend"]
    end
    
    subgraph "Response Path (←)"
        Backend -->|"13"| UTS2["Transport Socket (decrypt)"]
        UTS2 -->|"14"| UCodec2["Upstream Codec (parse)"]
        UCodec2 -->|"15"| UCF2["UpstreamCodecFilter"]
        UCF2 -->|"16"| UR2["UpstreamRequest"]
        UR2 -->|"17"| Router2["Router"]
        Router2 -->|"18"| DHFM2["HTTP Filters (← encode)"]
        DHFM2 -->|"19"| AS2["ActiveStream"]
        AS2 -->|"20"| DCodec2["Downstream Codec (serialize)"]
        DCodec2 -->|"21"| DTS2["Transport Socket (encrypt)"]
        DTS2 -->|"22"| C2["Client"]
    end
    
    style Router1 fill:#ffcdd2
    style Router2 fill:#ffcdd2
```

## Watermarks and Flow Control

Response data is flow-controlled through the entire path:

```mermaid
flowchart TD
    A["Upstream sends data fast"] --> B["Upstream codec buffers data"]
    B --> C{Downstream write buffer high?}
    C -->|Yes| D["Downstream connection raises<br/>high watermark"]
    D --> E["ActiveStream::onAboveWriteBufferHighWatermark()"]
    E --> F["readDisable(true) on upstream stream"]
    F --> G["Upstream stops reading"]
    
    C -->|No| H["Data flows to downstream codec"]
    
    G --> I["Downstream buffer drains"]
    I --> J["Low watermark reached"]
    J --> K["readDisable(false) on upstream stream"]
    K --> L["Resume reading from upstream"]
```

## Error Handling in Response Path

```mermaid
flowchart TD
    A["Upstream response"] --> B{Response type}
    
    B -->|"Normal 2xx/3xx/4xx"| C["Forward through encode path"]
    B -->|"5xx (retryable)"| D{Retry policy allows?}
    D -->|Yes| E["Don't send to downstream yet"]
    E --> F["Create new UpstreamRequest"]
    F --> G["Retry to same/different host"]
    D -->|No| C
    
    B -->|"Connection reset"| H{Retry on reset?}
    H -->|Yes| F
    H -->|No| I["sendLocalReply(502/503)"]
    
    B -->|"Timeout"| J{Retry on timeout?}
    J -->|Yes| F
    J -->|No| K["sendLocalReply(504)"]
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `source/common/router/upstream_codec_filter.h` | 26-74 | `CodecBridge`, `UpstreamCodecFilter` |
| `source/common/router/upstream_request.h` | 248-258 | `UpstreamRequestFilterManagerCallbacks` |
| `source/common/router/router.h` | 416-421 | `onUpstreamHeaders/Data/Trailers()` |
| `source/common/http/filter_manager.cc` | 754-831 | `encodeHeaders()` iteration |
| `source/common/http/filter_manager.cc` | 877-941 | `encodeData()` iteration |
| `source/common/http/conn_manager_impl.h` | 123-220 | `ActiveStream` as `FilterManagerCallbacks` |
| `envoy/http/codec.h` | 145-210 | `ResponseEncoder` interface |
| `source/common/http/filter_manager.h` | 771-1084 | `DownstreamFilterManager` — local reply, access log |

---

**Previous:** [Part 11 — Connection Pools](11-connection-pools.md)  
**Back to:** [Part 1 — Overview](01-overview.md)
