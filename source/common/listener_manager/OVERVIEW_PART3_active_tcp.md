# Listener Manager — Overview Part 3: Active TCP & Socket Lifecycle

**Directory:** `source/common/listener_manager/`  
**Part:** 3 of 4 — ActiveTcpListener, ActiveTcpSocket, Listener Filters, Connection Tracking, Connection Balancing

---

## Table of Contents

1. [Active TCP Components](#1-active-tcp-components)
2. [Accept to Connection Flow](#2-accept-to-connection-flow)
3. [ActiveTcpSocket — Listener Filter Chain](#3-activetcpsocket--listener-filter-chain)
4. [Listener Filter Timeout](#4-listener-filter-timeout)
5. [Connection Tracking by Filter Chain](#5-connection-tracking-by-filter-chain)
6. [Connection Balancing Across Workers](#6-connection-balancing-across-workers)
7. [Filter Chain Level Drain](#7-filter-chain-level-drain)
8. [ActiveStreamListenerBase](#8-activestreamlistenerbase)

---

## 1. Active TCP Components

```mermaid
classDiagram
    class ActiveTcpListener {
        +onAccept(socket)
        +newActiveConnection(chain, socket)
        +post(socket)
        +updateListenerConfig(config)
        -accepted_sockets_: list~ActiveTcpSocket~
    }

    class ActiveTcpSocket {
        +startFilterChain()
        +continueFilterChain(success)
        +newConnection()
        -listener_filters_: list~GenericListenerFilter~
        -accepted_socket_: ConnectionSocketPtr
        -listener_filter_timer_: TimerPtr
    }

    class OwnedActiveStreamListenerBase {
        +newConnection(socket, chain)
        +removeFilterChain(chain)
        #connections_by_context_: map~chain, ActiveConnectionsPtr~
    }

    class ActiveConnections {
        -connections_: list~ActiveTcpConnection~
        -filter_chain_: FilterChain
    }

    class ActiveTcpConnection {
        -connection_: ConnectionPtr
        +onEvent(event)
    }

    OwnedActiveStreamListenerBase <|-- ActiveTcpListener
    ActiveTcpListener *-- ActiveTcpSocket
    OwnedActiveStreamListenerBase *-- ActiveConnections
    ActiveConnections *-- ActiveTcpConnection
```

### Data Ownership

```
ActiveTcpListener (1 per listen address, per worker)
  ├── accepted_sockets_: list<ActiveTcpSocket>    (sockets in listener filter phase)
  └── connections_by_context_: map<FilterChain*, ActiveConnections>
        └── connections_: list<ActiveTcpConnection>  (established connections)
              └── connection_: Network::ConnectionPtr
```

---

## 2. Accept to Connection Flow

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant TL as TcpListenerImpl
    participant ATL as ActiveTcpListener
    participant ATS as ActiveTcpSocket
    participant LF1 as TLS Inspector
    participant LF2 as Proxy Protocol
    participant FCM as FilterChainManagerImpl
    participant LI as ListenerImpl
    participant Conn as ConnectionImpl

    OS->>TL: listen fd readable
    TL->>TL: accept4()
    TL->>ATL: onAccept(accepted_socket)

    ATL->>ATS: new ActiveTcpSocket(socket)
    ATL->>ATS: startFilterChain()
    ATS->>ATS: start listener_filter_timer_

    ATS->>LF1: onAccept(callbacks)
    LF1->>LF1: peek TLS ClientHello
    LF1->>ATS: socket().setRequestedServerName("api.example.com")
    LF1-->>ATS: Continue

    ATS->>LF2: onAccept(callbacks)
    LF2->>LF2: read PROXY protocol header
    LF2->>ATS: socket().setRemoteAddress(real_client_ip)
    LF2-->>ATS: Continue

    ATS->>ATS: all listener filters done
    ATS->>ATS: cancel listener_filter_timer_
    ATS->>FCM: findFilterChain(socket, info)
    FCM-->>ATS: FilterChainImpl*

    ATS->>ATL: newActiveConnection(filter_chain, socket)
    ATL->>LI: createNetworkFilterChain(conn, factories)
    ATL->>ATL: add to connections_by_context_[chain]
```

---

## 3. ActiveTcpSocket — Listener Filter Chain

### State Machine

```mermaid
stateDiagram-v2
    [*] --> RunFilters : startFilterChain()
    RunFilters --> WaitForData : filter returns StopIteration
    WaitForData --> RunFilters : continueFilterChain(true)
    WaitForData --> Rejected : continueFilterChain(false)
    RunFilters --> FindChain : all filters passed
    RunFilters --> Rejected : filter rejects
    FindChain --> CreateConn : filter chain found
    FindChain --> Rejected : no filter chain match
    Rejected --> [*] : socket closed
    CreateConn --> [*] : connection created
```

### `GenericListenerFilter` — Matcher + Filter

Each listener filter is wrapped with a predicate matcher. If the predicate doesn't match, the filter is skipped:

```mermaid
sequenceDiagram
    participant ATS as ActiveTcpSocket
    participant GLF as GenericListenerFilter
    participant Matcher as ListenerFilterMatcher
    participant Filter as ListenerFilter

    ATS->>GLF: onAccept(callbacks)
    GLF->>Matcher: matches(socket)
    alt Predicate matches
        Matcher-->>GLF: true
        GLF->>Filter: onAccept(callbacks)
        Filter-->>GLF: Continue or StopIteration
    else Predicate does not match
        Matcher-->>GLF: false
        GLF-->>ATS: Continue (skip)
    end
```

### Socket Metadata Populated by Listener Filters

```mermaid
mindmap
  root((Socket Metadata<br/>after listener filters))
    TLS Inspector
      requestedServerName (SNI)
      detectedTransportProtocol (tls)
      requestedApplicationProtocols (ALPN)
      ja3Hash (TLS fingerprint)
    Proxy Protocol
      remoteAddress (real client IP)
      directRemoteAddress (proxy IP)
    Original Destination
      localAddress (original dest via SO_ORIGINAL_DST)
    HTTP Inspector
      detectedTransportProtocol (raw_buffer)
```

---

## 4. Listener Filter Timeout

Listener filters have a configurable timeout. If filters are still running when the timer fires, the socket is either promoted with partial metadata or rejected:

```mermaid
flowchart TD
    Accept["Socket accepted"] --> Start["startFilterChain()"]
    Start --> Timer["listener_filter_timer_.enableTimer(timeout)"]
    Start --> Filters["Run listener filters"]

    Filters -->|Complete before timeout| OK["continueFilterChain(true)"]
    Timer -->|Fires before completion| Timeout{"continue_on_listener<br/>_filters_timeout?"}
    Timeout -->|true| OK
    Timeout -->|false| Reject["Close socket"]
    OK --> FindChain["findFilterChain(socket)"]
```

### Config

```yaml
listener:
  listener_filters_timeout: 15s         # max time for listener filters
  continue_on_listener_filters_timeout: true  # promote on timeout
```

---

## 5. Connection Tracking by Filter Chain

Connections are grouped by their matched filter chain. This enables per-chain operations:

```mermaid
flowchart TD
    ATL["ActiveTcpListener<br/>(per worker)"] --> Map["connections_by_context_<br/>map: FilterChain* → ActiveConnections"]

    Map --> AC_TLS["ActiveConnections (tls-api)<br/>├── conn1<br/>├── conn2<br/>└── conn3"]
    Map --> AC_Plain["ActiveConnections (plaintext)<br/>├── conn4<br/>└── conn5"]
    Map --> AC_Default["ActiveConnections (default)<br/>└── conn6"]
```

### `ActiveConnections` — Per Filter Chain Group

```mermaid
classDiagram
    class ActiveConnections {
        -connections_: list~ActiveTcpConnection~
        -filter_chain_: FilterChain*
        -listener_: OwnedActiveStreamListenerBase
        +addConnection(conn)
        +removeConnection(conn)
        +numConnections(): size_t
    }

    class ActiveTcpConnection {
        -connection_: ConnectionPtr
        -active_connections_: ActiveConnections
        +onEvent(event)
    }

    ActiveConnections *-- ActiveTcpConnection
```

### Connection Close Flow

```mermaid
sequenceDiagram
    participant Peer as Remote Peer
    participant Conn as ConnectionImpl
    participant ATC as ActiveTcpConnection
    participant AC as ActiveConnections
    participant ATL as ActiveTcpListener

    Peer->>Conn: TCP FIN
    Conn->>ATC: onEvent(RemoteClose)
    ATC->>AC: removeConnection(this)
    AC->>ATL: downstream_cx_destroy++
    ATC->>ATC: deferredDelete(this)
```

---

## 6. Connection Balancing Across Workers

When a connection balancer is configured, the accepting worker can redirect the socket to a less-loaded worker:

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant W0_ATL as Worker0::ActiveTcpListener
    participant Balancer as ConnectionBalancer
    participant W2_ATL as Worker2::ActiveTcpListener

    OS->>W0_ATL: accept on Worker 0
    W0_ATL->>Balancer: pickTargetHandler()
    Balancer-->>W0_ATL: Worker 2 (fewer connections)
    W0_ATL->>W2_ATL: post(accepted_socket)
    W2_ATL->>W2_ATL: startFilterChain()
    W2_ATL->>W2_ATL: create connection on Worker 2
```

### Balancer Types

| Type | Class | Behavior |
|------|-------|---------|
| None | `NopConnectionBalancerImpl` | Socket stays on accepting worker |
| Exact | `ExactConnectionBalancerImpl` | Route to worker with fewest active connections (uses mutex) |

---

## 7. Filter Chain Level Drain

When a listener update replaces filter chains (in-place update), only connections on the old chains are drained. New connections use new chains:

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant CH as ConnectionHandlerImpl
    participant ATL as ActiveTcpListener
    participant AC_Old as ActiveConnections (old chain)
    participant AC_New as ActiveConnections (new chain)

    LM->>CH: removeFilterChains(listener_tag, old_chains)
    CH->>ATL: removeFilterChain(old_chain)
    ATL->>AC_Old: startDraining()
    Note over AC_Old: Existing connections continue until closed

    Note over ATL: New connections use new chain
    ATL->>AC_New: newConnection(socket)

    loop as old connections close
        AC_Old->>ATL: connection closed
    end
    AC_Old-->>CH: all drained (callback)
```

### Drain State Machine (Per Filter Chain)

```mermaid
stateDiagram-v2
    [*] --> Active : filter chain created
    Active --> Draining : startDraining()
    Draining --> Draining : connections closing gradually
    Draining --> [*] : last connection closed
```

---

## 8. ActiveStreamListenerBase

`ActiveStreamListenerBase` is the shared base for TCP (and potentially QUIC) stream-oriented listeners:

```mermaid
classDiagram
    class ActiveStreamListenerBase {
        +newConnection(socket, chain, stream_info)
        +removeFilterChain(chain)
        +onFilterChainDraining(chain)
        #removeSocket(socket)
        #listener_: ListenerImpl*
        #parent_: ConnectionHandlerImpl
    }

    class OwnedActiveStreamListenerBase {
        #connections_by_context_: map~chain, ActiveConnectionsPtr~
        +numConnections(): uint64_t
    }

    class ActiveListenerImplBase {
        <<interface>>
    }

    ActiveListenerImplBase <|-- ActiveStreamListenerBase
    ActiveStreamListenerBase <|-- OwnedActiveStreamListenerBase
    OwnedActiveStreamListenerBase <|-- ActiveTcpListener
```

### Key Methods

| Method | Purpose |
|--------|---------|
| `newConnection(socket, chain)` | Create `ConnectionImpl`, apply network filter chain, add to `ActiveConnections` |
| `removeFilterChain(chain)` | Drain connections on a specific chain |
| `onFilterChainDraining(chain)` | Called when all connections on a drained chain are closed |
| `removeSocket(socket)` | Remove an `ActiveTcpSocket` from the pending list |

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_architecture.md) | Architecture, ListenerManagerImpl, Worker Dispatch, Lifecycle |
| [Part 2](OVERVIEW_PART2_filter_chains.md) | Filter Chain Manager, Matching, ListenerImpl Config |
| **Part 3 (this file)** | ActiveTcpListener, ActiveTcpSocket, Listener Filters, Connection Tracking |
| [Part 4](OVERVIEW_PART4_lds_and_advanced.md) | LDS API, UDP, Draining, Internal Listeners, Advanced Topics |
