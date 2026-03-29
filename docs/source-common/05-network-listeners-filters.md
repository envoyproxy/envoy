# Part 5: `source/common/network/` + `listener_manager/` — Listeners, Filters, and Active Connections

## Overview

This document covers the listener management layer — how listeners are created, how connections are accepted and distributed to workers, and how the `listener_manager/` folder orchestrates the lifecycle. This complements the `network/` folder's `TcpListenerImpl`.

## Listener Manager Architecture

```mermaid
graph TD
    subgraph "source/common/listener_manager/"
        LMI["listener_manager_impl.h/cc\n(ListenerManagerImpl)"]
        LI["listener_impl.h/cc\n(ListenerImpl)"]
        ATL["active_tcp_listener.h/cc\n(ActiveTcpListener)"]
        ATS["active_tcp_socket.h/cc\n(ActiveTcpSocket)"]
        ASLB["active_stream_listener_base.h/cc\n(ActiveStreamListenerBase)"]
        FCMI["filter_chain_manager_impl.h/cc\n(FilterChainManagerImpl)"]
        CHI["connection_handler_impl.h/cc\n(ConnectionHandlerImpl)"]
    end
    
    LMI -->|"creates"| LI
    LMI -->|"distributes to"| CHI
    CHI -->|"creates"| ATL
    ATL -->|"creates"| ATS
    ATS -->|"creates"| ASLB
    LI -->|"has"| FCMI
```

## Class Relationships

```mermaid
classDiagram
    class ListenerManagerImpl {
        -active_listeners_ : list
        -warming_listeners_ : list
        -workers_ : list~Worker~
        +addOrUpdateListener(config) bool
        +removeListener(name)
        +startWorkers(guard, callback)
    }
    class ListenerImpl {
        -listener_filter_factories_
        -filter_chain_manager_ : FilterChainManagerImpl
        -listen_socket_factories_
        +createListenerFilterChain(manager)
        +createNetworkFilterChain(conn, factories)
        +filterChainManager()
    }
    class ConnectionHandlerImpl {
        -listeners_ : map
        +addListener(config, options)
        +removeListeners(tag)
    }
    class ActiveTcpListener {
        -parent_ : ConnectionHandlerImpl
        -connection_balancer_
        +onAccept(socket)
        +onAcceptWorker(socket)
        +newActiveConnection(chain, conn)
    }
    class ActiveTcpSocket {
        -accept_filters_ : list
        -socket_ : ConnectionSocketPtr
        +continueFilterChain(success)
        +newConnection()
    }
    class ActiveStreamListenerBase {
        -config_ : ListenerConfig
        +onSocketAccepted(socket)
        +newConnection(socket, stream_info)
    }
    class FilterChainManagerImpl {
        -fc_contexts_ : match tree
        -default_filter_chain_
        +findFilterChain(socket, info) FilterChain*
        +addFilterChains(chains, builder)
    }

    ListenerManagerImpl --> ListenerImpl
    ListenerManagerImpl --> ConnectionHandlerImpl
    ConnectionHandlerImpl --> ActiveTcpListener
    ActiveTcpListener --|> ActiveStreamListenerBase
    ActiveStreamListenerBase --> ActiveTcpSocket
    ListenerImpl --> FilterChainManagerImpl
```

## Listener Lifecycle

```mermaid
stateDiagram-v2
    [*] --> ConfigReceived : LDS/static config
    ConfigReceived --> Warming : addOrUpdateListener()
    Warming --> Warming : initialize (health checks, secrets)
    Warming --> Active : initialization complete
    Active --> Active : config update (in-place)
    Active --> Draining : removeListener()
    Draining --> [*] : drain complete
    
    note right of Warming : ListenerImpl created,\nsockets bound,\nfactory chains built
    note right of Active : Distributed to workers,\naccepting connections
    note right of Draining : Stop accepting,\nwait for existing connections
```

## Worker Thread Architecture

```mermaid
graph TD
    subgraph "Main Thread"
        LM["ListenerManagerImpl"]
        LM --> L1["ListenerImpl (config)"]
        LM --> L2["ListenerImpl (config)"]
    end
    
    subgraph "Worker 1"
        CH1["ConnectionHandlerImpl"]
        CH1 --> ATL1["ActiveTcpListener\n(port 8080)"]
        CH1 --> ATL2["ActiveTcpListener\n(port 8443)"]
        ATL1 --> TLI1["TcpListenerImpl\n(event loop)"]
        ATL2 --> TLI2["TcpListenerImpl\n(event loop)"]
    end
    
    subgraph "Worker 2"
        CH2["ConnectionHandlerImpl"]
        CH2 --> ATL3["ActiveTcpListener\n(port 8080)"]
        CH2 --> ATL4["ActiveTcpListener\n(port 8443)"]
    end
    
    LM -->|"addListenerToWorker()"| CH1
    LM -->|"addListenerToWorker()"| CH2
```

## Connection Accept Flow (Detailed)

```mermaid
sequenceDiagram
    participant EL as Event Loop
    participant TLI as TcpListenerImpl
    participant ATL as ActiveTcpListener
    participant CB as ConnectionBalancer
    participant ATS as ActiveTcpSocket
    participant ASL as ActiveStreamListenerBase
    participant LI as ListenerImpl
    participant FCMI as FilterChainManagerImpl
    participant Disp as Dispatcher

    EL->>TLI: Socket readable event
    TLI->>TLI: socket_->ioHandle().accept()
    TLI->>TLI: Create AcceptedSocketImpl
    TLI->>ATL: onAccept(socket)
    
    ATL->>ATL: Check per-listener limit
    ATL->>CB: pickTargetHandler(socket)
    CB-->>ATL: target (self or other worker)
    
    ATL->>ATS: new ActiveTcpSocket(socket)
    ATL->>ASL: onSocketAccepted(active_socket)
    
    ASL->>LI: createListenerFilterChain(socket)
    Note over ATS: Run listener filters
    ATS->>ATS: continueFilterChain(true)
    Note over ATS: tls_inspector, http_inspector, etc.
    
    ATS->>ASL: newConnection(socket, stream_info)
    ASL->>FCMI: findFilterChain(socket, info)
    FCMI-->>ASL: FilterChainImpl*
    
    ASL->>ASL: chain.transportSocketFactory().createDownstreamTransportSocket()
    ASL->>Disp: createServerConnection(socket, transport_socket)
    Disp-->>ASL: ServerConnectionImpl
    
    ASL->>LI: createNetworkFilterChain(conn, factories)
    Note over ASL: Filters added, initializeReadFilters()
    
    ASL->>ATL: newActiveConnection(chain, conn)
    Note over ATL: Connection tracked in connections_by_context_
```

## Active Connection Tracking

```mermaid
graph TD
    subgraph "OwnedActiveStreamListenerBase"
        CBC["connections_by_context_\n(map: FilterChain → ActiveConnections)"]
        
        subgraph "FilterChain A connections"
            AC1["ActiveTcpConnection 1"]
            AC2["ActiveTcpConnection 2"]
        end
        
        subgraph "FilterChain B connections"
            AC3["ActiveTcpConnection 3"]
            AC4["ActiveTcpConnection 4"]
            AC5["ActiveTcpConnection 5"]
        end
        
        CBC --> AC1
        CBC --> AC2
        CBC --> AC3
        CBC --> AC4
        CBC --> AC5
    end
```

Connections are grouped by filter chain for efficient drain operations — when a filter chain is removed (e.g., TLS cert rotation), only connections using that chain need to be drained.

## Filter Chain Matching — Deep Dive

```mermaid
graph TD
    subgraph "FilterChainManagerImpl Match Tree"
        Root["fc_contexts_\n(destination port index)"]
        
        Root --> P80["Port 80"]
        Root --> P443["Port 443"]
        Root --> P0["Port 0 (any)"]
        
        P443 --> DIP1["Dest IP: 0.0.0.0/0"]
        
        DIP1 --> SNI1["SNI: api.example.com"]
        DIP1 --> SNI2["SNI: *.example.com"]
        DIP1 --> SNI_["SNI: (empty/any)"]
        
        SNI1 --> TP1["transport: tls"]
        TP1 --> AP1["ALPN: h2"]
        AP1 --> FC1["FilterChainImpl\n(cert-api, HCM-api)"]
        
        SNI2 --> TP2["transport: tls"]
        TP2 --> FC2["FilterChainImpl\n(cert-wild, HCM-web)"]
    end
```

### LcTrie for IP Matching

The `lc_trie.h` implementation provides efficient longest-prefix-match for CIDR-based filter chain matching:

```mermaid
graph TD
    subgraph "LcTrie (Level-Compressed Trie)"
        R["Root"]
        R --> N1["10.0.0.0/8"]
        R --> N2["192.168.0.0/16"]
        N1 --> N3["10.0.1.0/24"]
        N1 --> N4["10.0.2.0/24"]
        N2 --> N5["192.168.1.0/24"]
    end
    
    Query["Query: 10.0.1.5"] --> N3
    Note["Longest prefix match\nO(key_length) lookup"]
```

## Listener Filter Buffer

`ListenerFilterBufferImpl` provides a peek buffer for listener filters that need to read bytes without consuming them:

```mermaid
sequenceDiagram
    participant ATS as ActiveTcpSocket
    participant LFB as ListenerFilterBufferImpl
    participant IO as IoHandle
    participant Filter as TLS Inspector

    ATS->>LFB: new ListenerFilterBufferImpl(io_handle, max_bytes)
    ATS->>LFB: peekFromSocket()
    LFB->>IO: recv(buffer, MSG_PEEK)
    IO-->>LFB: ClientHello bytes (peeked, not consumed)
    LFB-->>ATS: bytes available
    
    ATS->>Filter: onData(buffer)
    Filter->>Filter: Parse TLS ClientHello
    Filter->>Filter: Extract SNI, ALPN
    Filter-->>ATS: Continue
    
    Note over LFB: Data stays in kernel buffer\nConnection will re-read it later
```

## File Catalog — listener_manager/

| File | Key Classes | Purpose |
|------|-------------|---------|
| `listener_manager_impl.h/cc` | `ListenerManagerImpl` | Listener lifecycle management |
| `listener_impl.h/cc` | `ListenerImpl`, `ListenSocketFactoryImpl` | Listener config and socket factory |
| `active_tcp_listener.h/cc` | `ActiveTcpListener` | Per-worker TCP listener |
| `active_tcp_socket.h/cc` | `ActiveTcpSocket` | Socket during listener filter processing |
| `active_stream_listener_base.h/cc` | `ActiveStreamListenerBase`, `OwnedActiveStreamListenerBase` | Connection creation base |
| `active_listener_base.h` | `ActiveListenerImplBase` | Active listener stats base |
| `filter_chain_manager_impl.h/cc` | `FilterChainManagerImpl`, `FilterChainImpl` | Filter chain matching and storage |
| `connection_handler_impl.h/cc` | `ConnectionHandlerImpl` | Per-worker connection handler |
| `lds_api.h/cc` | `LdsApiImpl` | Listener Discovery Service |

---

**Previous:** [Part 4 — Network Connections and Sockets](04-network-connections-sockets.md)  
**Next:** [Part 6 — Routing Engine and Configuration](06-router-engine-config.md)
