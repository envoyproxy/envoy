# Part 11: Connection Pools and Upstream Connections

## Overview

Connection pools manage the lifecycle of upstream connections. They maintain pools of ready, busy, and connecting connections to upstream hosts, multiplex streams over HTTP/2 connections, handle connection draining, and implement preconnect logic. The pool is the interface between the Router filter and the actual upstream network.

## Connection Pool Architecture

```mermaid
graph TD
    subgraph "Per-Cluster, Per-Host Connection Pool"
        Pool["HttpConnPoolImplBase"]
        
        subgraph "Client Lists"
            Ready["ready_clients_<br/>(idle, ready for streams)"]
            Busy["busy_clients_<br/>(active streams)"]
            Connecting["connecting_clients_<br/>(TCP/TLS handshake in progress)"]
        end
        
        Pending["pending_streams_<br/>(waiting for a connection)"]
        
        Pool --> Ready
        Pool --> Busy
        Pool --> Connecting
        Pool --> Pending
    end
    
    Router["Router Filter"] -->|"newStream()"| Pool
    Ready -->|"attach stream"| ActiveClient["ActiveClient<br/>(CodecClient wrapper)"]
    Connecting -->|"when ready"| Ready
```

## Class Hierarchy

```mermaid
classDiagram
    class ConnPoolImplBase {
        +newStreamImpl(context, can_send_early) Cancellable
        +tryCreateNewConnection() ConnectionResult
        +onPoolReady(client, context)
        +onPoolFailure(reason)
        -ready_clients_ : list~ActiveClientPtr~
        -busy_clients_ : list~ActiveClientPtr~
        -connecting_clients_ : list~ActiveClientPtr~
        -pending_streams_ : list~PendingStreamPtr~
    }
    class HttpConnPoolImplBase {
        +newStream(ResponseDecoder, Callbacks) Cancellable
        +onPoolReady(client, context)
        -host_ : HostConstSharedPtr
    }
    class ActiveClient {
        +newStreamEncoder(ResponseDecoder) RequestEncoder
        +remainingStreams() uint64_t
        +codec_client_ : CodecClientPtr
        -stream_count_ : uint64_t
        -state_ : State
    }
    class CodecClient {
        +newStream(ResponseDecoder) RequestEncoder
        +close()
        -connection_ : Network::ClientConnection
        -codec_ : ClientConnection (HTTP codec)
    }

    HttpConnPoolImplBase --|> ConnPoolImplBase
    ConnPoolImplBase --> ActiveClient : "manages"
    ActiveClient --> CodecClient : "owns"
```

**Location:** `source/common/conn_pool/conn_pool_base.h`, `source/common/http/conn_pool_base.h`

## Connection Pool Flow

### newStream() — Requesting a Connection

```mermaid
flowchart TD
    A["Router: conn_pool->newStream(decoder, callbacks)"] --> B["HttpConnPoolImplBase::newStream()"]
    B --> C["Create HttpPendingStream"]
    C --> D["newStreamImpl(context)"]
    D --> E{Ready client available?}
    
    E -->|Yes| F["attachToClient(ready_client)"]
    F --> G["onPoolReady(client, context)"]
    G --> H["encoder = client.newStreamEncoder(decoder)"]
    H --> I["callbacks.onPoolReady(encoder, host)"]
    
    E -->|No| J{Can create new connection?}
    J -->|Yes| K["tryCreateNewConnection()"]
    K --> L["createCodecClient(host)"]
    L --> M["host->createConnection(dispatcher)"]
    M --> N["Add to connecting_clients_"]
    N --> O["Queue pending_stream"]
    
    J -->|No (at limit)| P{Overflow?}
    P -->|No| O
    P -->|Yes| Q["onPoolFailure(Overflow)"]
```

### Connection Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Connecting : tryCreateNewConnection()
    Connecting --> Ready : TCP+TLS handshake complete
    Ready --> Busy : stream attached
    Busy --> Ready : stream complete, capacity left
    Busy --> Draining : stream complete, max streams reached
    Ready --> Draining : idle timeout or drain
    Draining --> [*] : all streams done
    Connecting --> [*] : connection failure
    
    state "Ready" as Ready
    state "Busy" as Busy
    state "Connecting" as Connecting
    state "Draining" as Draining
```

### Connection Ready — onPoolReady()

```mermaid
sequenceDiagram
    participant Pool as HttpConnPoolImplBase
    participant AC as ActiveClient
    participant CC as CodecClient
    participant UR as UpstreamRequest

    Note over Pool: Connection handshake complete
    Pool->>Pool: onUpstreamReady()
    Pool->>Pool: Move client: connecting → ready
    Pool->>Pool: processIdleClient(client)
    
    alt Pending stream exists
        Pool->>Pool: Dequeue pending_stream
        Pool->>AC: attachStream(context)
        Pool->>Pool: Move client: ready → busy
        Pool->>Pool: onPoolReady(client, context)
        Pool->>CC: newStreamEncoder(response_decoder)
        CC-->>Pool: RequestEncoder&
        Pool->>UR: callbacks.onPoolReady(encoder, host, stream_info, protocol)
    else No pending streams
        Note over Pool: Client stays in ready_clients_
    end
```

```
File: source/common/http/conn_pool_base.h (lines 62-80)

onPoolReady(client, context):
    1. Cast to HTTP ActiveClient
    2. encoder = http_client->newStreamEncoder(response_decoder)
    3. callbacks.onPoolReady(encoder, host, stream_info, protocol)
```

## ActiveClient — Connection Wrapper

```mermaid
graph TD
    subgraph "ActiveClient"
        CC["CodecClient"]
        
        subgraph "CodecClient internals"
            NetConn["Network::ClientConnection"]
            HTTPCodec["HTTP ClientConnection (codec)"]
            TS["TransportSocket (TLS)"]
        end
        
        CC --> NetConn
        CC --> HTTPCodec
        NetConn --> TS
        
        StreamCount["stream_count_"]
        MaxStreams["remaining_streams_"]
        State["state_<br/>(Connecting, Ready, Busy, Draining)"]
    end
    
    ActiveClient --> CC
    ActiveClient --> StreamCount
    ActiveClient --> MaxStreams
    ActiveClient --> State
```

### ActiveClient Initialization

```
File: source/common/http/conn_pool_base.h (lines 98-137)

ActiveClient::initialize():
    1. host()->createConnection(dispatcher, socketOptions, transportSocketOptions)
       → Creates Network::ClientConnection with TransportSocket
    2. createCodecClient(data)
       → Creates CodecClient wrapping the connection
    3. codec_client_->addConnectionCallbacks(this)
    4. codec_client_->setConnectionCloseListener(...)
```

### CodecClient

`CodecClient` wraps a network connection + HTTP codec as a unified client:

```mermaid
classDiagram
    class CodecClient {
        +newStream(ResponseDecoder) RequestEncoder
        +close()
        +protocol() Protocol
        +streamInfo() StreamInfo
        -connection_ : ClientConnectionPtr
        -codec_ : ClientConnectionPtr (HTTP)
    }
    class CodecClientHttp1 {
        +createCodec()
    }
    class CodecClientHttp2 {
        +createCodec()
    }
    
    CodecClientHttp1 --|> CodecClient
    CodecClientHttp2 --|> CodecClient
```

## HTTP/1 vs HTTP/2 Connection Pools

### HTTP/1.1 — One Stream per Connection

```mermaid
graph TD
    subgraph "HTTP/1.1 Pool"
        C1["Connection 1<br/>🔵 1 active stream"]
        C2["Connection 2<br/>🟢 idle (ready)"]
        C3["Connection 3<br/>🔵 1 active stream"]
    end
    
    R1["Request A"] --> C1
    R2["Request B"] --> C3
    R3["Request C"] -.->|"queued"| Pending["Pending Queue"]
    Pending -.->|"when C2 available"| C2
```

### HTTP/2 — Multiple Streams per Connection

```mermaid
graph TD
    subgraph "HTTP/2 Pool"
        C1["Connection 1<br/>🔵 3 active streams<br/>(max: 100)"]
        C2["Connection 2<br/>🔵 1 active stream<br/>(max: 100)"]
    end
    
    R1["Request A"] --> C1
    R2["Request B"] --> C1
    R3["Request C"] --> C1
    R4["Request D"] --> C2
```

HTTP/2 pool can multiplex many streams over a single connection. The `max_concurrent_streams` setting (from SETTINGS frame or config) controls how many streams a single connection can handle.

## Upstream Connection Creation

### Host::createConnection()

```mermaid
sequenceDiagram
    participant AC as ActiveClient
    participant Host as HostImpl
    participant TSF as TransportSocketFactory
    participant Disp as Dispatcher

    AC->>Host: createConnection(dispatcher, options, transport_options)
    Host->>Host: Resolve address
    Host->>TSF: createTransportSocket(options, host)
    TSF-->>Host: TransportSocket (e.g., TLS)
    Host->>Disp: createClientConnection(address, source, transport_socket, options)
    Disp-->>Host: ClientConnectionPtr
    Host-->>AC: CreateConnectionData{connection, host_description}
```

```
File: source/common/upstream/upstream_impl.cc (lines 477-503)

createConnection():
    1. Resolve transport socket factory (may use transport socket match criteria)
    2. factory.createTransportSocket(options, host)
    3. dispatcher.createClientConnection(address, source_addr, transport_socket, ...)
    4. Return {connection, host_description}
```

## Connection Pool Limits and Flow Control

### Key Limits

```mermaid
graph TD
    subgraph "Circuit Breakers"
        MaxConn["max_connections<br/>(per cluster, per priority)"]
        MaxPending["max_pending_requests<br/>(pending queue size)"]
        MaxRequests["max_requests<br/>(active streams)"]
        MaxRetries["max_retries<br/>(concurrent retries)"]
    end
    
    subgraph "Per-Connection Limits"
        MaxStreams["max_streams_per_connection<br/>(HTTP/2)"]
        IdleTimeout["idle_timeout<br/>(close idle connections)"]
        MaxLifetime["max_connection_duration"]
    end
    
    subgraph "Preconnect"
        Ratio["preconnect_ratio<br/>(anticipate future streams)"]
    end
```

### Preconnect Logic

```mermaid
flowchart TD
    A["New stream request"] --> B["Attach to existing ready client"]
    B --> C{Preconnect ratio check}
    C -->|"pending / connecting > ratio"| D["Don't preconnect"]
    C -->|"Need more"| E["tryCreateNewConnection()"]
    E --> F["New connection starts handshake"]
    F --> G["Will be ready for future requests"]
```

## Connection Pool Failure Handling

```mermaid
flowchart TD
    A["Pool request"] --> B{Connection result}
    B -->|ConnectionFailure| C["onPoolFailure(ConnectionFailure)"]
    B -->|Timeout| D["onPoolFailure(Timeout)"]
    B -->|Overflow| E["onPoolFailure(Overflow)"]
    B -->|LocalConnectionFailure| F["onPoolFailure(LocalConnectionFailure)"]
    
    C --> G["Router handles failure"]
    D --> G
    E --> G
    F --> G
    
    G --> H{Retryable?}
    H -->|Yes| I["Router retries with new pool"]
    H -->|No| J["sendLocalReply(503)"]
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `source/common/conn_pool/conn_pool_base.h` | 173-391 | `ConnPoolImplBase` — base pool logic |
| `source/common/http/conn_pool_base.h` | 52-95 | `HttpConnPoolImplBase` — HTTP pool |
| `source/common/http/conn_pool_base.h` | 62-80 | `onPoolReady()` — stream attachment |
| `source/common/http/conn_pool_base.h` | 98-137 | `ActiveClient` — connection wrapper |
| `source/common/http/codec_client.h` | — | `CodecClient` — codec + connection |
| `source/common/upstream/upstream_impl.cc` | 477-503 | `Host::createConnection()` |
| `source/common/router/router.cc` | 752-786 | `createConnPool()` in Router |
| `envoy/upstream/upstream.h` | — | `ThreadLocalCluster`, `Host` interfaces |

---

**Previous:** [Part 10 — Router Filter](10-router-filter.md)  
**Next:** [Part 12 — Response Flow: Upstream to Downstream](12-response-flow.md)
