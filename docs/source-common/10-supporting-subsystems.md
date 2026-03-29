# Part 10: Supporting Subsystems — Event Loop, Buffers, Config, Connection Pool, and More

## Overview

This document covers the foundational infrastructure folders that underpin the request path: the event loop (`event/`), buffer management (`buffer/`), xDS configuration (`config/`), the base connection pool (`conn_pool/`), TLS (`tls/`), and other supporting subsystems.

## Event Loop (`source/common/event/`)

### Dispatcher — The Heart of Envoy

Every Envoy worker thread runs a single-threaded event loop powered by libevent. The `Dispatcher` is the interface to this loop.

```mermaid
classDiagram
    class Dispatcher {
        <<interface>>
        +run(type) void
        +exit() void
        +createTimer(callback) TimerPtr
        +createFileEvent(fd, callback, trigger, events) FileEventPtr
        +createServerConnection(socket, transport) ServerConnectionPtr
        +createClientConnection(address, source, transport) ClientConnectionPtr
        +post(callback) void
        +deferredDelete(object) void
    }
    class DispatcherImpl {
        -base_ : event_base (libevent)
        -scheduler_ : Scheduler
        -deferred_delete_list_ : vector
        -post_callbacks_ : list
        +run(type)
        +post(callback)
        +deferredDelete(object)
    }
    class TimerImpl {
        -event_ : raw_event
        -cb_ : TimerCb
        +enableTimer(duration)
        +enableHRTimer(duration)
        +disableTimer()
    }
    class FileEventImpl {
        -event_ : raw_event
        -cb_ : FileReadyCb
        +activate(events)
        +setEnabled(events)
    }

    DispatcherImpl ..|> Dispatcher
    DispatcherImpl --> TimerImpl : "creates"
    DispatcherImpl --> FileEventImpl : "creates"
```

### Event Loop Architecture

```mermaid
graph TD
    subgraph "Worker Thread Event Loop"
        EL["event_base (libevent)"]
        
        subgraph "Registered Events"
            FE1["FileEvent: listen socket\n→ onAccept()"]
            FE2["FileEvent: connection 1\n→ onReadReady(), onWriteReady()"]
            FE3["FileEvent: connection 2\n→ onReadReady(), onWriteReady()"]
            T1["Timer: idle timeout\n→ close connection"]
            T2["Timer: health check\n→ send probe"]
            T3["Timer: retry backoff\n→ retry request"]
        end
        
        Post["Post Queue\n(cross-thread callbacks)"]
        DD["Deferred Delete Queue\n(safe destruction)"]
    end
    
    EL --> FE1
    EL --> FE2
    EL --> FE3
    EL --> T1
    EL --> T2
    EL --> T3
    EL --> Post
    EL --> DD
```

### Deferred Deletion

Objects that might be referenced during event processing can't be deleted immediately. `deferredDelete()` queues them for safe deletion at the end of the current event loop iteration:

```mermaid
sequenceDiagram
    participant Filter as HTTP Filter
    participant AS as ActiveStream
    participant Disp as Dispatcher
    participant DD as Deferred Delete List

    Filter->>AS: sendLocalReply(503)
    AS->>AS: Stream complete
    AS->>Disp: deferredDelete(this)
    Disp->>DD: Add to list
    
    Note over Disp: Current event processing continues
    Note over Disp: Other callbacks may still reference AS
    
    Note over Disp: End of event loop iteration
    Disp->>DD: Clear list → destroy all objects
    Note over AS: ActiveStream safely destroyed
```

## Buffer (`source/common/buffer/`)

### Buffer Architecture

```mermaid
classDiagram
    class Buffer_Instance {
        <<interface>>
        +add(data, size) void
        +drain(size) void
        +move(source) void
        +length() uint64_t
        +toString() string
        +linearize(size) void*
        +getRawSlices() vector~RawSlice~
    }
    class OwnedImpl {
        -slices_ : deque~Slice~
        +add(data, size)
        +drain(size)
        +move(source)
        +read(io_handle, max) IoResult
        +write(io_handle) IoResult
    }
    class WatermarkBuffer {
        -high_watermark_ : uint32_t
        -low_watermark_ : uint32_t
        -above_high_watermark_ : bool
        +setWatermarks(high, low)
    }

    OwnedImpl ..|> Buffer_Instance
    WatermarkBuffer --|> OwnedImpl
```

### Zero-Copy Buffer Design

```mermaid
graph LR
    subgraph "OwnedImpl (Buffer)"
        S1["Slice 1\n[header bytes]"]
        S2["Slice 2\n[body chunk 1]"]
        S3["Slice 3\n[body chunk 2]"]
    end
    
    Note["Slices are reference-counted\nmove() transfers ownership\nNo copying between filters"]
```

### Watermark Flow Control

```mermaid
sequenceDiagram
    participant Producer as Data Producer
    participant WB as WatermarkBuffer
    participant Consumer as Data Consumer

    Producer->>WB: add(data) → length = 50KB
    Note over WB: Below high watermark (64KB)
    
    Producer->>WB: add(data) → length = 70KB
    WB->>WB: Above high watermark!
    WB->>Producer: onAboveWriteBufferHighWatermark()
    Note over Producer: Stop producing (readDisable)
    
    Consumer->>WB: drain(40KB) → length = 30KB
    WB->>WB: Below low watermark (32KB)
    WB->>Producer: onBelowWriteBufferLowWatermark()
    Note over Producer: Resume producing (readEnable)
```

## Base Connection Pool (`source/common/conn_pool/`)

### ConnPoolImplBase

```mermaid
classDiagram
    class ConnPoolImplBase {
        -ready_clients_ : list~ActiveClientPtr~
        -busy_clients_ : list~ActiveClientPtr~
        -connecting_clients_ : list~ActiveClientPtr~
        -early_data_clients_ : list~ActiveClientPtr~
        -pending_streams_ : list~PendingStreamPtr~
        -host_ : HostConstSharedPtr
        -dispatcher_ : Event::Dispatcher
        +newStreamImpl(context, can_send_early) Cancellable*
        +tryCreateNewConnection() ConnectionResult
        +onPoolReady(client, context)
        +onPoolFailure(reason, host)
        +onUpstreamReady()
        +processIdleClient(client)
    }
    class ActiveClient {
        -parent_ : ConnPoolImplBase
        -connect_timer_ : TimerPtr
        -remaining_streams_ : uint64_t
        -stream_count_ : uint64_t
        -state_ : State
        +close()
        +onEvent(event)
    }
    class PendingStream {
        -context_ : AttachContext
        -parent_ : ConnPoolImplBase
        +cancel(reason)
    }

    ConnPoolImplBase *-- ActiveClient
    ConnPoolImplBase *-- PendingStream
```

This base class is shared between HTTP and TCP connection pools. The HTTP-specific pool (`HttpConnPoolImplBase`) adds codec client creation on top of this.

### Pool Decision Flow

```mermaid
flowchart TD
    A["newStreamImpl()"] --> B{Ready client\navailable?}
    B -->|Yes| C["Attach stream to ready client"]
    C --> D["Move client: ready → busy"]
    D --> E["onPoolReady()"]
    
    B -->|No| F{Can create new\nconnection?}
    F -->|"Yes (under limits)"| G["tryCreateNewConnection()"]
    G --> H["Create ActiveClient"]
    H --> I["Start TCP+TLS handshake"]
    I --> J["Add to connecting_clients_"]
    
    F -->|"No (at limit)"| K{Pending queue\nunder limit?}
    K -->|Yes| L["Queue as PendingStream"]
    K -->|No| M["onPoolFailure(Overflow)"]
    
    J --> N["...handshake completes..."]
    N --> O["Move: connecting → ready"]
    O --> P["processIdleClient()"]
    P --> Q{Pending stream?}
    Q -->|Yes| C
    Q -->|No| R["Stay in ready_clients_"]
```

## Configuration / xDS (`source/common/config/`)

### xDS Subscription Architecture

```mermaid
graph TD
    subgraph "xDS Configuration Sources"
        FS["Filesystem\n(static YAML)"]
        REST["REST API\n(polling)"]
        GRPC["gRPC Stream\n(bidirectional)"]
        DELTA["Delta gRPC\n(incremental)"]
    end
    
    subgraph "Subscription Layer"
        SF["SubscriptionFactory"]
        FS --> SF
        REST --> SF
        GRPC --> SF
        DELTA --> SF
        
        SF --> Sub["Subscription\n(watches for config changes)"]
    end
    
    subgraph "Consumers"
        Sub --> CDS["CDS (clusters)"]
        Sub --> EDS["EDS (endpoints)"]
        Sub --> LDS["LDS (listeners)"]
        Sub --> RDS["RDS (routes)"]
        Sub --> SDS["SDS (secrets)"]
    end
```

### gRPC Multiplexing

```mermaid
graph TD
    subgraph "GrpcMuxImpl"
        Conn["gRPC Connection\n(single stream)"]
        
        Conn --> W1["CDS watch"]
        Conn --> W2["EDS watch"]
        Conn --> W3["LDS watch"]
        Conn --> W4["RDS watch"]
    end
    
    Note["All xDS types multiplexed\nover a single gRPC stream\n(ADS - Aggregated Discovery Service)"]
```

## TLS (`source/common/tls/`)

### TLS Architecture

```mermaid
classDiagram
    class ContextImpl {
        -ssl_ctx_ : SSL_CTX
        -cert_chain_ : X509
        -tls_max_version_ : uint16_t
        -cipher_suites_ : string
    }
    class ClientContextImpl {
        +createTransportSocket(options, host)
        -allow_renegotiation_ : bool
        -max_session_keys_ : uint32_t
    }
    class ServerContextImpl {
        +createDownstreamTransportSocket()
        -session_ticket_keys_ : vector
        -ocsp_responses_ : vector
    }
    class SslSocket {
        -ssl_ : bssl::UniquePtr~SSL~
        -info_ : SslConnectionInfoImpl
        +doRead(buffer) IoResult
        +doWrite(buffer, end_stream) IoResult
        +onConnected()
    }

    ClientContextImpl --|> ContextImpl
    ServerContextImpl --|> ContextImpl
    SslSocket --> ContextImpl : "uses"
```

## Stats (`source/common/stats/`)

### Stats Architecture

```mermaid
graph TD
    subgraph "Stats Hierarchy"
        Store["ThreadLocalStoreImpl"]
        Store --> Scope1["Scope: cluster.production-api"]
        Store --> Scope2["Scope: http.ingress_http"]
        Store --> Scope3["Scope: listener.0.0.0.0_8080"]
        
        Scope1 --> C1["Counter: upstream_rq_total"]
        Scope1 --> C2["Counter: upstream_rq_5xx"]
        Scope1 --> G1["Gauge: upstream_cx_active"]
        Scope1 --> H1["Histogram: upstream_rq_time"]
    end
```

| Stat Type | Purpose | Thread Safety |
|-----------|---------|--------------|
| **Counter** | Monotonically increasing count | Atomic increment, TLS merge |
| **Gauge** | Current value (can go up/down) | Atomic set, TLS merge |
| **Histogram** | Distribution of values | Per-thread accumulation, periodic merge |
| **TextReadout** | String value | Mutex-protected |

## Other Important Folders

### `stream_info/` — Per-Request Metadata

```mermaid
graph TD
    SI["StreamInfoImpl"]
    SI --> Timing["Request timing\nstart, end, durations"]
    SI --> Flags["Response flags\n(NR, UF, UO, etc.)"]
    SI --> FS["FilterState\n(typed key-value store)"]
    SI --> Upstream["Upstream info\nhost, cluster, connection"]
    SI --> Downstream["Downstream info\nconnection, SSL, address"]
    SI --> ByteCounters["Bytes sent/received"]
    SI --> Protocol["Protocol (HTTP/1, HTTP/2)"]
    SI --> ResponseCode["Response code"]
```

### `conn_pool/` — Base Connection Pool

| File | Key Classes | Purpose |
|------|-------------|---------|
| `conn_pool_base.h/cc` | `ConnPoolImplBase`, `ActiveClient`, `PendingStream` | Base pool (shared by HTTP and TCP) |

### `tracing/` — Distributed Tracing

```mermaid
graph LR
    HCM["HCM"] --> Tracer["HttpTracerImpl"]
    Tracer --> Span["Span (per request)"]
    Span --> Driver["Tracing Driver\n(Zipkin, Jaeger, etc.)"]
```

### `grpc/` — gRPC Client

```mermaid
graph TD
    subgraph "gRPC Clients"
        Async["AsyncClientImpl\n(Envoy-managed connections)"]
        Google["GoogleAsyncClientImpl\n(gRPC C++ library)"]
    end
    
    Async --> CodecClient
    Google --> gRPCChannel
```

## Complete Folder Summary

```mermaid
graph TD
    subgraph "Critical Path"
        http["http/ — HTTP processing"]
        network["network/ — TCP connections"]
        router["router/ — routing"]
        upstream["upstream/ — clusters"]
        listener_manager["listener_manager/ — listeners"]
        conn_pool["conn_pool/ — base pool"]
    end
    
    subgraph "Infrastructure"
        event["event/ — event loop"]
        buffer["buffer/ — zero-copy buffers"]
        tls["tls/ — TLS/SSL"]
        config["config/ — xDS"]
        stats["stats/ — metrics"]
    end
    
    subgraph "Supporting"
        stream_info["stream_info/ — request metadata"]
        tracing["tracing/ — distributed tracing"]
        grpc["grpc/ — gRPC client"]
        access_log["access_log/ — logging"]
        runtime["runtime/ — feature flags"]
        secret["secret/ — credentials"]
        formatter["formatter/ — log formats"]
        protobuf["protobuf/ — proto utils"]
    end
    
    subgraph "Specialized"
        quic["quic/ — QUIC/HTTP3"]
        tcp_proxy["tcp_proxy/ — L4 proxy"]
        common_util["common/ — utilities"]
    end
    
    http --> network
    http --> event
    http --> buffer
    router --> http
    router --> upstream
    upstream --> network
    upstream --> config
    listener_manager --> network
    conn_pool --> network
    tls --> network
    config --> grpc
```

---

**Previous:** [Part 9 — Hosts, Health Checking, and Outlier Detection](09-upstream-hosts-health.md)  
**Back to:** [Part 1 — Overview](01-overview.md)
