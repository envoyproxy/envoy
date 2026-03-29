# Socket & IO Handle System

**Files:**
- `source/common/network/io_socket_handle_base_impl.h/.cc`
- `source/common/network/io_socket_handle_impl.h/.cc`
- `source/common/network/io_uring_socket_handle_impl.h/.cc`
- `source/common/network/socket_impl.h/.cc`
- `source/common/network/socket_interface.h` / `socket_interface_impl.h/.cc`
- `source/common/network/socket_option_impl.h/.cc`
- `source/common/network/socket_option_factory.h/.cc`
**Namespace:** `Envoy::Network`

## Overview

The socket/IO system is organized in three layers:
1. **`IoHandle`** — abstracts raw OS I/O (syscalls: readv, writev, sendmsg, recvmsg, accept, connect)
2. **`Socket`** — adds addressing, socket options, and connection metadata on top of `IoHandle`
3. **`SocketInterface`** — an injectable singleton factory that creates `IoHandle` instances (enabling io_uring backend)

## Layer Architecture

```mermaid
flowchart TD
    subgraph Layer3["Layer 3: Connection / Listener"]
        CI["ConnectionImpl<br/>(owns Socket + FilterManager)"]
        TL["TcpListenerImpl<br/>(accepts from ListenSocket)"]
    end

    subgraph Layer2["Layer 2: Socket"]
        SI["SocketImpl<br/>(owns IoHandle + options + ConnectionInfo)"]
        CSI["ConnectionSocketImpl<br/>(adds SNI, ALPN, transport protocol, JA3)"]
        LSI["ListenSocketImpl<br/>(adds bind/setupSocket)"]
    end

    subgraph Layer1["Layer 1: IoHandle"]
        ISHI["IoSocketHandleImpl<br/>(epoll/kqueue, readv/writev, sendmsg)"]
        IURI["IoUringSocketHandleImpl<br/>(io_uring SQE-based)"]
    end

    subgraph Factory["SocketInterface (singleton factory)"]
        SIF["SocketInterfaceImpl<br/>(creates IoSocketHandleImpl or IoUringSocketHandleImpl)"]
    end

    CI --> CSI
    TL --> LSI
    CSI --> SI
    LSI --> SI
    SI --> ISHI
    SI --> IURI
    SIF --> ISHI
    SIF --> IURI
```

## IoHandle Hierarchy

```mermaid
classDiagram
    class IoHandle {
        <<interface>>
        +read(buffer, max_length): IoResult
        +readv(max_length, iov, num_iov): IoResult
        +write(buffer): IoResult
        +writev(iov, num_iov): IoResult
        +sendmsg(iov, num_iov, flags, self_ip, peer_addr): IoResult
        +recvmsg(iov, num_iov, self_port, output): IoResult
        +recvmmsg(msgs, self_port, output): IoResult
        +accept(addr): AcceptedSocketPtr
        +connect(addr): IoResult
        +bind(addr): IoCallResult
        +listen(backlog): IoCallResult
        +enableFileEvents(events)
        +close(): IoCallResult
        +fd(): os_fd_t
    }

    class IoSocketHandleBaseImpl {
        +fd(): os_fd_t
        +getOption(level, optname): IoCallResult
        +setOption(level, optname, val): IoCallResult
        +localAddress(): Address::InstancePtr
        +peerAddress(): Address::InstancePtr
        +getRoundTripTime(): absl::optional~Duration~
        +getCongestionWindowInBytes(): absl::optional~uint64_t~
        -fd_: os_fd_t
    }

    class IoSocketHandleImpl {
        +readv(max_length, iov, num_iov): IoResult
        +writev(iov, num_iov): IoResult
        +sendmsg(iov, num_iov, flags, self_ip, peer_addr): IoResult
        +recvmsg(iov, num_iov, self_port, output): IoResult
        +recvmmsg(msgs, self_port, output): IoResult
        +accept(addr): AcceptedSocketPtr
        +enableFileEvents(events)
        -file_event_: Event::FileEventPtr
        -quic_src_addr_cache_: AddressPtr
    }

    class IoUringSocketHandleImpl {
        +read(buffer, max): IoResult
        +write(buffer): IoResult
        +connect(addr): IoResult
        -io_uring_socket_: IoUringSocket
        -socket_type_: Accept/Server/Client
    }

    IoHandle <|-- IoSocketHandleBaseImpl
    IoSocketHandleBaseImpl <|-- IoSocketHandleImpl
    IoSocketHandleBaseImpl <|-- IoUringSocketHandleImpl
```

## `IoSocketHandleImpl` — Event-Driven I/O

`IoSocketHandleImpl` integrates directly with the `Event::Dispatcher`'s event loop via `enableFileEvents()`:

```mermaid
sequenceDiagram
    participant CI as ConnectionImpl
    participant IOH as IoSocketHandleImpl
    participant Dispatcher as Event::Dispatcher
    participant OS as Kernel

    CI->>IOH: enableFileEvents(READ | WRITE)
    IOH->>Dispatcher: createFileEvent(fd, callback, READ|WRITE)

    OS->>Dispatcher: epoll/kqueue triggers (readable)
    Dispatcher->>IOH: file_event_callback(READ)
    IOH->>CI: onFileEvent(READ)
    CI->>IOH: readv(iov, num_iov)
    IOH->>OS: readv() syscall
    OS-->>IOH: bytes
```

## `IoUringSocketHandleImpl` — io_uring Backend

For Linux kernels with io_uring support, async I/O is submitted as SQEs (Submission Queue Entries) and completed via CQEs (Completion Queue Entries):

```mermaid
sequenceDiagram
    participant CI as ConnectionImpl
    participant IURI as IoUringSocketHandleImpl
    participant IUS as IoUringSocket
    participant Ring as io_uring Ring
    participant OS as Kernel

    CI->>IURI: read(buffer, max_length)
    IURI->>IUS: submitReadRequest(buffer)
    IUS->>Ring: io_uring_prep_readv(SQE)
    Ring->>OS: Async I/O submitted

    OS-->>Ring: I/O complete (CQE ready)
    Ring->>IUS: processCompletionEvent(CQE)
    IUS-->>IURI: onReadCompleted(bytes)
    IURI-->>CI: IoResult
```

## Socket Hierarchy

```mermaid
classDiagram
    class Socket {
        <<interface>>
        +ioHandle(): IoHandle
        +connectionInfoProvider(): ConnectionInfoProvider
        +addOption(option)
        +addOptions(options)
        +getSocketOption(level, name, val): IoCallResult
        +setSocketOption(level, name, val): IoCallResult
        +bind(address): IoCallResult
        +listen(backlog): IoCallResult
        +close()
        +socketType(): Type
        +addressType(): Address::Type
    }

    class SocketImpl {
        -io_handle_: IoHandlePtr
        -connection_info_provider_: ConnectionInfoSetterImpl
        -options_: SocketOptionsList
    }

    class ConnectionSocketImpl {
        +setRequestedServerName(sni)
        +setTransportProtocol(protocol)
        +setRequestedApplicationProtocols(protocols)
        +setDetectedTransportProtocol(protocol)
        +ja3Hash(): string_view
        +roundTripTime(): absl::optional~Duration~
    }

    class ClientSocketImpl

    class ListenSocketImpl {
        +setupSocket(options, bind_to_port)
        +bindToPort(): bool
    }

    class AcceptedSocketImpl {
        +newAcceptedConnection(): size_t
    }

    Socket <|-- SocketImpl
    SocketImpl <|-- ConnectionSocketImpl
    SocketImpl <|-- ListenSocketImpl
    ConnectionSocketImpl <|-- ClientSocketImpl
    ConnectionSocketImpl <|-- AcceptedSocketImpl
```

## Socket Options System

### `SocketOptionImpl`

Wraps a single `setsockopt()` call, applied at a specific lifecycle state:

```mermaid
classDiagram
    class SocketOptionImpl {
        +isSupported(): bool
        +setOption(socket, state): bool
        +optionDetails(socket, state): OptionDetails
        -optname_: SocketOptionName
        -value_: buffer
        -in_state_: SocketState
    }

    class SocketState {
        <<enum>>
        PreBind
        PreConnect
        PreListen
        Bound
        Connected
    }

    SocketOptionImpl --> SocketState
```

### `AddrFamilyAwareSocketOptionImpl`

Applies different options for IPv4 vs IPv6:

```mermaid
flowchart TD
    AFAS["AddrFamilyAwareSocketOptionImpl::setOption(socket, state)"] --> B{socket.addressType()?}
    B -->|IPv4| C["ipv4_option_.setOption(socket, state)"]
    B -->|IPv6| D["ipv6_option_.setOption(socket, state)"]
    B -->|Any| E["Try ipv4_option_ first,<br/>then ipv6_option_"]
```

### `SocketOptionFactory` — Common Options

```mermaid
mindmap
  root(SocketOptionFactory)
    buildTcpKeepaliveOptions
      SO_KEEPALIVE
      TCP_KEEPIDLE
      TCP_KEEPINTVL
      TCP_KEEPCNT
    buildIpFreebindOptions
      IP_FREEBIND
      IPV6_FREEBIND
    buildIpTransparentOptions
      IP_TRANSPARENT
      IPV6_TRANSPARENT
    buildSocketMarkOptions
      SO_MARK
    buildUdpGroOptions
      UDP_GRO
    buildReusePortOptions
      SO_REUSEPORT
    buildTcpFastOpenOptions
      TCP_FASTOPEN
```

## `SocketInterface` — Platform Factory

`SocketInterfaceSingleton` is an injectable singleton that decouples `IoHandle` creation from `ConnectionImpl`:

```mermaid
flowchart TD
    SIS["SocketInterfaceSingleton::get()"] --> SIF["SocketInterfaceImpl (default)"]
    SIF --> B{Platform config?}
    B -->|Standard epoll/kqueue| ISHI["IoSocketHandleImpl"]
    B -->|io_uring enabled| IURI["IoUringSocketHandleImpl"]

    SIF --> TCP["socket(AF_INET, SOCK_STREAM)"]
    SIF --> UDP["socket(AF_INET, SOCK_DGRAM)"]
    SIF --> Pipe["socket(AF_UNIX, SOCK_STREAM)"]
```

## TCP/UDP Socket Option Application Lifecycle

```mermaid
sequenceDiagram
    participant LM as ListenerManager
    participant LS as ListenSocket
    participant SO as SocketOption

    LM->>LS: addOptions(options)
    LM->>LS: setupSocket()
    LS->>SO: setOption(socket, PreBind)
    LS->>LS: bind(address)
    LS->>SO: setOption(socket, Bound)
    LS->>LS: listen(backlog)
    LS->>SO: setOption(socket, PreListen)
    Note over LS: Socket ready for accept()
```

## `ConnectionInfoSetterImpl` — Connection Metadata

`ConnectionSocketImpl` exposes rich connection metadata via `ConnectionInfoSetterImpl`:

| Field | Purpose |
|-------|---------|
| `localAddress()` | Local socket address |
| `remoteAddress()` | Remote peer address |
| `directRemoteAddress()` | Pre-XFF actual remote IP |
| `requestedServerName()` | TLS SNI from client |
| `transportProtocol()` | "raw_buffer", "tls", "quic" |
| `requestedApplicationProtocols()` | ALPN protocols requested |
| `ja3Hash()` | TLS JA3 fingerprint |
| `roundTripTime()` | TCP RTT estimate |
