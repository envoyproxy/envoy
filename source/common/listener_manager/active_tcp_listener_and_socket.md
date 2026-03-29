# ActiveTcpListener & ActiveTcpSocket

**Files:**
- `source/common/listener_manager/active_tcp_listener.h/.cc`
- `source/common/listener_manager/active_tcp_socket.h/.cc`
- `source/common/listener_manager/active_stream_listener_base.h/.cc`
**Namespace:** `Envoy::Server`

## Overview

`ActiveTcpListener` is the per-worker wrapper for a TCP listener. When a connection is accepted, it creates an `ActiveTcpSocket` to run listener filters, then selects a filter chain and creates a `ConnectionImpl`. `ActiveStreamListenerBase` provides the base logic for managing connections grouped by filter chain.

## Class Hierarchy

```mermaid
classDiagram
    class ActiveTcpListener {
        +onAccept(socket)
        +newActiveConnection(filter_chain, socket, ...)
        +onReject(reason)
        +post(socket)
        +updateListenerConfig(config)
    }

    class TcpListenerCallbacks {
        <<interface>>
        +onAccept(socket)
        +onReject(reason)
    }

    class BalancedConnectionHandler {
        <<interface>>
        +post(socket)
        +numConnections(): uint64_t
    }

    class OwnedActiveStreamListenerBase {
        +newConnection(socket, filter_chain)
        +removeFilterChain(chain)
        #connections_by_context_: map~chain, ActiveConnectionsPtr~
    }

    class ActiveTcpSocket {
        +startFilterChain()
        +continueFilterChain(success)
        +newConnection()
        -listener_filters_: list~GenericListenerFilter~
        -accepted_socket_: ConnectionSocketPtr
    }

    class ListenerFilterManager {
        <<interface>>
        +addAcceptFilter(matcher, filter)
    }

    class ListenerFilterCallbacks {
        <<interface>>
        +socket(): ConnectionSocket
        +dispatcher(): Dispatcher
        +continueFilterChain(success)
    }

    class ActiveConnections {
        -connections_: list~ActiveTcpConnection~
        -filter_chain_: FilterChain
    }

    class ActiveTcpConnection {
        -connection_: ConnectionPtr
        +onEvent(event)
    }

    TcpListenerCallbacks <|-- ActiveTcpListener
    BalancedConnectionHandler <|-- ActiveTcpListener
    OwnedActiveStreamListenerBase <|-- ActiveTcpListener
    ListenerFilterManager <|-- ActiveTcpSocket
    ListenerFilterCallbacks <|-- ActiveTcpSocket
    ActiveTcpListener *-- ActiveTcpSocket
    OwnedActiveStreamListenerBase *-- ActiveConnections
    ActiveConnections *-- ActiveTcpConnection
```

## Accept-to-Connection Flow

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant TL as TcpListenerImpl
    participant ATL as ActiveTcpListener
    participant ATS as ActiveTcpSocket
    participant LF as ListenerFilter (TLS Inspector)
    participant FCM as FilterChainManagerImpl
    participant LI as ListenerImpl
    participant Conn as ConnectionImpl

    OS->>TL: listen fd readable
    TL->>TL: accept4()
    TL->>ATL: onAccept(accepted_socket)
    ATL->>ATS: new ActiveTcpSocket(socket)
    ATL->>ATS: startFilterChain()

    ATS->>LF: onAccept(callbacks)
    LF->>LF: peek TLS ClientHello
    LF->>ATS: socket().setRequestedServerName("api.example.com")
    LF->>ATS: socket().setTransportProtocol("tls")
    LF-->>ATS: Continue

    ATS->>ATS: continueFilterChain(true)
    ATS->>FCM: findFilterChain(socket, info)
    FCM-->>ATS: FilterChainImpl*

    ATS->>ATL: newActiveConnection(filter_chain, socket)
    ATL->>LI: createNetworkFilterChain(conn, factories)
    LI->>Conn: addReadFilter(http_codec_filter)
    LI->>Conn: addReadFilter(connection_manager)
    ATL->>ATL: add to connections_by_context_[filter_chain]
```

## `ActiveTcpSocket` — Listener Filter Chain

### Listener Filter Iteration

```mermaid
stateDiagram-v2
    [*] --> FilterA : startFilterChain()
    FilterA --> FilterB : Continue
    FilterB --> FilterC : Continue
    FilterC --> FindChain : all filters done
    FilterA --> WaitingForData : StopIteration (need more data)
    WaitingForData --> FilterA : continueFilterChain(true)
    FilterA --> Rejected : continueFilterChain(false)
    FindChain --> NewConnection : filter chain found
    FindChain --> Rejected : no filter chain match
    Rejected --> [*] : socket closed
    NewConnection --> [*] : connection created
```

### Timeout Handling

Listener filters have a configurable timeout. If filters don't complete within the timeout, the socket is either promoted (with whatever metadata is available) or rejected:

```mermaid
flowchart TD
    Accept["Socket accepted"] --> Start["startFilterChain()"]
    Start --> Timer["Start listener_filters_timeout timer"]

    Start --> LFChain["Run listener filters"]

    LFChain -->|all complete in time| OK["continueFilterChain(true)"]
    Timer -->|fires before completion| Timeout{continue_on_timeout?}
    Timeout -->|Yes| OK
    Timeout -->|No| Reject["Close socket"]
```

### `GenericListenerFilter` — Wrapped with Matcher

Each listener filter is wrapped in a `GenericListenerFilter` that checks a `ListenerFilterMatcher` predicate first:

```mermaid
sequenceDiagram
    participant ATS as ActiveTcpSocket
    participant GLF as GenericListenerFilter
    participant Matcher as ListenerFilterMatcher
    participant Filter as ListenerFilter

    ATS->>GLF: onAccept(callbacks)
    GLF->>Matcher: matches(socket)
    alt Match
        Matcher-->>GLF: true
        GLF->>Filter: onAccept(callbacks)
        Filter-->>GLF: Continue / StopIteration
    else No match
        Matcher-->>GLF: false
        GLF-->>ATS: Continue (skip filter)
    end
```

## Connection Balancing

`ActiveTcpListener` supports connection balancing: an accepted socket can be redirected to another worker if that worker has fewer connections:

```mermaid
sequenceDiagram
    participant W0 as Worker 0 (ActiveTcpListener)
    participant Balancer as ConnectionBalancer
    participant W2 as Worker 2 (ActiveTcpListener)

    W0->>Balancer: pickTargetHandler()
    Balancer-->>W0: Worker 2 has fewer connections

    W0->>W2: post(accepted_socket)
    W2->>W2: startFilterChain() on posted socket
```

## `ActiveConnections` — Per Filter Chain

Connections are grouped by their matched filter chain. This enables filter-chain-level drain (when a filter chain is updated, only connections using that chain are drained):

```mermaid
flowchart TD
    ATL["ActiveTcpListener"] --> ACMap["connections_by_context_<br/>map: FilterChain* → ActiveConnections"]
    ACMap --> AC1["ActiveConnections<br/>(filter_chain=tls-api)<br/>connections: [conn1, conn2, conn3]"]
    ACMap --> AC2["ActiveConnections<br/>(filter_chain=plaintext-health)<br/>connections: [conn4]"]
    ACMap --> AC3["ActiveConnections<br/>(filter_chain=default)<br/>connections: [conn5, conn6]"]
```

## `ActiveTcpConnection` — Per Connection Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Active : newConnection()
    Active --> Active : data read/write
    Active --> Closing : RemoteClose / LocalClose
    Closing --> [*] : removed from ActiveConnections
```

```mermaid
sequenceDiagram
    participant ATC as ActiveTcpConnection
    participant Conn as ConnectionImpl
    participant AC as ActiveConnections
    participant ATL as ActiveTcpListener

    Conn->>ATC: onEvent(RemoteClose)
    ATC->>AC: remove(this)
    AC->>ATL: stat: downstream_cx_destroy++
    ATC->>ATC: deferred delete
```

## Filter Chain Draining

When a filter chain is replaced, only the connections on that specific chain drain:

```mermaid
sequenceDiagram
    participant CH as ConnectionHandlerImpl
    participant ATL as ActiveTcpListener
    participant AC as ActiveConnections (old chain)
    participant Conns as ActiveTcpConnection list

    CH->>ATL: removeFilterChain(old_chain)
    ATL->>AC: startDraining()
    Note over Conns: Existing connections continue until closed
    Conns->>AC: onEvent(RemoteClose) per connection
    AC-->>ATL: last connection drained
    ATL->>CH: drain complete callback
```

## Key Data Relationships

```
ActiveTcpListener (per worker, per listen address)
  ├── accepted_sockets_: list<ActiveTcpSocket>
  │     └── listener_filters_: list<GenericListenerFilter>
  └── connections_by_context_: map<FilterChain*, ActiveConnections>
        └── connections_: list<ActiveTcpConnection>
              └── connection_: Network::ConnectionPtr
```
