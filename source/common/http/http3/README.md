# Envoy HTTP/3 Codec — Documentation Index

**Source folder:** `source/common/http/http3/`

> **Compile guard:** All HTTP/3 code requires `ENVOY_ENABLE_QUIC`. The folder will not build
> with QUIC disabled.

---

## File → Doc Map

| Source File(s) | Doc | Description |
|---|---|---|
| `codec_stats.h` | [codec_stats.md](./codec_stats.md) | All `http3.*` counters, QUIC version tracking, comparison with H2 stats |
| `conn_pool.h` / `conn_pool.cc` | [conn_pool.md](./conn_pool.md) | `ActiveClient`, `Http3ConnPoolImpl`, QUIC capacity model, 0-RTT, connection migration |

> HTTP/3 codec implementation (stream encoding/decoding) lives in
> `source/common/quic/` rather than here — specifically
> `envoy_quic_client_stream.h/cc` and `envoy_quic_server_stream.h/cc`.

---

## Component Relationships

```mermaid
flowchart TB
    subgraph http3 pool
        H3Pool[Http3ConnPoolImpl\nconn_pool.h] --> AC[ActiveClient\nconn_pool.h]
        H3Pool --> PQI[PersistentQuicInfoImpl\nquic_info_]
        H3Pool --> CB[PoolConnectResultCallback\nhandshake result]
        AC --> CC[CodecClientProd\nCodecType::HTTP3]
    end

    subgraph quic layer
        CC --> QNC[EnvoyQuicClientConnection\ncreateQuicNetworkConnection]
        QNC --> QUIC[QUIC transport\nQUICHE library]
        QNC --> CIG[DeterministicConnectionIdGenerator]
    end

    subgraph pool base
        FCP[FixedHttpConnPoolImpl] --> H3Pool
    end

    subgraph stats
        Stats[CodecStats\ncodec_stats.h] -.->|http3.*| Scope[Stats::Scope]
    end

    Grid[ConnPoolGrid\nH3+H2+H1 fallback] --> H3Pool
    Grid --> CB
    CM[Cluster Manager] --> Grid
```

---

## Key Differences from HTTP/1 and HTTP/2

| Property | HTTP/1 | HTTP/2 | HTTP/3 |
|---|---|---|---|
| Transport | TCP | TCP | **QUIC (UDP)** |
| TLS | Optional (TCP-level) | Required (TLS 1.2+) | **Built-in (TLS 1.3 only)** |
| Stream multiplexing | No (1 per conn) | Yes (SETTINGS negotiated) | Yes (**MAX_STREAMS** frame) |
| Head-of-line blocking | Yes | At TCP level | **None** (per-stream loss recovery) |
| 0-RTT resumption | No | No | **Yes** |
| Capacity restored on stream close | N/A | Yes | **No** |
| `trackStreamCapacity()` | — | `true` | **`false`** |
| Connection migration | No | No | **Yes** (on network change) |
| Codec location | `http/http1/` | `http/http2/` | **`quic/`** |
| Pool implementation | `FixedHttpConnPoolImpl` | `HttpConnPoolImplBase` | **`FixedHttpConnPoolImpl`** |
| METADATA frames | No | Yes (extension) | **No** |

---

## HTTP/3 Connection Lifecycle

```mermaid
sequenceDiagram
    participant Grid as ConnPoolGrid
    participant Pool as Http3ConnPoolImpl
    participant AC as ActiveClient
    participant CC as CodecClientProd
    participant QUIC as QUIC Transport

    Grid->>Pool: newStream(response_decoder, callbacks)
    Pool->>AC: allocateConnPool → create ActiveClient
    AC->>CC: CodecClientProd(HTTP3, connection, auto_connect=false)
    AC->>AC: schedule async_connect_callback_ (next event loop)

    note over AC,QUIC: Next event loop iteration
    AC->>CC: codec_client_->connect()
    CC->>QUIC: QUIC handshake begins

    alt 0-RTT session available
        QUIC-->>AC: ReadyForEarlyData state
        AC->>Pool: onUpstreamReadyForEarlyData()
        Pool->>Grid: dispatch early data streams
    end

    QUIC-->>AC: Full handshake complete
    AC->>Pool: onConnected()
    Pool->>Grid: connect_callback_->onHandshakeComplete()
    QUIC-->>AC: MAX_STREAMS frame received
    AC->>AC: updateCapacity(num_streams)
    AC->>Pool: incrConnectingAndConnectedStreamCapacity(delta)

    loop concurrent requests (up to quiche_capacity_)
        Pool->>AC: newStreamEncoder(response_decoder)
        AC->>AC: quiche_capacity_--
        AC->>CC: newStream()
    end

    alt Network change (migration enabled)
        QUIC-->>AC: network path change detected
        AC->>QUIC: migrate to new path
        note over Pool: drainConnections() skipped
    end
```

---

## 0-RTT vs Full Handshake

```mermaid
flowchart TD
    A[New QUIC connection] --> B{Prior session\nticket cached?}
    B -->|Yes| C[0-RTT: send early data immediately\nState = ReadyForEarlyData]
    B -->|No| D[1-RTT: wait for handshake\nState = Connecting]
    C --> E{Server accepts\n0-RTT?}
    E -->|Yes| F[Full handshake completes\nState = Ready]
    E -->|No| G[0-RTT rejected\nonZeroRttHandshakeFailed\nState = Busy]
    D --> F
```
