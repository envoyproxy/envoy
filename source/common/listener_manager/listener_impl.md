# ListenerImpl

**Files:** `source/common/listener_manager/listener_impl.h` / `.cc`  
**Size:** ~24 KB header, ~66 KB implementation  
**Namespace:** `Envoy::Server`

## Overview

`ListenerImpl` maps a protobuf `Listener` configuration to a runtime listener object. It owns the `FilterChainManagerImpl`, the `ListenSocketFactory`, and all the factory contexts needed to construct filter chains. It implements both `Network::ListenerConfig` (to configure the listener at the network level) and `Network::FilterChainFactory` (to construct filter chains for accepted connections).

## Class Hierarchy

```mermaid
classDiagram
    class ListenerImpl {
        +create(config, name, manager, ...): ListenerImplPtr
        +newListenerWithFilterChain(new_config): ListenerImplPtr
        +initialize()
        +createNetworkFilterChain(connection, filter_factories): bool
        +createListenerFilterChain(manager): bool
        +filterChainManager(): FilterChainManagerImpl
        -filter_chain_manager_: FilterChainManagerImpl
        -socket_factories_: vector~ListenSocketFactoryPtr~
        -name_: string
        -listener_tag_: uint64_t
    }

    class ListenerConfig {
        <<interface>>
        +filterChainFactory(): FilterChainFactory
        +listenSocketFactories(): vector~ListenSocketFactory~
        +listenerFiltersTimeout(): Duration
        +connectionBalancer(): ConnectionBalancer
    }

    class FilterChainFactory {
        <<interface>>
        +createNetworkFilterChain(connection, factories): bool
        +createListenerFilterChain(manager): bool
    }

    class ListenSocketFactoryImpl {
        +getListenSocket(worker_index): SocketSharedPtr
        +localAddress(): Address::InstanceConstSharedPtr
        +sharedSocket(): SocketSharedPtr
        -sockets_: vector~SocketSharedPtr~ (per worker)
    }

    class ListenSocketFactory {
        <<interface>>
    }

    ListenerConfig <|-- ListenerImpl
    FilterChainFactory <|-- ListenerImpl
    ListenSocketFactory <|-- ListenSocketFactoryImpl
    ListenerImpl *-- FilterChainManagerImpl
    ListenerImpl *-- ListenSocketFactoryImpl
```

## Listener Creation Flow

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant LI as ListenerImpl
    participant LSFI as ListenSocketFactoryImpl
    participant FCM as FilterChainManagerImpl
    participant LFCFB as ListenerFilterChainFactoryBuilder

    LM->>LI: create(config, name, manager, ...)
    LI->>LSFI: create socket factory
    LSFI->>OS: socket() + bind()
    LI->>FCM: create FilterChainManagerImpl
    LI->>LFCFB: buildFilterChain(fc_config) per filter chain
    LFCFB-->>FCM: addFilterChains(filter_chains)
    LI->>LI: build listener_filters list
    LI-->>LM: ListenerImplPtr
```

## In-Place Filter Chain Update

When only filter chains change (address and socket options are identical), the listener avoids rebinding:

```mermaid
flowchart TD
    A["New Listener Config"] --> B{Same address and<br/>socket options?}
    B -->|Yes| C["Filter-chain-only update<br/>newListenerWithFilterChain()"]
    B -->|No| D["Full listener replacement<br/>(drain old, create new)"]
    C --> E["New FilterChainManagerImpl<br/>with updated chains"]
    C --> F["Old filter chains → DrainingFilterChainsManager"]
    D --> G["Old listener → draining<br/>New listener → warming"]
```

## `ListenSocketFactoryImpl` — Per-Worker Sockets

With `SO_REUSEPORT`, each worker thread gets its own listen socket. Without it, all workers share a single socket:

```mermaid
flowchart TD
    LSFI["ListenSocketFactoryImpl"] --> B{SO_REUSEPORT?}
    B -->|Yes| Multi["Per-worker sockets:<br/>sockets_[0], sockets_[1], sockets_[2]"]
    B -->|No| Single["Shared socket:<br/>sockets_[0] (shared across workers)"]

    Multi --> W0["Worker 0: accept on sockets_[0]"]
    Multi --> W1["Worker 1: accept on sockets_[1]"]
    Multi --> W2["Worker 2: accept on sockets_[2]"]

    Single --> WAll["All workers: accept on sockets_[0]"]
```

## Factory Contexts

```mermaid
classDiagram
    class ListenerFactoryContextBaseImpl {
        +clusterManager(): ClusterManager
        +dispatcher(): Dispatcher
        +serverScope(): Stats::Scope
        +drainDecision(): DrainDecision
    }

    class PerListenerFactoryContextImpl {
        +listenerScope(): Stats::Scope
        +listenerInfo(): ListenerInfo
        +initManager(): Init::Manager
    }

    class PerFilterChainFactoryContextImpl {
        +drainDecision(): DrainDecision
        +overloadManager(): OverloadManager
    }

    class FactoryContextImplBase {
        <<interface>>
    }

    class ListenerFactoryContext {
        <<interface>>
    }

    FactoryContextImplBase <|-- ListenerFactoryContextBaseImpl
    ListenerFactoryContextBaseImpl <|-- PerListenerFactoryContextImpl
    ListenerFactoryContext <|-- PerListenerFactoryContextImpl
```

## `createNetworkFilterChain` — Per-Connection

Called for every new accepted connection to instantiate the network filter chain:

```mermaid
sequenceDiagram
    participant ATL as ActiveTcpListener
    participant LI as ListenerImpl
    participant FCI as FilterChainImpl
    participant Conn as ConnectionImpl

    ATL->>LI: createNetworkFilterChain(connection, filter_factories)
    LI->>FCI: get filter factory callbacks
    loop for each NetworkFilterFactoryCb
        LI->>Conn: addReadFilter(filter) / addWriteFilter(filter)
    end
    LI-->>ATL: true (success)
```

## `ListenerMessageUtil`

Static helper that compares listener configs to determine the type of update:

```mermaid
flowchart TD
    Old["Old Listener Config"] --> LMU["ListenerMessageUtil"]
    New["New Listener Config"] --> LMU
    LMU --> A{socketOptionsEqual?}
    A -->|No| Full["Full listener replacement"]
    A -->|Yes| B{filterChainOnlyChange?}
    B -->|Yes| InPlace["In-place filter chain update"]
    B -->|No| Full
```

## Configuration Highlights

| Config Field | Purpose |
|-------------|---------|
| `listener_filters` | Pre-connection filters (TLS inspector, proxy protocol, original dst) |
| `listener_filters_timeout` | Max time to wait for listener filters before closing |
| `filter_chains` | List of filter chains with match criteria (SNI, ALPN, source IP, etc.) |
| `default_filter_chain` | Fallback when no `filter_chain_match` matches |
| `per_connection_buffer_limit_bytes` | Watermark buffer limit per connection |
| `connection_balance_config` | Connection balancing across workers |
| `enable_reuse_port` | Whether to use `SO_REUSEPORT` |
| `bind_to_port` | Whether to bind (false for API listeners) |
| `traffic_direction` | INBOUND / OUTBOUND / UNSPECIFIED |
