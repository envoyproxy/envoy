# Listener Manager — Overview Part 1: Architecture & ListenerManager

**Directory:** `source/common/listener_manager/`  
**Part:** 1 of 4 — Overall Architecture, ListenerManagerImpl, Worker Dispatch, Listener Lifecycle

---

## Table of Contents

1. [High-Level Architecture](#1-high-level-architecture)
2. [Component Map](#2-component-map)
3. [End-to-End Flow: Config to Connection](#3-end-to-end-flow-config-to-connection)
4. [ListenerManagerImpl](#4-listermanagerimpl)
5. [Listener Lifecycle States](#5-listener-lifecycle-states)
6. [Worker Dispatch](#6-worker-dispatch)
7. [ListenerImpl — Config to Runtime](#7-listenerimpl--config-to-runtime)
8. [Key Design Patterns](#8-key-design-patterns)

---

## 1. High-Level Architecture

```mermaid
flowchart TB
    subgraph Config["Configuration Sources"]
        Bootstrap["Bootstrap Config<br/>(static listeners)"]
        LDS["LDS API<br/>(dynamic listeners)"]
    end

    subgraph MainThread["Main Thread"]
        LM["ListenerManagerImpl<br/>(lifecycle, config changes)"]
        LI["ListenerImpl<br/>(per listener config)"]
        FCM["FilterChainManagerImpl<br/>(filter chain lookup)"]
        LSFI["ListenSocketFactoryImpl<br/>(socket creation)"]
        DFCM["DrainingFilterChainsManager<br/>(old chain cleanup)"]
    end

    subgraph Workers["Worker Threads"]
        W0["Worker 0"]
        W1["Worker 1"]
        W2["Worker 2"]
    end

    subgraph PerWorker["Per Worker (ConnectionHandlerImpl)"]
        ATL["ActiveTcpListener"]
        ATS["ActiveTcpSocket<br/>(listener filters)"]
        AC["ActiveConnections<br/>(per filter chain)"]
        ATC["ActiveTcpConnection"]
    end

    Bootstrap & LDS --> LM
    LM --> LI --> FCM
    LI --> LSFI
    LM --> DFCM
    LM -->|dispatch| W0 & W1 & W2
    W0 & W1 & W2 --> PerWorker
    ATL --> ATS --> AC --> ATC
```

---

## 2. Component Map

```mermaid
mindmap
  root((listener_manager))
    Core Management
      listener_manager_impl.h
      listener_impl.h
      listener_info_impl.h
    Filter Chain
      filter_chain_manager_impl.h
      filter_chain_factory_context_callback.h
    Connection Handling
      connection_handler_impl.h
      active_tcp_listener.h
      active_tcp_socket.h
      active_stream_listener_base.h
    Dynamic Config
      lds_api.h
    UDP
      active_raw_udp_listener_config.h
```

---

## 3. End-to-End Flow: Config to Connection

```mermaid
sequenceDiagram
    autonumber
    participant Config as Bootstrap / LDS
    participant LM as ListenerManagerImpl
    participant LI as ListenerImpl
    participant FCM as FilterChainManagerImpl
    participant Workers as Worker Threads
    participant CH as ConnectionHandlerImpl
    participant ATL as ActiveTcpListener
    participant ATS as ActiveTcpSocket
    participant OS as Kernel

    Config->>LM: addOrUpdateListener(proto_config)
    LM->>LI: ListenerImpl::create(config)
    LI->>FCM: addFilterChains(config.filter_chains)
    LI->>LI: initialize (wait for RDS if needed)
    LI-->>LM: initialization complete
    LM->>LM: move listener: warming → active
    LM->>Workers: dispatch addListener(listener)
    Workers->>CH: addListener(config)
    CH->>ATL: create ActiveTcpListener(socket)
    ATL->>OS: enable file event on listen fd

    OS->>ATL: accept event
    ATL->>ATS: new ActiveTcpSocket(accepted_fd)
    ATS->>ATS: run listener filters
    ATS->>FCM: findFilterChain(socket)
    FCM-->>ATS: FilterChainImpl
    ATS->>ATL: newActiveConnection(chain, socket)
    ATL->>ATL: createNetworkFilterChain on connection
```

---

## 4. ListenerManagerImpl

### Responsibilities

```mermaid
mindmap
  root((ListenerManagerImpl))
    Lifecycle
      addOrUpdateListener
      removeListener
      warming → active → draining
    Workers
      startWorkers
      stopWorkers
      dispatch listeners to workers
    Sockets
      ProdListenerComponentFactory
      Hot restart socket sharing
    Dynamic Config
      createLdsApi
      LDS subscription
    Drain
      DrainingFilterChainsManager
      In-place filter chain update
    Stats
      listener_added
      listener_modified
      listener_removed
      listener_create_failure
```

### Add vs Update Decision

```mermaid
flowchart TD
    Config["New listener config"] --> LM["ListenerManagerImpl::addOrUpdateListener()"]
    LM --> A{Listener name<br/>already exists?}
    A -->|No| New["Create new ListenerImpl<br/>→ warming list"]
    A -->|Yes| B{Same address +<br/>socket options?}
    B -->|Yes| InPlace["In-place filter chain update<br/>newListenerWithFilterChain()"]
    B -->|No| Full["Full listener replacement<br/>old → draining, new → warming"]
    InPlace --> DFC["Old filter chains → DrainingFilterChainsManager"]
    Full --> Drain["Old listener → draining"]
```

### Stats Generated

| Stat | When |
|------|------|
| `listener_manager.listener_added` | New listener successfully created |
| `listener_manager.listener_modified` | Existing listener updated |
| `listener_manager.listener_removed` | Listener removed |
| `listener_manager.listener_create_success` | Listener config parsed and validated |
| `listener_manager.listener_create_failure` | Listener config failed validation |
| `listener_manager.listener_in_place_updated` | Filter-chain-only update (no socket rebind) |
| `listener_manager.total_listeners_warming` | Gauge: listeners initializing |
| `listener_manager.total_listeners_active` | Gauge: listeners serving |
| `listener_manager.total_listeners_draining` | Gauge: listeners draining |

---

## 5. Listener Lifecycle States

```mermaid
stateDiagram-v2
    [*] --> Warming : addOrUpdateListener()
    Warming --> Active : initialization complete
    Warming --> [*] : initialization failed
    Active --> Active : in-place filter chain update
    Active --> Draining : removeListener() or full replacement
    Draining --> [*] : all connections closed + drain timeout
```

### What Happens in Each State

| State | Accepting? | In Worker? | Description |
|-------|-----------|-----------|-------------|
| **Warming** | No | No | Waiting for initialization (RDS, ECDS, secrets) |
| **Active** | Yes | Yes | Fully operational, accepting connections |
| **Draining** | No | Being removed | No new connections; existing connections finish |

---

## 6. Worker Dispatch

### Adding a Listener to Workers

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl (main thread)
    participant W0 as Worker 0
    participant W1 as Worker 1
    participant CH0 as ConnectionHandlerImpl (W0)
    participant CH1 as ConnectionHandlerImpl (W1)

    LM->>W0: post(addListener, listener_config)
    LM->>W1: post(addListener, listener_config)
    W0->>CH0: addListener(config, runtime)
    CH0->>CH0: create ActiveTcpListener(socket_for_worker_0)
    W1->>CH1: addListener(config, runtime)
    CH1->>CH1: create ActiveTcpListener(socket_for_worker_1)
```

### `SO_REUSEPORT` — Per-Worker Sockets

```mermaid
flowchart TD
    LSFI["ListenSocketFactoryImpl"] --> B{SO_REUSEPORT?}
    B -->|Yes| Multi["Separate socket per worker:<br/>Worker 0 → socket[0]<br/>Worker 1 → socket[1]<br/>Worker 2 → socket[2]"]
    B -->|No| Shared["All workers share socket[0]"]
    Multi -->|kernel distributes| Fair["Kernel load-balances across sockets"]
    Shared -->|accept contention| Thundering["Thundering herd (mitigated by EPOLLEXCLUSIVE)"]
```

---

## 7. ListenerImpl — Config to Runtime

### What ListenerImpl Owns

```mermaid
flowchart LR
    LI["ListenerImpl"] --> FCM["FilterChainManagerImpl<br/>(filter chain lookup trie)"]
    LI --> LSFI["ListenSocketFactoryImpl<br/>(socket per worker)"]
    LI --> LFs["Listener Filters<br/>(TLS inspector, proxy proto)"]
    LI --> Context["PerListenerFactoryContextImpl<br/>(scope, init manager, cluster mgr)"]
    LI --> Metadata["ListenerInfoImpl<br/>(metadata, direction, overload)"]
```

### `ListenerMessageUtil` — Config Comparison

```mermaid
flowchart TD
    Old["Old Listener Config"] --> LMU["ListenerMessageUtil"]
    New["New Listener Config"] --> LMU
    LMU --> A{Socket options equal?}
    A -->|No| Full["Full replacement needed"]
    A -->|Yes| B{Only filter chains changed?}
    B -->|Yes| InPlace["In-place update possible"]
    B -->|No| Full
```

### `ProdListenerComponentFactory` — Hot Restart

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant PLCF as ProdListenerComponentFactory
    participant HR as HotRestarter
    participant OS as Kernel

    LM->>PLCF: createListenSocket(addr, opts, bind_type)
    PLCF->>HR: getParentSocket(addr)
    alt Parent Envoy has socket for this address
        HR-->>PLCF: inherited_fd
        PLCF->>PLCF: duplicate fd, apply new options
        PLCF-->>LM: ListenSocket(inherited_fd)
    else No parent (cold start)
        PLCF->>OS: socket() + bind() + listen()
        PLCF-->>LM: ListenSocket(new_fd)
    end
```

---

## 8. Key Design Patterns

### Pattern 1: Main Thread Owns Config, Workers Own Connections

All config changes flow through the main thread (`ListenerManagerImpl`). Workers are dispatched to asynchronously. This avoids locking on config data.

```mermaid
flowchart LR
    MainThread["Main Thread<br/>(LM, LI, FCM, LDS)"] -->|post()| Worker["Worker Thread<br/>(CH, ATL, ATS, Conn)"]
    Worker -->|stats/drain complete| MainThread
```

### Pattern 2: In-Place Filter Chain Update

When only filter chains change, the listener socket is preserved. Old chains drain in-place while new chains handle new connections:

```mermaid
flowchart TD
    Update["Filter-chain-only update"] --> NewFCM["New FilterChainManagerImpl<br/>(new filter chains)"]
    Update --> OldFC["Old filter chains → DrainingFilterChainsManager"]
    NewFCM --> NewConns["New connections use new chains"]
    OldFC --> OldConns["Existing connections drain on old chains"]
```

### Pattern 3: Deferred Listener Destruction

Listeners are not destroyed immediately. They are moved to a draining list and destroyed after all connections are closed and a drain timeout expires.

### Pattern 4: Hot Restart Socket Inheritance

During hot restart, the new Envoy process inherits listen sockets from the old process via domain sockets, ensuring zero downtime.

---

## Navigation

| Part | Topics |
|------|--------|
| **Part 1 (this file)** | Architecture, ListenerManagerImpl, Worker Dispatch, Lifecycle |
| [Part 2](OVERVIEW_PART2_filter_chains.md) | Filter Chain Manager, Matching, ListenerImpl Config |
| [Part 3](OVERVIEW_PART3_active_tcp.md) | ActiveTcpListener, ActiveTcpSocket, Listener Filters, Connection Tracking |
| [Part 4](OVERVIEW_PART4_lds_and_advanced.md) | LDS API, UDP, Draining, Internal Listeners, Advanced Topics |
