# Envoy Network Layer — Overview Part 2: Filters & Listeners

**Directory:** `source/common/network/`  
**Part:** 2 of 4 — Network Filter Manager, Listener Filters, TCP/UDP Listeners, Connection Balancing

---

## Table of Contents

1. [Network Filter System Overview](#1-network-filter-system-overview)
2. [FilterManagerImpl — Read/Write Chain Execution](#2-filtermanagerimpl--readwrite-chain-execution)
3. [Listener Filter System](#3-listener-filter-system)
4. [ListenerFilterBufferImpl — Peeking Data](#4-listenerfilterbufferimpl--peeking-data)
5. [TcpListenerImpl — Accept Loop](#5-tcplistenerimpl--accept-loop)
6. [UdpListenerImpl — Datagram Processing](#6-udplistenerimpl--datagram-processing)
7. [Listen Socket Types](#7-listen-socket-types)
8. [Connection Balancing](#8-connection-balancing)

---

## 1. Network Filter System Overview

Envoy has two separate filter systems:

| System | Layer | Data | Per-unit |
|--------|-------|------|---------|
| **Network filters** (`FilterManagerImpl`) | L4 (TCP/UDP) | Raw `Buffer::Instance` | Per connection |
| **HTTP filters** (`Http::FilterManager`) | L7 (HTTP) | Parsed headers/body | Per HTTP request |

Network filters are installed on a connection once at accept time and run for the entire connection lifetime.

```mermaid
flowchart TD
    subgraph "Connection Established"
        LF["Listener Filters<br/>(pre-connection: TLS inspect, proxy protocol)"]
        LF --> SCI["ServerConnectionImpl created"]
    end

    subgraph "Per-Connection Filter Chain (FilterManagerImpl)"
        SCI --> RF_A["ReadFilter A<br/>(e.g. TLS transport)"]
        RF_A --> RF_B["ReadFilter B<br/>(e.g. HTTP/1 codec)"]
        RF_B --> RF_C["ReadFilter C<br/>(e.g. ConnectionManagerImpl)"]
    end

    subgraph "Per-Request Filter Chain (Http::FilterManager)"
        RF_C --> HF_A["HTTP Filter A<br/>(JWT auth)"]
        HF_A --> HF_B["HTTP Filter B<br/>(rate limit)"]
        HF_B --> Router["Router Filter"]
    end
```

---

## 2. FilterManagerImpl — Read/Write Chain Execution

### Filter Chain Layout

```mermaid
flowchart LR
    subgraph ReadChain["Read Chain (head → tail, forward iteration)"]
        direction LR
        RH["Head"] --> RA["ReadFilter A"] --> RB["ReadFilter B"] --> RT["Tail"]
    end

    subgraph WriteChain["Write Chain (tail → head, reverse iteration)"]
        direction RL
        WH["Head"] <-- WA["WriteFilter A"] <-- WB["WriteFilter B"] <-- WT["Tail"]
    end

    Net["Raw bytes (inbound)"] --> RH
    RT -->|decoded data| App["Application"]
    App -->|response| WT
    WH -->|raw bytes| Net2["Network (outbound)"]
```

### `ActiveReadFilter` and `ActiveWriteFilter`

Each filter in the chain is wrapped in an Active struct that tracks initialization state and provides callback access:

```mermaid
classDiagram
    class ActiveReadFilter {
        +filter_: ReadFilterSharedPtr
        +initialized_: bool
        +onNewConnection(): FilterStatus
        +onData(buffer, end_stream): FilterStatus
        +continueReading()
        +injectReadDataToFilterChain(buffer, end_stream)
        +connection(): Connection
        +upstreamHost(): HostDescriptionConstSharedPtr
    }

    class ActiveWriteFilter {
        +filter_: WriteFilterSharedPtr
        +initialized_: bool
        +onWrite(buffer, end_stream): FilterStatus
        +injectWriteDataToFilterChain(buffer, end_stream)
        +connection(): Connection
    }

    class FilterManagerImpl {
        -read_filters_: list~ActiveReadFilter~
        -write_filters_: list~ActiveWriteFilter~
        -connection_: FilterManagerConnection
        -iter_: list iterator
    }

    FilterManagerImpl *-- ActiveReadFilter
    FilterManagerImpl *-- ActiveWriteFilter
```

### Read Iteration with Stop/Continue

```mermaid
sequenceDiagram
    participant CI as ConnectionImpl
    participant FM as FilterManagerImpl
    participant FA as ReadFilter A
    participant FB as ReadFilter B

    CI->>FM: onRead()
    FM->>FA: onData(buffer, end_stream)
    FA-->>FM: FilterStatus::Continue
    FM->>FB: onData(buffer, end_stream)
    FB-->>FM: FilterStatus::StopIteration
    Note over FB,FM: Data held in connection read_buffer_
    Note over FB: FB does async work
    FB->>FM: continueReading()
    FM->>FM: resume from FB's position
    Note over FM: Chain complete
```

### `injectReadDataToFilterChain`

Allows a filter to synthesize data as if it arrived from the network, bypassing earlier filters:

```mermaid
sequenceDiagram
    participant FB as ReadFilter B
    participant FM as FilterManagerImpl
    participant FC as ReadFilter C

    FB->>FM: injectReadDataToFilterChain(synthetic_buffer, end_stream)
    FM->>FC: onData(synthetic_buffer, end_stream)
    Note over FM,FC: Starts from the filter AFTER B, not from A
```

---

## 3. Listener Filter System

Listener filters run **before** a connection is handed to a filter chain. They can inspect early bytes, modify the accepted socket's metadata (SNI, transport protocol, destination address), or outright reject the connection.

### Filter Application Flow

```mermaid
sequenceDiagram
    participant TL as TcpListenerImpl
    participant GF as GenericListenerFilterImplBase
    participant Matcher as ListenerFilterMatcher
    participant LF as ListenerFilter

    TL->>TL: accept() returns new_fd
    TL->>TL: create AcceptedSocketImpl
    TL->>GF: onAccept(accepted_cb)
    GF->>Matcher: matches(accepted_socket)

    alt Match
        Matcher-->>GF: true
        GF->>LF: onAccept(accepted_cb)
        LF->>LF: peekData / read metadata
        LF-->>GF: FilterStatus::Continue
    else No match
        Matcher-->>GF: false
        GF-->>TL: Continue (skip this filter)
    end

    TL->>TL: newConnection(accepted_socket)
```

### Listener Filter Matchers

```mermaid
classDiagram
    class ListenerFilterMatcher {
        <<interface>>
        +matches(socket): bool
    }

    class AnyMatcher {
        +matches(): always true
    }

    class NotMatcher {
        -child_: ListenerFilterMatcherPtr
        +matches(): NOT child.matches()
    }

    class AndMatcher {
        -matchers_: vector~ListenerFilterMatcherPtr~
        +matches(): ALL children match
    }

    class OrMatcher {
        -matchers_: vector~ListenerFilterMatcherPtr~
        +matches(): ANY child matches
    }

    class DstPortMatcher {
        -port_ranges_: vector~PortRange~
        +matches(): dst_port in ranges
    }

    ListenerFilterMatcher <|-- AnyMatcher
    ListenerFilterMatcher <|-- NotMatcher
    ListenerFilterMatcher <|-- AndMatcher
    ListenerFilterMatcher <|-- OrMatcher
    ListenerFilterMatcher <|-- DstPortMatcher
    NotMatcher *-- ListenerFilterMatcher
    AndMatcher *-- ListenerFilterMatcher
    OrMatcher *-- ListenerFilterMatcher
```

### Matcher Tree Example

```
Config:
  matcher:
    or:
      - dst_port: [443, 8443]
      - and:
          - dst_port: [80, 8080]
          - not: { dst_port: [8080] }

Resulting tree:
  OrMatcher
    ├── DstPortMatcher([443, 8443])
    └── AndMatcher
          ├── DstPortMatcher([80, 8080])
          └── NotMatcher
                └── DstPortMatcher([8080])
```

### Common Listener Filters

| Filter | What it does |
|--------|-------------|
| TLS Inspector | Peeks first bytes to detect TLS ClientHello; sets `transport_protocol=tls` and `requested_server_name` (SNI) |
| HTTP Inspector | Detects HTTP/1.1 vs HTTP/2 from preface bytes |
| Proxy Protocol | Reads PROXY protocol v1/v2 header; sets real remote address in filter state |
| Original Destination | Reads SO_ORIGINAL_DST; sets destination address override |

---

## 4. ListenerFilterBufferImpl — Peeking Data

Listener filters need to inspect connection bytes **without consuming them** from the socket buffer. `ListenerFilterBufferImpl` uses `recv(MSG_PEEK)`:

```mermaid
sequenceDiagram
    participant LF as TLS Inspector Filter
    participant LFB as ListenerFilterBufferImpl
    participant OS as Kernel

    LF->>LFB: peekData(min_bytes=5)
    LFB->>OS: recv(fd, buf, 5, MSG_PEEK)
    OS-->>LFB: 5 bytes (data remains in socket buffer)
    LFB-->>LF: ConstRawSlice view of bytes

    LF->>LF: parse TLS ClientHello
    LF->>Socket: setRequestedServerName("api.example.com")
    LF->>Socket: setTransportProtocol("tls")
    LF-->>TL: Continue

    Note over OS: Data still in socket buffer for codec to read normally
```

### Peek vs Drain

```mermaid
flowchart TD
    A[ListenerFilterBufferImpl] --> B{Operation}
    B -->|peekData| C["recv(fd, buf, n, MSG_PEEK)<br/>Data NOT consumed from kernel buffer"]
    B -->|drain| D["recv(fd, buf, n, 0)<br/>Data consumed from kernel buffer"]
    C -->|typical for listener filters| Inspect["Inspect TLS/HTTP/PROXY header"]
    D -->|for proxy protocol| Consume["Consume PROXY header bytes"]
```

---

## 5. TcpListenerImpl — Accept Loop

### Accept Loop Flow

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant TL as TcpListenerImpl
    participant LS as TcpListenSocket

    OS->>TL: listen socket readable (new connections pending)
    TL->>TL: onSocketEvent(READABLE)

    loop until EAGAIN
        TL->>OS: accept4(listen_fd, addr, SOCK_NONBLOCK|SOCK_CLOEXEC)
        OS-->>TL: new_fd + remote_addr

        TL->>TL: check global connection limit
        alt Limit reached
            TL->>OS: close(new_fd)
            TL->>TL: stat: downstream_cx_overflow++
        else Check load shed point
            TL->>TL: overload_manager.shouldShedLoad(accept_lsp)
            alt Shed
                TL->>OS: close(new_fd)
                TL->>TL: stat: downstream_cx_overload_reject++
            else Accept
                TL->>TL: create AcceptedSocketImpl(new_fd, remote_addr)
                TL->>Listener: onAccept(accepted_socket)
            end
        end
    end
```

### Overload / Limit Protection

```mermaid
flowchart TD
    NewConn["New accepted connection"] --> GL{Global connection<br/>limit check}
    GL -->|exceeds limit| Reject1["close(fd)<br/>downstream_cx_overflow++"]
    GL -->|within limit| LSP{LoadShedPoint<br/>(OverloadManager)?}
    LSP -->|shed| Reject2["close(fd)<br/>downstream_cx_overload_reject++"]
    LSP -->|accept| LF["Run Listener Filters"]
    LF --> CM["newConnection(socket)"]
```

---

## 6. UdpListenerImpl — Datagram Processing

### Packet Receive Loop

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant UL as UdpListenerImpl
    participant PP as UdpPacketProcessor

    OS->>UL: socket readable event
    UL->>UL: onReadReady()

    alt GRO enabled (Linux)
        UL->>OS: recvmmsg(msgs[], flags=MSG_WAITFORONE)
        OS-->>UL: N packets in one syscall (batched)
    else Standard
        loop until EAGAIN
            UL->>OS: recvmsg(msg, 0)
            OS-->>UL: 1 datagram
        end
    end

    loop for each received packet
        UL->>PP: processPacket(local_addr, peer_addr, buffer, recv_time)
    end
```

### UDP Worker Routing

For multi-threaded environments, UDP packets from the same peer must be consistently routed to the same worker:

```mermaid
flowchart TD
    Pkt["UDP Packet<br/>peer=10.0.0.1:5000"] --> Router["UdpListenerWorkerRouterImpl"]
    Router --> Hash["hash(peer_addr) % num_workers"]
    Hash -->|result=1| W1["Worker Thread 1"]
    W1 -->|all packets from 10.0.0.1:5000| Handler["UDP Session Handler"]
```

### UDP Send Path

```mermaid
sequenceDiagram
    participant App
    participant UL as UdpListenerImpl
    participant IOH as IoSocketHandleImpl
    participant OS

    App->>UL: send(UdpSendData{peer_addr, local_addr, buffer})
    UL->>IOH: sendmsg(iov, flags, local_ip, peer_addr)
    IOH->>OS: sendmsg() syscall
    OS-->>IOH: bytes_sent
```

---

## 7. Listen Socket Types

```mermaid
classDiagram
    class ListenSocketImpl {
        +setupSocket(options, bind_to_port)
        +bindToPort(): bool
        -io_handle_: IoHandlePtr
    }

    class TcpListenSocket {
        +Type: Stream (SOCK_STREAM)
    }

    class UdpListenSocket {
        +Type: Datagram (SOCK_DGRAM)
    }

    class UdsListenSocket {
        +Type: Unix domain stream
    }

    class InternalListenSocket {
        +Type: Internal/virtual
    }

    ListenSocketImpl <|-- TcpListenSocket
    ListenSocketImpl <|-- UdpListenSocket
    ListenSocketImpl <|-- UdsListenSocket
    ListenSocketImpl <|-- InternalListenSocket
```

### Socket Lifecycle

```mermaid
sequenceDiagram
    participant LM as ListenerManager
    participant LS as TcpListenSocket

    LM->>LS: new TcpListenSocket(addr, options)
    LS->>OS: socket(AF_INET, SOCK_STREAM, 0)
    LS->>OS: setsockopt(SO_REUSEPORT) if configured
    LS->>LS: applyOptions(PreBind)
    LS->>OS: bind(addr) if bind_to_port
    LS->>LS: applyOptions(Bound)
    LS->>OS: listen(backlog)
    LS->>LS: applyOptions(PreListen)
    LM->>LS: ready for TcpListenerImpl::accept()
```

---

## 8. Connection Balancing

`connection_balancer_impl.h` provides strategies for distributing accepted connections across worker threads:

```mermaid
classDiagram
    class ConnectionBalancer {
        <<interface>>
        +pickTargetHandler(handler): BalancedConnectionHandlerPtr
        +registerHandler(handler)
        +unregisterHandler(handler)
    }

    class NopConnectionBalancerImpl {
        +pickTargetHandler(): current worker (no redistribution)
    }

    class ExactConnectionBalancerImpl {
        +pickTargetHandler(): worker with fewest active connections
        -lock_: mutex
        -handlers_: map~handler, count~
    }

    ConnectionBalancer <|-- NopConnectionBalancerImpl
    ConnectionBalancer <|-- ExactConnectionBalancerImpl
```

### Exact Balancer Flow

```mermaid
flowchart TD
    Accept["TcpListenerImpl accepts connection<br/>on Worker 0"] --> Balancer["ExactConnectionBalancerImpl<br/>pickTargetHandler()"]
    Balancer --> B{Worker with<br/>fewest connections?}
    B -->|Worker 2 has fewest| Redirect["Redirect socket to Worker 2"]
    B -->|Worker 0 has fewest| Keep["Keep on Worker 0"]
    Redirect --> W2["Worker 2 creates<br/>ServerConnectionImpl"]
```

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_architecture_and_connections.md) | Architecture, Connections, Happy Eyeballs, Filter Manager |
| **Part 2 (this file)** | Network Filters, TCP/UDP Listeners, Listener Filters |
| [Part 3](OVERVIEW_PART3_sockets_and_io.md) | Sockets, IoHandles, Socket Options, io_uring |
| [Part 4](OVERVIEW_PART4_addressing_dns_and_utilities.md) | Addressing, CIDR, DNS, Matching, Transport Socket Options |
