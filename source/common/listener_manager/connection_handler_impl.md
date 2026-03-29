# ConnectionHandlerImpl

**Files:** `source/common/listener_manager/connection_handler_impl.h` / `.cc`  
**Size:** ~8 KB header, ~19 KB implementation  
**Namespace:** `Envoy::Server`

## Overview

`ConnectionHandlerImpl` is the **per-worker thread** connection handler. Each worker thread has one `ConnectionHandlerImpl` that owns all the active listeners and connections for that thread. It creates TCP/UDP listeners, manages listener lifecycle, and dispatches connection operations.

## Class Hierarchy

```mermaid
classDiagram
    class ConnectionHandlerImpl {
        +addListener(overridden_listener, config, runtime): ActiveListenerPtr
        +removeListeners(listener_tag)
        +removeFilterChains(listener_tag, chains, callback)
        +stopListeners(listener_tag, options)
        +numConnections(): uint64_t
        +findByAddress(address): InternalListenerOptRef
        -per_handler_listeners_: map~tag, ActiveListenerDetails~
        -dispatcher_: Dispatcher
        -worker_index_: uint32_t
    }

    class ConnectionHandler {
        <<interface>>
    }

    class TcpConnectionHandler {
        <<interface>>
        +createListener(config): ActiveTcpListenerPtr
    }

    class UdpConnectionHandler {
        <<interface>>
        +getUdpListenerCallbacks(listener_tag): UdpListenerCallbacks
    }

    class InternalListenerManager {
        <<interface>>
        +findByAddress(address): InternalListenerOptRef
    }

    class ActiveListenerDetails {
        +per_address_details_: vector~PerAddressActiveListenerDetails~
        +listener_tag_: uint64_t
        +listener_: ListenerConfig*
    }

    class PerAddressActiveListenerDetails {
        +typed_listener_: ActiveTcpListener / ActiveUdpListener / InternalListener
        +address_: Address::InstanceConstSharedPtr
    }

    ConnectionHandler <|-- ConnectionHandlerImpl
    TcpConnectionHandler <|-- ConnectionHandlerImpl
    UdpConnectionHandler <|-- ConnectionHandlerImpl
    InternalListenerManager <|-- ConnectionHandlerImpl
    ConnectionHandlerImpl *-- ActiveListenerDetails
    ActiveListenerDetails *-- PerAddressActiveListenerDetails
```

## Worker Thread Architecture

```mermaid
flowchart TD
    subgraph MainThread["Main Thread"]
        LM["ListenerManagerImpl"]
    end

    subgraph Worker0["Worker Thread 0"]
        CH0["ConnectionHandlerImpl"]
        ATL0["ActiveTcpListener<br/>(:443)"]
        ATL0b["ActiveTcpListener<br/>(:80)"]
        UDP0["ActiveUdpListener<br/>(:53)"]
    end

    subgraph Worker1["Worker Thread 1"]
        CH1["ConnectionHandlerImpl"]
        ATL1["ActiveTcpListener<br/>(:443)"]
        ATL1b["ActiveTcpListener<br/>(:80)"]
        UDP1["ActiveUdpListener<br/>(:53)"]
    end

    LM -->|dispatch addListener| CH0
    LM -->|dispatch addListener| CH1
    CH0 --> ATL0 & ATL0b & UDP0
    CH1 --> ATL1 & ATL1b & UDP1
```

## `addListener` Flow

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant CH as ConnectionHandlerImpl
    participant ATL as ActiveTcpListener
    participant TL as TcpListenerImpl

    LM->>CH: addListener(config, runtime) [on worker thread]
    CH->>CH: create ActiveListenerDetails(listener_tag)

    loop for each listen address
        CH->>ATL: new ActiveTcpListener(config, address, handler)
        ATL->>TL: new TcpListenerImpl(dispatcher, socket, callbacks)
        TL->>OS: enable file event on listen fd
        CH->>CH: store in per_handler_listeners_
    end

    CH-->>LM: ActiveListenerPtr
```

## Listener Lookup

```mermaid
flowchart TD
    Tag["Listener tag: 12345"] --> Map["per_handler_listeners_<br/>map: tag → ActiveListenerDetails"]
    Map --> ALD["ActiveListenerDetails<br/>{per_address_details_: [...]}"]
    ALD --> PAD1["PerAddressActiveListenerDetails<br/>address=0.0.0.0:443<br/>typed_listener_=ActiveTcpListener"]
    ALD --> PAD2["PerAddressActiveListenerDetails<br/>address=[::]:443<br/>typed_listener_=ActiveTcpListener"]
```

## Remove Listeners / Filter Chains

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant CH as ConnectionHandlerImpl
    participant ATL as ActiveTcpListener
    participant Conns as ActiveConnections

    alt Remove entire listener
        LM->>CH: removeListeners(listener_tag)
        CH->>ATL: destroy (stop accepting)
        ATL->>Conns: close all connections
        CH->>CH: remove from per_handler_listeners_
    else Remove specific filter chains (in-place update)
        LM->>CH: removeFilterChains(listener_tag, old_chains, callback)
        CH->>ATL: removeFilterChain(chain)
        ATL->>Conns: drain connections on old chain
        Conns-->>CH: callback when all drained
    end
```

## Internal Listener Support

For Envoy internal communication (e.g., between filters or internal redirect), `ConnectionHandlerImpl` supports internal listeners:

```mermaid
flowchart TD
    Caller["Internal caller<br/>(e.g. filter)"] --> CH["ConnectionHandlerImpl::findByAddress(internal_addr)"]
    CH --> B{Internal listener<br/>at address?}
    B -->|Yes| IL["InternalListener<br/>(handle connection locally)"]
    B -->|No| NotFound["absl::nullopt"]
```

## Stats

Key stats maintained per worker:

| Stat | What it tracks |
|------|---------------|
| `downstream_cx_total` | Total connections accepted |
| `downstream_cx_active` | Currently active connections |
| `downstream_cx_destroy` | Connections destroyed |
| `no_filter_chain_match` | Connections with no matching filter chain |
