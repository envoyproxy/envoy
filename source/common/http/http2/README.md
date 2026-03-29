# Envoy HTTP/2 Codec — Documentation Index

**Source folder:** `source/common/http/http2/`

---

## File → Doc Map

| Source File(s) | Doc | Description |
|---|---|---|
| `codec_impl.h` / `codec_impl.cc` | [codec_impl.md](./codec_impl.md) | Full codec class hierarchy — `ConnectionImpl`, `StreamImpl`, `ClientStreamImpl`, `ServerStreamImpl`, `Http2Visitor` |
| `protocol_constraints.h` / `.cc` | [protocol_constraints.md](./protocol_constraints.md) | Flood detection and per-connection security limits |
| `codec_stats.h` | [codec_stats.md](./codec_stats.md) | All `http2.*` counters and gauges |
| `conn_pool.h` / `conn_pool.cc` | [conn_pool.md](./conn_pool.md) | HTTP/2 multiplexed connection pool and stream limit calculation |
| `metadata_encoder.h` / `metadata_decoder.h` | [metadata_encoder_decoder.md](./metadata_encoder_decoder.md) | Non-standard METADATA frame extension — encoding and decoding |

---

## Component Relationships

```mermaid
flowchart TB
    subgraph Codec
        SCI[ServerConnectionImpl] --> CI[ConnectionImpl]
        CCI[ClientConnectionImpl] --> CI
        CI --> V[Http2Visitor\nprotocol event handler]
        CI --> Adapter["Http2Adapter\n(OgHttp2 or nghttp2)"]
        Adapter --> V
        V --> CI
        CI --> SS[ServerStreamImpl]
        CI --> CS[ClientStreamImpl]
        SS --> SI[StreamImpl]
        CS --> SI
        SI --> PRD[pending_recv_data_\nWatermarkBuffer]
        SI --> PSD[pending_send_data_\nWatermarkBuffer]
        SI --> MetaDec[MetadataDecoder]
        SI --> MetaEnc[NewMetadataEncoder]
        CI --> PC[ProtocolConstraints]
        CI --> Stats[CodecStats]
    end

    subgraph Pool
        AC[Http2::ActiveClient\nconn_pool.h] --> CCI
        AC --> Cache[HttpServerPropertiesCache\nAlt-Svc / SETTINGS cache]
    end

    NC[Network::Connection] --> CI
    HCM[ConnectionManagerImpl] --> SCI
    ConnPool[ConnPoolImplBase] --> AC
```

---

## Key Design Properties

- **Multiplexed streams** — many `StreamImpl`s share a single `Network::Connection`; stream IDs allocated by the adapter
- **Dual adapter support** — `OgHttp2Adapter` (QUICHE, default) or `nghttp2` (legacy, compile-time flag `ENVOY_NGHTTP2`), selected via runtime flag `http2_use_oghttp2`
- **Per-stream buffers** — each stream has `pending_recv_data_` and `pending_send_data_` as `WatermarkBuffer`s; watermark callbacks drive flow control (see `source/docs/flow_control_02_http2_filters.md`)
- **`readDisable` reference counting** — `read_disable_count_` tracks multiple independent callers; stream only resumes when all callers release
- **Deferred processing** — opt-in feature that buffers body/trailers inside the codec when a stream is read-disabled; drained via `process_buffered_data_callback_`
- **Flood protection** — `ProtocolConstraints` enforces per-connection limits on outbound frames and inbound PRIORITY/WINDOW_UPDATE/empty frames
- **METADATA extension** — custom frame type `0x4D` for inter-filter key-value passing; requires `allow_metadata = true`
- **LRU stream ordering** — with deferred processing, `active_streams_` is LRU; low watermark notifications prefer least-recently-written streams

---

## H2 Stream Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Open : HEADERS frame (server) / newStream() (client)
    Open --> HalfClosedLocal : local END_STREAM sent
    Open --> HalfClosedRemote : remote END_STREAM received
    HalfClosedLocal --> Closed : remote END_STREAM received
    HalfClosedRemote --> Closed : local END_STREAM sent
    Open --> Closed : RST_STREAM sent or received
    HalfClosedLocal --> Closed : RST_STREAM
    HalfClosedRemote --> Closed : RST_STREAM
    Closed --> [*] : stream destroyed (deferred delete)
```

## Adapter Selection

```mermaid
flowchart TD
    A[Http2ProtocolOptions] --> B{http2_use_oghttp2\nruntime flag?}
    B -->|true - default| C[OgHttp2Adapter\nQUICHE-based]
    B -->|false| D[nghttp2\nrequires ENVOY_NGHTTP2 compile flag]
    C --> E[Http2Adapter interface]
    D --> E
    E --> F[Http2Visitor receives all protocol events]
    F --> G[ConnectionImpl handles stream lifecycle]
```
