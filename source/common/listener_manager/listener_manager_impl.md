# ListenerManagerImpl

**Files:** `source/common/listener_manager/listener_manager_impl.h` / `.cc`  
**Size:** ~22 KB header, ~62 KB implementation  
**Namespace:** `Envoy::Server`

## Overview

`ListenerManagerImpl` is the top-level manager for all Envoy listeners. It runs on the **main thread** and coordinates:

- Adding, updating, and removing listeners (static and dynamic via LDS)
- Creating and sharing listen sockets (with hot-restart support)
- Dispatching listeners to worker threads
- Draining old listeners and filter chains on config changes
- Managing listener lifecycle (warming → active → draining)

## Class Hierarchy

```mermaid
classDiagram
    class ListenerManagerImpl {
        +addOrUpdateListener(config, version_info, modifiable): bool
        +removeListener(name): bool
        +startWorkers(guard_dog, callback)
        +stopWorkers()
        +stopListeners(type, options)
        +listeners(state): vector~ListenerRef~
        +numConnections(): uint64_t
        +createLdsApi(lds_config, xds_config_tracker)
        -active_listeners_: list~ListenerImplPtr~
        -warming_listeners_: list~ListenerImplPtr~
        -workers_: vector~WorkerPtr~
        -draining_filter_chains_manager_: DrainingFilterChainsManager
    }

    class ListenerManager {
        <<interface>>
        +addOrUpdateListener()
        +removeListener()
        +startWorkers()
        +stopWorkers()
        +stopListeners()
        +listeners()
    }

    class ProdListenerComponentFactory {
        +createListenSocket(address, options, bind_type, ...)
        +createNetworkFilterFactoryList(filters, context)
        +createListenerFilterFactoryList(filters, context)
        +createDrainManager(drain_type)
    }

    class ListenerComponentFactory {
        <<interface>>
    }

    class DrainingFilterChainsManager {
        +add(listener, draining_chains)
        +startDrainSequence(drain_timeout, callback)
        -draining_: list~DrainingListenerInfo~
    }

    class ListenerFilterChainFactoryBuilder {
        +buildFilterChain(filter_chain, context): NetworkFilterFactoriesAndMetadata
    }

    ListenerManager <|-- ListenerManagerImpl
    ListenerComponentFactory <|-- ProdListenerComponentFactory
    ListenerManagerImpl *-- DrainingFilterChainsManager
    ListenerManagerImpl --> ProdListenerComponentFactory
    ListenerManagerImpl --> ListenerFilterChainFactoryBuilder
```

## Listener Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Warming : addOrUpdateListener()
    Warming --> Active : listener initialized (RDS ready, etc.)
    Active --> Active : config update (filter-chain-only change)
    Active --> Draining : removeListener() or replaced
    Draining --> [*] : all connections drained + timer expired
    Warming --> [*] : initialization failure
```

## Add/Update Listener Flow

```mermaid
sequenceDiagram
    participant LDS as LDS API / Bootstrap
    participant LM as ListenerManagerImpl
    participant LI as ListenerImpl
    participant FCM as FilterChainManagerImpl
    participant Workers as Worker Threads

    LDS->>LM: addOrUpdateListener(config, version_info)
    LM->>LM: check if listener name exists

    alt New listener
        LM->>LI: ListenerImpl::create(config, ...)
        LI->>FCM: addFilterChains(filter_chain_configs)
        LM->>LM: add to warming_listeners_
        LI->>LI: initialize (wait for RDS, etc.)
        LI-->>LM: initialization complete
        LM->>LM: move from warming to active
        LM->>Workers: addListener(listener) per worker
    else Update existing (filter chain only)
        LM->>LI: newListenerWithFilterChain(new_config)
        LI->>FCM: update filter chains
        LM->>Workers: updateListener(listener)
        LM->>DrainingFilterChainsManager: drain old filter chains
    else Full listener update
        LM->>LI: create new ListenerImpl
        LM->>LM: move old to draining
        LM->>Workers: removeListener + addListener
    end
```

## Worker Dispatch

```mermaid
flowchart TD
    LM["ListenerManagerImpl<br/>(main thread)"] -->|addListener| W1["Worker 1<br/>ConnectionHandlerImpl"]
    LM -->|addListener| W2["Worker 2<br/>ConnectionHandlerImpl"]
    LM -->|addListener| W3["Worker 3<br/>ConnectionHandlerImpl"]

    W1 --> ATL1["ActiveTcpListener"]
    W2 --> ATL2["ActiveTcpListener"]
    W3 --> ATL3["ActiveTcpListener"]
```

## `ProdListenerComponentFactory` — Socket and Factory Creation

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant PLCF as ProdListenerComponentFactory
    participant HR as HotRestarter
    participant OS as Kernel

    LM->>PLCF: createListenSocket(address, options, bind_type)
    PLCF->>HR: getParentSocket(address)
    alt Hot restart: parent has socket
        HR-->>PLCF: parent_fd
        PLCF-->>LM: ListenSocket(parent_fd)
    else No parent
        PLCF->>OS: socket() + bind() + listen()
        OS-->>PLCF: new_fd
        PLCF-->>LM: ListenSocket(new_fd)
    end
```

## `DrainingFilterChainsManager`

Manages the lifecycle of filter chains being replaced. When a listener update only changes filter chains, old filter chains are drained in-place without destroying the listener:

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant DFC as DrainingFilterChainsManager
    participant Workers as Worker Threads
    participant Conns as ActiveConnections

    LM->>DFC: add(listener, old_filter_chains)
    DFC->>Workers: removeFilterChains(old_chains)
    Workers->>Conns: startDraining()
    Note over Conns: Existing connections continue until idle or timeout
    Conns-->>DFC: all connections drained
    DFC->>DFC: schedule listener destruction
```

## Stats

```mermaid
mindmap
  root((ListenerManagerStats))
    Listeners
      listener_added
      listener_modified
      listener_removed
      listener_create_success
      listener_create_failure
      listener_in_place_updated
    State
      total_listeners_warming
      total_listeners_active
      total_listeners_draining
    Connections
      total_filter_chains_draining
```

## Key Configuration Points

| Config | Effect |
|--------|--------|
| `listener.name` | Unique identifier for update/remove |
| `listener.address` | Bind address (IP:port, UDS, internal) |
| `listener.filter_chains` | List of filter chains with match criteria |
| `listener.listener_filters` | Pre-connection filters (TLS inspector, proxy protocol) |
| `listener.drain_type` | `DEFAULT` (graceful) or `MODIFY_ONLY` |
| `listener.per_connection_buffer_limit_bytes` | Watermark buffer limit per connection |
| `listener.enable_reuse_port` | `SO_REUSEPORT` for multi-worker accept |
