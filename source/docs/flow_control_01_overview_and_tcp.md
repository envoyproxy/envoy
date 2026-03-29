# Envoy Flow Control — Part 1: Overview and TCP

## Watermark Concept

All buffer limits in Envoy are **soft limits**. Each buffer has a high and low watermark.
When a buffer exceeds the high watermark, a callback fires to pause the data source.
When it drains below the low watermark (~50% of high), the source is resumed.

The back-off may be:
- **Immediate** — stop reading from a socket (`readDisable(true)` on a TCP connection)
- **Gradual** — stop sending HTTP/2 window updates, causing the peer to eventually stop

The low watermark is intentionally set at about **half the high watermark** to avoid
thrashing — rapidly toggling between paused and resumed states as data straddles the limit.

The callback contract:
- `onAboveWriteBufferHighWatermark()` — buffer exceeded the limit; pause the source
- `onBelowWriteBufferLowWatermark()` — buffer drained sufficiently; resume the source 

```mermaid
flowchart LR
    SRC[Data Source] -->|writes data| BUF[Buffer]
    BUF -->|exceeds high watermark| HWM[High Watermark CB\nonAboveWriteBufferHighWatermark]
    HWM -->|readDisable / pause window updates| SRC
    BUF -->|drains below low watermark| LWM[Low Watermark CB\nonBelowWriteBufferLowWatermark]
    LWM -->|readEnable / resume| SRC
```

---

## TCP Flow Control

Handled by coordination between `Network::ConnectionImpl::write_buffer_` and `Network::TcpProxy`.

Each `Network::ConnectionImpl` has a `write_buffer_` that accumulates data to be written to the
underlying socket. When this buffer grows beyond the high watermark, it notifies registered
`Network::ConnectionCallbacks`. The `TcpProxy` filter subscribes to these callbacks for both its
downstream and upstream connections, and uses them to gate reads on the opposite side.

- **`TcpProxy::DownstreamCallbacks`** — reacts to the *downstream* write buffer; controls the *upstream* read side
- **`TcpProxy::UpstreamCallbacks`** — reacts to the *upstream* write buffer; controls the *downstream* read side

This cross-connection gating is the key mechanism: when data cannot be flushed to one side fast
enough, reads from the other side are paused to prevent unbounded buffering.

### Downstream Backpressure (downstream buffer fills up → pause upstream reads)

This happens when the downstream client is slow to consume data. Data sent from upstream to the
downstream write buffer accumulates. Once the buffer exceeds the high watermark, upstream reads
are disabled so no more data is pulled from the upstream server until the downstream client catches up.

```mermaid
sequenceDiagram
    participant US as Upstream Connection
    participant DSBuf as Downstream write_buffer_
    participant DSCb as TcpProxy::DownstreamCallbacks
    participant DS as Downstream Connection

    US->>DSBuf: write data
    DSBuf->>DSBuf: exceeds high watermark
    DSBuf->>DSCb: onAboveWriteBufferHighWatermark()
    DSCb->>US: readDisable(true)
    Note over US: Upstream reads paused

    DSBuf->>DSBuf: drains below low watermark
    DSBuf->>DSCb: onBelowWriteBufferLowWatermark()
    DSCb->>US: readDisable(false)
    Note over US: Upstream reads resumed
```

### Upstream Backpressure (upstream buffer fills up → pause downstream reads)

This happens when the upstream server is slow to consume data sent from the downstream client
(e.g., a large request body). The upstream write buffer fills, downstream reads are disabled,
and TCP back-pressure propagates to the downstream client's send window.

```mermaid
sequenceDiagram
    participant DS as Downstream Connection
    participant USBuf as Upstream write_buffer_
    participant USCb as TcpProxy::UpstreamCallbacks
    participant US as Upstream Connection

    DS->>USBuf: write data
    USBuf->>USBuf: exceeds high watermark
    USBuf->>USCb: onAboveWriteBufferHighWatermark()
    USCb->>DS: readDisable(true)
    Note over DS: Downstream reads paused

    USBuf->>USBuf: drains below low watermark
    USBuf->>USCb: onBelowWriteBufferLowWatermark()
    USCb->>DS: readDisable(false)
    Note over DS: Downstream reads resumed
```
