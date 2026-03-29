# CodecClient

**File:** `source/common/http/codec_client.h` / `.cc`  
**Size:** ~12 KB header, ~13 KB implementation  
**Namespace:** `Envoy::Http`

## Overview

`CodecClient` is Envoy's **upstream HTTP client** abstraction. It wraps a single `Network::Connection` with an HTTP codec on top (HTTP/1.1, HTTP/2, or HTTP/3) and presents a uniform stream-creation API to the connection pool layer. The pool creates one `CodecClient` per upstream TCP/QUIC connection.

## Class Hierarchy

```mermaid
classDiagram
    class CodecClient {
        +newStream(response_decoder): RequestEncoder
        +newStreamHandle(response_decoder_handle): RequestEncoderPtr
        +close(type)
        +goAway()
        +protocol(): Protocol
        +numActiveRequests(): size_t
        +id(): uint64_t
        -connection_: Network::ClientConnectionPtr
        -codec_: ClientConnectionPtr
        -active_requests_: list~ActiveRequest~
        -connect_timer_: TimerPtr
    }

    class HttpConnectionCallbacks["Http::ConnectionCallbacks"] {
        <<interface>>
        +onGoAway(error_code)
    }

    class NetworkConnectionCallbacks["Network::ConnectionCallbacks"] {
        <<interface>>
        +onEvent(event)
        +onAboveWriteBufferHighWatermark()
        +onBelowWriteBufferLowWatermark()
    }

    class DeferredDeletable["Event::DeferredDeletable"] {
        <<interface>>
    }

    class CodecClientCallbacks {
        <<interface>>
        +onStreamDestroy()
        +onStreamReset(reason)
        +onStreamPreDecodeComplete()
    }

    HttpConnectionCallbacks <|-- CodecClient
    NetworkConnectionCallbacks <|-- CodecClient
    DeferredDeletable <|-- CodecClient
    CodecClient --> CodecClientCallbacks : notifies
```

## Inner Type: `ActiveRequest`

Each in-flight HTTP request is tracked by an `ActiveRequest` object:

```mermaid
classDiagram
    class ActiveRequest {
        +response_decoder_: ResponseDecoder*
        +encoder_: RequestEncoderWrapper
        +decode_complete_: bool
        +encode_complete_: bool
        +reset_called_: bool
        +onPreDecodeComplete()
        +onDecodeComplete()
    }
    class ResponseDecoderWrapper {
        <<wraps>>
    }
    class RequestEncoderWrapper {
        <<wraps>>
    }
    ActiveRequest --> ResponseDecoderWrapper
    ActiveRequest --> RequestEncoderWrapper
```

## Connection Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Connecting : CodecClient constructed
    Connecting --> Connected : Network::ConnectionEvent::Connected
    Connecting --> ConnectTimeout : connect_timer_ fires
    Connected --> Active : newStream() called
    Active --> Active : more streams (H2/H3 only)
    Active --> GoingAway : onGoAway() received
    GoingAway --> Closed : all active_requests_ drained
    Connected --> Closed : Network::ConnectionEvent::RemoteClose
    Connected --> Closed : close() called
    ConnectTimeout --> [*] : connection destroyed
    Closed --> [*] : DeferredDelete
```

## Stream Creation Flow

```mermaid
sequenceDiagram
    participant Pool as ConnPool::ActiveClient
    participant CC as CodecClient
    participant Codec as ClientConnection (H1/H2/H3)
    participant Net as Network::Connection

    Pool->>CC: newStream(response_decoder)
    CC->>CC: create ActiveRequest
    CC->>Codec: newStream(active_request)
    Codec->>Net: (establishes stream framing)
    Codec-->>CC: RequestEncoder&
    CC-->>Pool: RequestEncoder& (caller encodes request)
    Pool->>CC: RequestEncoder.encodeHeaders(headers)
    Net-->>Codec: response bytes
    Codec->>CC: ActiveRequest.decodeHeaders(headers)
    CC->>Pool: response_decoder.decodeHeaders(headers)
    Codec->>CC: onDecodeComplete()
    CC->>CC: removeRequest(active_request)
    CC->>Pool.callbacks_: onStreamDestroy()
```

## Protocol-Specific Behavior

| Aspect | HTTP/1.1 | HTTP/2 | HTTP/3 (QUIC) |
|--------|----------|--------|---------------|
| Concurrency | 1 stream at a time (no pipelining enforced by CodecClient) | Multiple streams per connection | Multiple streams per QUIC connection |
| GOAWAY | N/A (no concept) | `onGoAway()` — stops new streams | QUIC CONNECTION_CLOSE |
| Half-close | Supported | Not applicable | Not applicable |
| Connect timeout | `connect_timer_` | `connect_timer_` | QUIC handshake timeout |
| Stream reset | `onStreamReset(reason)` | RST_STREAM | RESET_STREAM |

## Watermark / Backpressure

```mermaid
flowchart TD
    NetBuf["Network Write Buffer"] -->|bytes > high watermark| CC_high["CodecClient\nonAboveWriteBufferHighWatermark()"]
    CC_high --> Codec_high["ClientCodec\nonUnderlyingConnectionAboveWriteBufferHighWatermark()"]
    Codec_high --> Stream_high["RequestEncoder\nonAboveWriteBufferHighWatermark()"]
    Stream_high --> Pool_pause["Pool pauses new encodes"]

    NetBuf -->|bytes < low watermark| CC_low["CodecClient\nonBelowWriteBufferLowWatermark()"]
    CC_low --> Codec_low["ClientCodec\nonUnderlyingConnectionBelowWriteBufferLowWatermark()"]
    Codec_low --> Stream_low["RequestEncoder\nonBelowWriteBufferLowWatermark()"]
    Stream_low --> Pool_resume["Pool resumes encodes"]
```

## Error Handling

| Event | Handler | Outcome |
|-------|---------|---------|
| Connect timeout | `connect_timer_` callback | Close connection, notify pool |
| Protocol error | `onEvent(RemoteClose)` | `onStreamReset(RemoteReset)` per active request |
| GOAWAY (H2) | `onGoAway(error_code)` | Stop accepting new streams; drain existing |
| Stream reset | `ActiveRequest::onResetStream(reason)` | Notify `CodecClientCallbacks::onStreamReset()` |
| Codec error (absl::Status) | Propagated from codec dispatch | Connection closed |

## `CodecClientProd`

The production subclass `CodecClientProd` extends `CodecClient` and instantiates the real codec via a codec factory. It is used in all non-test contexts.

```mermaid
classDiagram
    class CodecClientProd {
        +CodecClientProd(type, connection, host, dispatcher, ...)
    }
    CodecClient <|-- CodecClientProd
```

## Key Configuration

| Parameter | Purpose |
|-----------|---------|
| `CodecType` | H1 / H2 / H3 — determines which codec implementation is used |
| `connect_timeout` | Duration for `connect_timer_` |
| `max_response_headers_count` | Passed to codec to limit response header count |
| `header_validator_factory` | Optional UHV (Universal Header Validator) for protocol compliance |
