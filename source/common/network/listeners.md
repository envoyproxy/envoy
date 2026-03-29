# TCP and UDP Listeners

**Files:**
- `source/common/network/base_listener_impl.h/.cc`
- `source/common/network/tcp_listener_impl.h/.cc`
- `source/common/network/udp_listener_impl.h/.cc`
- `source/common/network/listen_socket_impl.h/.cc`
- `source/common/network/listener_filter_buffer_impl.h/.cc`
**Namespace:** `Envoy::Network`

## Overview

Envoy's listener layer accepts new connections (TCP) or datagrams (UDP) from the OS, applies listener filters (L4 pre-processing before the connection is handed to a filter chain), enforces connection limits and overload shedding, and dispatches to the appropriate worker thread.

## Class Hierarchy

```mermaid
classDiagram
    class Listener {
        <<interface>>
        +enable()
        +disable()
        +destroy()
    }

    class BaseListenerImpl {
        #dispatcher_: Dispatcher
        #socket_: SocketSharedPtr
        +localAddress(): Address::InstancePtr
    }

    class TcpListenerImpl {
        +onSocketEvent(events)
        -accept_filters_: vector~ListenerFilter~
        -connection_limit_: ConnectionLimit
        -load_shed_point_: LoadShedPoint
    }

    class UdpListenerImpl {
        +onSocketEvent(events)
        +onReadReady()
        +send(data): IoResult
        -udp_packet_processor_: UdpPacketProcessor
        -worker_router_: UdpListenerWorkerRouterImpl
    }

    Listener <|-- BaseListenerImpl
    BaseListenerImpl <|-- TcpListenerImpl
    BaseListenerImpl <|-- UdpListenerImpl
```

## Listen Socket Hierarchy

```mermaid
classDiagram
    class ListenSocketImpl {
        +setupSocket(options, bind_to_port)
        +bindToPort(): bool
    }

    class TcpListenSocket
    class UdpListenSocket
    class UdsListenSocket
    class InternalListenSocket
    class AcceptedSocketImpl

    ListenSocketImpl <|-- TcpListenSocket
    ListenSocketImpl <|-- UdpListenSocket
    ListenSocketImpl <|-- UdsListenSocket
    ListenSocketImpl <|-- InternalListenSocket
    ListenSocketImpl <|-- AcceptedSocketImpl
```

## TCP Accept Flow

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant TL as TcpListenerImpl
    participant LFB as ListenerFilterBuffer
    participant LF as ListenerFilter
    participant CM as ConnectionManager

    OS->>TL: socket writable event (new connection)
    TL->>TL: onSocketEvent(READABLE)

    loop accept loop
        TL->>OS: accept4() syscall
        OS-->>TL: new_fd + remote_addr
        TL->>TL: check connection limit
        TL->>TL: check load_shed_point (overload)

        alt Connection accepted
            TL->>TL: create AcceptedSocketImpl
            TL->>LFB: create ListenerFilterBufferImpl
            TL->>LF: onAccept(accepted_socket)
            LF-->>TL: Continue
            TL->>CM: newConnection(accepted_socket)
        else Limit exceeded
            TL->>OS: close(new_fd)
        end
    end
```

## Listener Filter Buffer — Peek Without Consuming

`ListenerFilterBufferImpl` lets listener filters peek at the first bytes of a connection (e.g., for TLS detection, proxy protocol parsing) using `recv(MSG_PEEK)` so the data is not consumed:

```mermaid
sequenceDiagram
    participant LF as ListenerFilter (e.g. TLS inspector)
    participant LFB as ListenerFilterBufferImpl
    participant OS as Kernel

    LF->>LFB: peekData(min_bytes)
    LFB->>OS: recv(fd, buf, max_bytes, MSG_PEEK)
    OS-->>LFB: peeked_bytes (data still in kernel buffer)
    LFB-->>LF: Buffer::ConstRawSlice (read-only view)
    LF->>LF: inspect bytes (e.g. detect TLS ClientHello)
    LF->>LFB: doneWithData()
    Note over LFB: Data remains in socket buffer for codec to read
```

## Listener Filter Match Predicates

`filter_matcher.h` provides composable predicates that determine whether a listener filter applies to a given accepted connection:

```mermaid
flowchart TD
    Config["ListenerFilter matcher config"] --> Builder["ListenerFilterMatcherBuilder"]
    Builder --> Tree{matcher type?}
    Tree -->|any| AnyMatcher["ListenerFilterAnyMatcher<br/>(always matches)"]
    Tree -->|not| NotMatcher["ListenerFilterNotMatcher<br/>(negates child)"]
    Tree -->|and| AndMatcher["ListenerFilterAndMatcher<br/>(all children must match)"]
    Tree -->|or| OrMatcher["ListenerFilterOrMatcher<br/>(any child must match)"]
    Tree -->|dst_port| PortMatcher["ListenerFilterDstPortMatcher<br/>(port range check)"]

    Accepted["Accepted connection"] --> AnyMatcher
    Accepted --> PortMatcher
    PortMatcher --> B{dst_port in range?}
    B -->|Yes| Apply["Apply filter"]
    B -->|No| Skip["Skip filter (return Continue)"]
```

## Overload / Connection Limit Protection

`TcpListenerImpl` integrates with two protection mechanisms:

```mermaid
flowchart TD
    Accept["New connection accepted"] --> A{Global connection<br/>limit reached?}
    A -->|Yes| B["Reject: close(new_fd)<br/>cx_overflow++ stat"]
    A -->|No| C{LoadShedPoint<br/>check (overload)?}
    C -->|Shed| D["Reject: close(new_fd)<br/>overload_reject++ stat"]
    C -->|Accept| E["Hand to listener filter chain"]
```

| Protection | Source | Stat |
|------------|--------|------|
| Connection limit | `config.globalConnectionLimit()` | `listener.downstream_cx_overflow` |
| Load shedding | `OverloadManager::LoadShedPoint` | `listener.downstream_cx_overload_reject` |

## UDP Listener Flow

UDP listeners have different semantics — there are no connections, only datagrams. `UdpListenerImpl` uses `recvmsg`/`recvmmsg` (with optional GRO) to batch-receive packets:

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant UL as UdpListenerImpl
    participant PP as UdpPacketProcessor
    participant Router as UdpListenerWorkerRouter

    OS->>UL: socket readable event
    UL->>UL: onReadReady()

    alt GRO enabled
        UL->>OS: recvmmsg(msgs[], flags=MSG_WAITFORONE)
        OS-->>UL: N datagrams in one syscall
    else Standard
        UL->>OS: recvmsg(msg, flags)
        OS-->>UL: 1 datagram
    end

    loop for each packet
        UL->>PP: processPacket(local_addr, peer_addr, buffer, recv_time)
    end

    UL->>OS: sendmsg (if any UDP response queued)
```

## UDP Worker Routing

For multi-worker setups, UDP packets from the same peer are consistently routed to the same worker thread:

```mermaid
flowchart TD
    Pkt["UDP packet from 10.0.0.1:5000"] --> Router["UdpListenerWorkerRouterImpl"]
    Router -->|hash(peer_addr)| W0["Worker 0"]
    Router -->|hash(peer_addr)| W1["Worker 1"]
    Router -->|hash(peer_addr)| W2["Worker 2"]
    W1 -->|all packets from 10.0.0.1:5000| Session["UDP Session Handler"]
```

## `GenericListenerFilterImplBase<T>`

A template that wraps any listener filter with a `ListenerFilterMatcher`, short-circuiting `onAccept()` when the predicate doesn't match:

```mermaid
sequenceDiagram
    participant CM as ConnectionManager
    participant GLFI as GenericListenerFilterImplBase
    participant Matcher as ListenerFilterMatcher
    participant Filter as Actual ListenerFilter

    CM->>GLFI: onAccept(cb)
    GLFI->>Matcher: matches(accepted_socket)
    alt Matches
        Matcher-->>GLFI: true
        GLFI->>Filter: onAccept(cb)
        Filter-->>GLFI: Continue or StopIteration
    else No match
        Matcher-->>GLFI: false
        GLFI-->>CM: Continue (skip this filter)
    end
```

## Socket Setup Lifecycle

```mermaid
sequenceDiagram
    participant LM as ListenerManagerImpl
    participant LS as TcpListenSocket
    participant SO as SocketOption

    LM->>LS: new TcpListenSocket(address, options, bind_to_port)
    LS->>LS: socket(AF_INET, SOCK_STREAM)
    LS->>SO: setOption(socket, PreBind)
    LS->>LS: setsockopt(SO_REUSEPORT)
    LS->>LS: bind(address) if bind_to_port
    LS->>SO: setOption(socket, Bound)
    LS->>LS: listen(backlog)
    LS->>SO: setOption(socket, PreListen)
    LM->>LS: ready for accept()
```
