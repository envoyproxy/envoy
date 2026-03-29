# Envoy Listener Architecture

> How `ListenerManagerImpl`, `ListenerImpl`, `ConnectionHandlerImpl`, `ActiveListenerImplBase`, `ActiveTcpListener`, `ActiveTcpSocket`, and `Connection` interact to accept and serve TCP connections.

---

## 1. Block Diagram

```mermaid
flowchart TB
    subgraph MainThread["Main Thread"]
        LDS["LDS API (xDS)"]
        LMI["ListenerManagerImpl"]
        LI["ListenerImpl"]
        CHI_main["ConnectionHandlerImpl (main)"]
    end

    subgraph WorkerN["Worker Thread (per worker)"]
        Worker["WorkerImpl"]
        CHI["ConnectionHandlerImpl"]
        ATL["ActiveTcpListener"]
        ATS["ActiveTcpSocket"]
        Conn["Network::Connection"]
        FC["FilterChain"]
    end

    LDS -->|addOrUpdateListener| LMI
    LMI -->|create| LI
    LI -->|addListenerToWorker| Worker
    Worker -->|addListener| CHI
    CHI -->|create| ATL
    ATL -->|onAccept| ATS
    ATS -->|continueFilterChain| ATS
    ATS -->|newConnection| ATL
    ATL -->|createServerConnection| Conn
    Conn -->|matched filter chain| FC
```

---

## 2. Class Diagram (UML)

```mermaid
classDiagram
    class ListenerManagerImpl {
        -active_listeners_: ListenerList
        -warming_listeners_: ListenerList
        -workers_: vector~WorkerPtr~
        +addOrUpdateListener(config) bool
        +removeListener(name) bool
        +startWorkers(guard_dog, callback) Status
        +stopListeners(type, options) void
        +numConnections() uint64_t
    }

    class ListenerImpl {
        -config_: ListenerConfig
        -filter_chain_manager_: FilterChainManagerImpl
        -socket_factory_: ListenSocketFactory
        +filterChainFactory() FilterChainFactory
        +filterChainManager() FilterChainManager
        +listenerTag() uint64_t
        +bindToPort() bool
    }

    class ConnectionHandlerImpl {
        -dispatcher_: Event::Dispatcher
        -listeners_: ActiveListenerDetailsMap
        +addListener(config) void
        +removeListeners(tag) void
        +numConnections() uint64_t
        +createListener(socket, cb, ...) ListenerPtr
        +getBalancedHandlerByTag(tag) BalancedHandlerOptRef
    }

    class ActiveListenerImplBase {
        +stats_: ListenerStats
        +per_worker_stats_: PerHandlerListenerStats
        +config_: ListenerConfig*
        +listenerTag() uint64_t
    }

    class ActiveStreamListenerBase {
        -sockets_: list~ActiveTcpSocket~
        -listener_: ListenerPtr
        -dispatcher_: Dispatcher
        +onSocketAccepted(socket) void
        +newConnection(socket, stream_info) void
        +removeSocket(socket) ActiveTcpSocketPtr
    }

    class ActiveTcpListener {
        -num_listener_connections_: atomic_uint64_t
        -connection_balancer_: ConnectionBalancer
        -listen_address_: Address
        +onAccept(socket) void
        +onAcceptWorker(socket, ...) void
        +newActiveConnection(filter_chain, conn, ...) void
        +post(socket) void
        +incNumConnections() void
        +decNumConnections() void
    }

    class ActiveTcpSocket {
        -socket_: ConnectionSocketPtr
        -accept_filters_: list~GenericListenerFilter~
        -listener_: ActiveStreamListenerBase
        -stream_info_: StreamInfo
        +startFilterChain() void
        +continueFilterChain(success) void
        +addAcceptFilter(matcher, filter) void
        +onTimeout() void
    }

    class ActiveTcpConnection {
        -connection_: ConnectionPtr
        -stream_info_: StreamInfo
        -active_connections_: ActiveConnections
        +onEvent(event) void
    }

    ListenerManagerImpl --> ListenerImpl : owns
    ListenerManagerImpl --> ConnectionHandlerImpl : controls via Worker
    ConnectionHandlerImpl --> ActiveTcpListener : creates
    ActiveListenerImplBase <|-- ActiveStreamListenerBase
    ActiveStreamListenerBase <|-- ActiveTcpListener
    ActiveTcpListener --> ActiveTcpSocket : creates on accept
    ActiveTcpSocket --> ActiveTcpConnection : promotes to
```

---

## 3. TCP Connection Accept Sequence Diagram

```mermaid
sequenceDiagram
    participant Client as TCP Client
    participant OS as OS / Socket
    participant ATL as ActiveTcpListener
    participant ATS as ActiveTcpSocket
    participant LF as ListenerFilter(s)
    participant SB as ActiveStreamListenerBase
    participant CM as ConnectionHandlerImpl
    participant Conn as Network::Connection
    participant FC as FilterChain

    Client->>OS: TCP SYN
    OS-->>ATL: onAccept(socket)
    ATL->>ATL: check connection limit
    ATL->>ATL: check rebalance needed?
    alt Rebalance to another worker
        ATL->>ATL: post(socket) to target worker
    else Accept locally
        ATL->>ATS: create ActiveTcpSocket
        ATL->>SB: onSocketAccepted(active_socket)
        SB->>FC: createListenerFilterChain(active_socket)
        SB->>ATS: startFilterChain()
        loop For each ListenerFilter
            ATS->>LF: onAccept(callbacks)
            LF-->>ATS: FilterStatus (Continue / StopIteration)
        end
        ATS->>SB: continueFilterChain(success=true)
        SB->>SB: newConnection(socket, stream_info)
        SB->>CM: createServerConnection(socket, transport_socket)
        CM-->>Conn: Network::Connection created
        SB->>FC: matched filter chain
        FC->>Conn: createNetworkFilterChain(connection)
        Conn-->>Client: TCP established
    end
```

---

## 4. Listener Startup Sequence Diagram

```mermaid
sequenceDiagram
    participant LDS as LDS xDS
    participant LMI as ListenerManagerImpl
    participant LI as ListenerImpl
    participant Worker as WorkerImpl
    participant CHI as ConnectionHandlerImpl
    participant ATL as ActiveTcpListener

    LDS->>LMI: addOrUpdateListener(config)
    LMI->>LI: create ListenerImpl
    LI->>LI: build FilterChainManager
    LI->>LI: create ListenSocketFactory
    LMI->>LMI: warming_listeners_.push(LI)
    LMI->>Worker: addListenerToWorker(LI)
    Worker->>CHI: addListener(config)
    CHI->>ATL: create ActiveTcpListener
    ATL->>ATL: bind socket, start listening
    ATL-->>LMI: onListenerWarmed()
    LMI->>LMI: move to active_listeners_
```

---

## 5. Listener Removal / Drain Sequence Diagram

```mermaid
sequenceDiagram
    participant LDS as LDS xDS
    participant LMI as ListenerManagerImpl
    participant Worker as WorkerImpl
    participant CHI as ConnectionHandlerImpl
    participant ATL as ActiveTcpListener

    LDS->>LMI: removeListener(name)
    LMI->>LMI: drainListener(listener)
    LMI->>Worker: stopListener(listener)
    Worker->>CHI: stopListeners(tag)
    CHI->>ATL: shutdownListener()
    ATL->>ATL: listener_.reset() (stop accepting)
    LMI->>LMI: start drain timer
    Note over ATL: existing connections continue
    LMI->>Worker: removeListener(tag)
    Worker->>CHI: removeListeners(tag)
    CHI->>CHI: destroy ActiveTcpListener
```

---

## 6. Key Classes Explained

### 6.1 `ListenerManagerImpl`

**Location:** `source/common/listener_manager/listener_manager_impl.h`

**Purpose:** Top-level orchestrator for listeners. Translates xDS/LDS config into active listeners on workers.

| Method | Description |
|--------|-------------|
| `addOrUpdateListener()` | Add or hot-reload a listener from config |
| `removeListener()` | Drain and remove a listener |
| `startWorkers()` | Start all worker threads |
| `stopListeners()` | Stop accepting on all/inbound listeners |
| `numConnections()` | Aggregate connection count across workers |

**State:**
- `active_listeners_` — Fully warmed and active.
- `warming_listeners_` — Initializing, not yet serving.
- `draining_listeners_` — Draining old connections.

---

### 6.2 `ListenerImpl`

**Location:** `source/common/listener_manager/listener_impl.h`

**Purpose:** Represents one listener instance. Holds config, filter chain manager, socket factory, and drain manager.

| Member | Description |
|--------|-------------|
| `filter_chain_manager_` | Matches incoming connections to filter chains |
| `socket_factory_` | Creates/distributes listen sockets to workers |
| `drain_manager_` | Tracks drain state |
| `init_manager_` | Coordinates listener initialization |

---

### 6.3 `ConnectionHandlerImpl`

**Location:** `source/common/listener_manager/connection_handler_impl.h`

**Purpose:** Per-worker connection handler. Manages active listeners and counts connections.

| Method | Description |
|--------|-------------|
| `addListener()` | Creates `ActiveTcpListener` / UDP / QUIC listener |
| `removeListeners()` | Destroy listener by tag |
| `createListener()` | Create a `Network::Listener` from socket |
| `getBalancedHandlerByTag()` | Find the right handler for connection rebalancing |
| `numConnections()` | Count active connections on this worker |

---

### 6.4 `ActiveListenerImplBase`

**Location:** `source/server/active_listener_base.h`

**Purpose:** Shared base for all active listener types. Holds stats and listener config pointer.

| Member | Description |
|--------|-------------|
| `stats_` | Per-listener stats (active_cx, cx_overflow, etc.) |
| `per_worker_stats_` | Per-worker-per-listener stats |
| `config_` | Pointer to the `ListenerConfig` |

---

### 6.5 `ActiveStreamListenerBase`

**Location:** `source/common/listener_manager/active_stream_listener_base.h`

**Purpose:** Extends `ActiveListenerImplBase`. Manages the lifecycle of `ActiveTcpSocket` objects and transitions them to `Connection`.

| Method | Description |
|--------|-------------|
| `onSocketAccepted()` | Run listener filters, move socket to list if paused |
| `newConnection()` | Call `createServerConnection()`, run network filter chain |
| `removeSocket()` | Detach socket from internal list |
| `onFilterChainDraining()` | Remove connections from draining chains |

---

### 6.6 `ActiveTcpListener`

**Location:** `source/common/listener_manager/active_tcp_listener.h`

**Purpose:** TCP-specific active listener. Handles `onAccept`, balancing, and promoting sockets to connections.

| Method | Description |
|--------|-------------|
| `onAccept()` | Called by OS-level event, checks limits, may rebalance |
| `onAcceptWorker()` | Accepted on this worker; create `ActiveTcpSocket` |
| `newActiveConnection()` | Wraps a `ServerConnection` in `ActiveTcpConnection` |
| `post()` | Rebalance socket to another worker |
| `incNumConnections()` / `decNumConnections()` | Track per-listener connection count |

---

### 6.7 `ActiveTcpSocket`

**Location:** `source/common/listener_manager/active_tcp_socket.h`

**Purpose:** Wraps a newly accepted socket as it passes through listener filters.

| Method | Description |
|--------|-------------|
| `startFilterChain()` | Kick off listener filter iteration |
| `continueFilterChain()` | Advance to next filter or promote to connection |
| `addAcceptFilter()` | Register a listener filter |
| `onTimeout()` | Timeout on listener filter |
| `setDynamicMetadata()` | Filters can set metadata (e.g. for routing) |

**State machine:**
1. Created on `onAccept`.
2. Listener filters run one by one via `continueFilterChain()`.
3. If a filter stops iteration → socket is held in `sockets_` list, timer started.
4. When all filters pass → `newConnection()` is called.
5. Promoted to `Connection` → socket removed from list, `ActiveTcpConnection` created.

---

### 6.8 `ActiveTcpConnection`

**Location:** `source/common/listener_manager/active_stream_listener_base.h`

**Purpose:** Wraps a live `Network::Connection` in the listener's context.

| Member | Description |
|--------|-------------|
| `connection_` | The underlying network connection |
| `stream_info_` | Request/connection metadata |
| `active_connections_` | Parent connection collection |

---

### 6.9 `ActiveConnections`

**Purpose:** Groups `ActiveTcpConnection` objects that share the same filter chain. When a filter chain is drained, all its connections are removed together.

---

## 7. Listener Filter Chain vs Network Filter Chain

```mermaid
flowchart LR
    subgraph L4Accept["L4 Accept Phase"]
        LF1["ListenerFilter 1<br/>(e.g. tls_inspector)"]
        LF2["ListenerFilter 2<br/>(e.g. original_dst)"]
    end

    subgraph NetworkFilters["Network Filter Phase"]
        NF1["NetworkFilter 1<br/>(e.g. http_connection_manager)"]
        NF2["NetworkFilter 2<br/>(optional)"]
    end

    Socket --> LF1 --> LF2
    LF2 -->|"match filter chain"| NF1 --> NF2 --> Upstream
```

| Phase | Where | Purpose |
|-------|-------|---------|
| **Listener Filter** | `ActiveTcpSocket` | Pre-accept: inspect, modify, route socket before Connection is created |
| **Network Filter** | `Connection` | Post-accept: read/write data, codec, proxy |

---

## 8. Connection Balancing

When `use_original_dst` or `connection_balance_config` is set, `ActiveTcpListener` may redirect incoming sockets to a different worker via `post()`.

```mermaid
sequenceDiagram
    participant W1 as Worker 1 (accepted)
    participant ATL1 as ActiveTcpListener (W1)
    participant ATL2 as ActiveTcpListener (W2)
    participant W2 as Worker 2 (target)

    W1->>ATL1: onAccept(socket)
    ATL1->>ATL1: getBalancedHandlerByAddress()
    ATL1->>ATL2: post(socket)
    ATL2->>W2: dispatcher_.post(lambda)
    W2->>ATL2: onAcceptWorker(socket, rebalanced=true)
```

---

## 9. Source Paths

| Class | Path |
|-------|------|
| `ListenerManagerImpl` | `source/common/listener_manager/listener_manager_impl.*` |
| `ListenerImpl` | `source/common/listener_manager/listener_impl.*` |
| `ConnectionHandlerImpl` | `source/common/listener_manager/connection_handler_impl.*` |
| `ActiveListenerImplBase` | `source/server/active_listener_base.h` |
| `ActiveStreamListenerBase` / `ActiveConnections` | `source/common/listener_manager/active_stream_listener_base.*` |
| `ActiveTcpListener` | `source/common/listener_manager/active_tcp_listener.*` |
| `ActiveTcpSocket` | `source/common/listener_manager/active_tcp_socket.*` |
| `FilterChainManagerImpl` | `source/common/listener_manager/filter_chain_manager_impl.*` |
