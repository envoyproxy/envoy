# Envoy Network Layer — Overview Part 3: Sockets & IO Handles

**Directory:** `source/common/network/`  
**Part:** 3 of 4 — Socket Hierarchy, IoHandle, io_uring, Socket Options, Transport Sockets

---

## Table of Contents

1. [IO Abstraction Layers](#1-io-abstraction-layers)
2. [IoHandle Hierarchy](#2-iohandle-hierarchy)
3. [IoSocketHandleImpl — Epoll/Kqueue Path](#3-iosockethandleimpl--epollkqueue-path)
4. [IoUringSocketHandleImpl — io_uring Path](#4-iouringsockethandleimpl--io_uring-path)
5. [Socket Hierarchy](#5-socket-hierarchy)
6. [ConnectionSocketImpl — Rich Metadata](#6-connectionsocketimpl--rich-metadata)
7. [SocketInterface — Platform Factory](#7-socketinterface--platform-factory)
8. [Socket Options System](#8-socket-options-system)
9. [Transport Sockets](#9-transport-sockets)
10. [IO Error Handling](#10-io-error-handling)

---

## 1. IO Abstraction Layers

```mermaid
flowchart TB
    subgraph App["Application Layer"]
        CI["ConnectionImpl<br/>(owns FilterManager + TransportSocket)"]
    end

    subgraph Layer2["Socket Layer"]
        CSI["ConnectionSocketImpl<br/>(metadata: SNI, ALPN, JA3, RTT)"]
        LSI["ListenSocketImpl<br/>(bind, listen, accept)"]
    end

    subgraph Layer1["IoHandle Layer"]
        ISHI["IoSocketHandleImpl<br/>(epoll/kqueue file events)"]
        IURI["IoUringSocketHandleImpl<br/>(io_uring SQE/CQE)"]
    end

    subgraph OS["OS Kernel"]
        FD["File Descriptor (os_fd_t)"]
        URING["io_uring ring buffers"]
        EPOLL["epoll / kqueue"]
    end

    CI --> CSI --> ISHI --> FD
    CI --> CSI --> IURI --> URING
    LSI --> ISHI --> EPOLL
```

---

## 2. IoHandle Hierarchy

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
        +resetFileEvents()
        +close(): IoCallResult
        +fd(): os_fd_t
        +isOpen(): bool
    }

    class IoSocketHandleBaseImpl {
        +fd(): os_fd_t
        +isOpen(): bool
        +getOption(level, name): IoCallResult
        +setOption(level, name, val): IoCallResult
        +localAddress(): AddressPtr
        +peerAddress(): AddressPtr
        +getRoundTripTime(): optional~Duration~
        +getCongestionWindowInBytes(): optional~uint64_t~
        -fd_: os_fd_t
    }

    class IoSocketHandleImpl {
        +readv(): IoResult
        +writev(): IoResult
        +sendmsg(): IoResult
        +recvmsg(): IoResult
        +recvmmsg(): IoResult
        +accept(): AcceptedSocketPtr
        +connect(): IoResult
        +enableFileEvents(events)
        -file_event_: Event::FileEventPtr
        -quic_src_addr_cache_: AddressPtr
    }

    class IoUringSocketHandleImpl {
        +read(buffer, max): IoResult
        +write(buffer): IoResult
        +connect(addr): IoResult
        +accept(addr): AcceptedSocketPtr
        -io_uring_socket_: IoUringSocket
        -socket_type_: SocketType
    }

    IoHandle <|-- IoSocketHandleBaseImpl
    IoSocketHandleBaseImpl <|-- IoSocketHandleImpl
    IoSocketHandleBaseImpl <|-- IoUringSocketHandleImpl
```

---

## 3. IoSocketHandleImpl — Epoll/Kqueue Path

`IoSocketHandleImpl` is the standard I/O path on Linux (epoll) and macOS/BSD (kqueue).

### File Event Registration

```mermaid
sequenceDiagram
    participant CI as ConnectionImpl
    participant IOH as IoSocketHandleImpl
    participant Dispatcher as Event::Dispatcher
    participant Libevent as libevent

    CI->>IOH: enableFileEvents(READ | WRITE)
    IOH->>Dispatcher: createFileEvent(fd, callback, READ|WRITE)
    Dispatcher->>Libevent: event_add(fd, EV_READ|EV_WRITE)

    Note over OS: Data arrives on fd
    Libevent->>Dispatcher: event fires
    Dispatcher->>IOH: file_event_callback(READ)
    IOH->>CI: onFileEvent(READ)
```

### `recvmmsg` — Batched UDP Receive

```mermaid
sequenceDiagram
    participant UL as UdpListenerImpl
    participant IOH as IoSocketHandleImpl
    participant OS as Kernel

    UL->>IOH: recvmmsg(msgs[32], self_port, output)
    IOH->>OS: recvmmsg(fd, msgs, 32, MSG_WAITFORONE, timeout)
    OS-->>IOH: n packets received in msgs[]
    IOH->>IOH: parse ancillary data (src IP from IP_PKTINFO)
    IOH-->>UL: IoResult{ok, n packets}
    UL->>UL: process each packet
```

### `sendmsg` — UDP with Source IP

```mermaid
sequenceDiagram
    participant App
    participant IOH as IoSocketHandleImpl
    participant OS as Kernel

    App->>IOH: sendmsg(iov, num_iov, flags, self_ip, peer_addr)
    IOH->>IOH: build msghdr + cmsg (IP_PKTINFO for self_ip)
    IOH->>OS: sendmsg(fd, msghdr, flags)
    OS-->>IOH: bytes_sent
```

### QUIC Source Address Cache

For QUIC (HTTP/3), the same source address is used repeatedly. `IoSocketHandleImpl` caches it to avoid repeated `getsockname()` calls:

```mermaid
flowchart LR
    Q["QUIC packet send"] --> Cache{"quic_src_addr_cache_<br/>populated?"}
    Cache -->|Yes| UseCached["Use cached source address"]
    Cache -->|No| GetSockName["getsockname(fd)"]
    GetSockName --> Store["Store in quic_src_addr_cache_"]
    Store --> UseCached
    UseCached --> Send["sendmsg(iov, flags, src_ip, dst_addr)"]
```

---

## 4. IoUringSocketHandleImpl — io_uring Path

On modern Linux kernels (5.1+), `IoUringSocketHandleImpl` submits I/O operations as SQEs (Submission Queue Entries) to avoid the overhead of per-syscall context switches.

### SQE/CQE Cycle

```mermaid
sequenceDiagram
    participant CI as ConnectionImpl
    participant IURI as IoUringSocketHandleImpl
    participant IUS as IoUringSocket
    participant Ring as io_uring Ring
    participant OS as Kernel

    CI->>IURI: read(buffer, max_length)
    IURI->>IUS: prepareRead(SQE)
    IUS->>Ring: io_uring_prep_readv(SQE)
    Ring->>OS: (deferred, batched submission)

    Note over OS: Kernel processes I/O
    OS-->>Ring: CQE result (bytes_read)
    Ring->>IUS: onCqeComplete(CQE)
    IUS-->>IURI: IoResult{ok, bytes_read}
    IURI-->>CI: IoResult
```

### Socket Types for io_uring

`IoUringSocketHandleImpl` is parameterized by socket type, which determines which io_uring operations are available:

| `SocketType` | Operations | Use Case |
|-------------|-----------|---------|
| `Accept` | `io_uring_prep_accept` | Listen socket (server) |
| `Server` | `io_uring_prep_read`, `io_uring_prep_write` | Accepted connection |
| `Client` | `io_uring_prep_connect`, `io_uring_prep_read`, `io_uring_prep_write` | Upstream connection |

---

## 5. Socket Hierarchy

```mermaid
classDiagram
    class Socket {
        <<interface>>
        +ioHandle(): IoHandle
        +connectionInfoProvider(): ConnectionInfoProvider
        +addOption(option)
        +addOptions(options)
        +setSocketOption(level, name, val): IoCallResult
        +getSocketOption(level, name): IoCallResult
        +bind(address): IoCallResult
        +listen(backlog): IoCallResult
        +close()
        +socketType(): SocketType
        +addressType(): Address::Type
    }

    class SocketImpl {
        -io_handle_: IoHandlePtr
        -connection_info_provider_: ConnectionInfoSetterImpl
        -options_: SocketOptionsList
    }

    class ConnectionSocketImpl {
        +setRequestedServerName(sni)
        +requestedServerName(): string_view
        +setTransportProtocol(protocol)
        +detectedTransportProtocol(): string_view
        +setRequestedApplicationProtocols(alpn)
        +requestedApplicationProtocols(): vector~string~
        +ja3Hash(): string_view
        +roundTripTime(): optional~Duration~
    }

    class ClientSocketImpl
    class AcceptedSocketImpl {
        +AcceptedSocketImpl(fd, addr, global_accepted_socket_stats)
    }

    class ListenSocketImpl {
        +setupSocket(options, bind_to_port)
        +bindToPort(): bool
    }

    class TcpListenSocket
    class UdpListenSocket
    class UdsListenSocket

    Socket <|-- SocketImpl
    SocketImpl <|-- ConnectionSocketImpl
    SocketImpl <|-- ListenSocketImpl
    ConnectionSocketImpl <|-- ClientSocketImpl
    ConnectionSocketImpl <|-- AcceptedSocketImpl
    ListenSocketImpl <|-- TcpListenSocket
    ListenSocketImpl <|-- UdpListenSocket
    ListenSocketImpl <|-- UdsListenSocket
```

---

## 6. ConnectionSocketImpl — Rich Metadata

`ConnectionSocketImpl` (and its `ConnectionInfoSetterImpl`) stores rich per-connection metadata populated during the TLS handshake and listener filter processing:

```mermaid
mindmap
  root(ConnectionSocketImpl metadata)
    Network
      localAddress
      remoteAddress
      directRemoteAddress
    TLS
      requestedServerName (SNI from ClientHello)
      detectedTransportProtocol (raw_buffer/tls/quic)
      requestedApplicationProtocols (ALPN list)
      negotiatedApplicationProtocol (selected ALPN)
      ja3Hash (TLS fingerprint)
    Performance
      roundTripTime (TCP RTT)
      congestionWindow (bytes)
    Certificates
      sslConnection (SSL* handle)
      peerCertificateDigests
      dnsSanLocalCertificate
      dnsSanPeerCertificate
```

### Metadata Population Timeline

```mermaid
sequenceDiagram
    participant OS as Kernel
    participant TL as TcpListenerImpl
    participant TI as TLS Inspector Filter
    participant ASI as AcceptedSocketImpl
    participant TLS as TLS Transport Socket
    participant CM as ConnectionManagerImpl

    OS-->>TL: accept() returns new_fd
    TL->>ASI: create AcceptedSocketImpl(fd, remote_addr)
    TL->>TI: onAccept(socket)
    TI->>ASI: peekData() to read ClientHello
    TI->>ASI: setRequestedServerName("api.example.com")
    TI->>ASI: setTransportProtocol("tls")
    TL->>CM: newConnection(socket)
    CM->>TLS: connect (TLS handshake)
    TLS->>ASI: setNegotiatedApplicationProtocol("h2")
    TLS->>ASI: setJa3Hash("abc123...")
    Note over ASI: All metadata available for filter chain
```

---

## 7. SocketInterface — Platform Factory

`SocketInterfaceSingleton` is an injected singleton that centralizes `IoHandle` and `Socket` creation:

```mermaid
flowchart TD
    subgraph Factory["SocketInterfaceImpl (default)"]
        socket_tcp["socket(AF_INET, SOCK_STREAM)"]
        socket_udp["socket(AF_INET, SOCK_DGRAM)"]
        socket_unix["socket(AF_UNIX, SOCK_STREAM)"]
    end

    SIS["SocketInterfaceSingleton::get()"] --> Factory
    Factory --> B{io_uring enabled?}
    B -->|Yes| IURI["IoUringSocketHandleImpl"]
    B -->|No| ISHI["IoSocketHandleImpl"]

    ISHI --> TcpConn["TCP ConnectionImpl"]
    ISHI --> UdpListen["UdpListenerImpl"]
    IURI --> TcpConn2["TCP ConnectionImpl (io_uring)"]
```

### Injection for Testing

```mermaid
flowchart LR
    Test["Unit Test"] --> MockSI["MockSocketInterface<br/>(injectable)"]
    Prod["Production"] --> DefaultSI["DefaultSocketInterfaceExtension<br/>(SocketInterfaceImpl)"]
    MockSI --> MockIOH["MockIoHandle<br/>(controllable)"]
    DefaultSI --> RealIOH["IoSocketHandleImpl"]
```

---

## 8. Socket Options System

### `SocketOptionImpl`

Encapsulates a single `setsockopt()` call to be applied at a specific lifecycle stage:

```mermaid
sequenceDiagram
    participant LS as ListenSocket
    participant SO as SocketOptionImpl
    participant OS as Kernel

    SO->>SO: SocketOptionImpl(SOL_SOCKET, SO_REUSEPORT, 1, PreBind)
    LS->>SO: setOption(socket, PreBind)
    SO->>OS: setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, 1, sizeof(int))
    OS-->>SO: 0 (success) or errno
```

### Socket Option Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Created : SocketImpl constructed
    Created --> PreBind : addOption applied at PreBind
    PreBind --> Bound : bind(address)
    Bound --> PreListen : listen(backlog) for servers
    PreListen --> Listening : options applied at PreListen
    Created --> PreConnect : for client connections
    PreConnect --> Connected : connect(remote_addr) succeeds
```

### `AddrFamilyAwareSocketOptionImpl`

Routes socket options to the appropriate IPv4 or IPv6 implementation:

```mermaid
flowchart TD
    AFAS["AddrFamilyAwareSocketOptionImpl::setOption(socket, state)"] --> B{socket.addressType()?}
    B -->|AF_INET| IPv4Opt["ipv4_option_.setOption(socket, state)<br/>e.g. IP_FREEBIND"]
    B -->|AF_INET6| IPv6Opt["ipv6_option_.setOption(socket, state)<br/>e.g. IPV6_FREEBIND"]
    B -->|Unknown| Try["Try IPv4, then IPv6"]
```

### `SocketOptionFactory` — Standard Options

```mermaid
mindmap
  root(SocketOptionFactory)
    buildTcpKeepaliveOptions
      SO_KEEPALIVE
      TCP_KEEPIDLE seconds
      TCP_KEEPINTVL seconds
      TCP_KEEPCNT probes
    buildIpFreebindOptions
      IP_FREEBIND IPv4
      IPV6_FREEBIND IPv6
    buildIpTransparentOptions
      IP_TRANSPARENT IPv4
      IPV6_TRANSPARENT IPv6
    buildSocketMarkOptions
      SO_MARK value
    buildUdpGroOptions
      UDP_GRO
    buildReusePortOptions
      SO_REUSEPORT
    buildTcpFastOpenOptions
      TCP_FASTOPEN
    buildNoSignalOptions
      SO_NOSIGPIPE macOS
```

---

## 9. Transport Sockets

Transport sockets sit between `ConnectionImpl` and the raw `IoHandle`, providing encryption/decryption or protocol framing:

```mermaid
classDiagram
    class TransportSocket {
        <<interface>>
        +doRead(buffer): IoResult
        +doWrite(buffer, end_stream): IoResult
        +onConnected()
        +closeSocket(event)
        +protocol(): string_view
        +canFlushClose(): bool
    }

    class TransportSocketCallbacks {
        <<interface>>
        +ioHandle(): IoHandle
        +connection(): Connection
        +raiseEvent(event)
        +shouldDrainReadBuffer(): bool
        +setTransportSocketIsReadable()
    }

    class RawBufferSocket {
        +doRead(): passthrough readv
        +doWrite(): passthrough writev
        +protocol(): ""
    }

    class TlsSocket {
        +doRead(): SSL_read
        +doWrite(): SSL_write
        +protocol(): "tls"
    }

    class QuicSocket {
        +doRead(): QUIC stream read
        +doWrite(): QUIC stream write
        +protocol(): "quic"
    }

    TransportSocket <|-- RawBufferSocket
    TransportSocket <|-- TlsSocket
    TransportSocket <|-- QuicSocket
    ConnectionImpl ..|> TransportSocketCallbacks
    ConnectionImpl --> TransportSocket
```

### Transport Socket Data Path

```mermaid
sequenceDiagram
    participant IOH as IoHandle
    participant CI as ConnectionImpl
    participant TS as TransportSocket
    participant FM as FilterManagerImpl

    Note over CI: Inbound data
    IOH->>CI: onFileEvent(READ)
    CI->>TS: doRead(read_buffer_)
    TS->>IOH: readv(iov) [RawBufferSocket passthrough]
    IOH-->>TS: raw bytes
    TS-->>CI: IoResult{ok, n_bytes} [decrypt if TLS]
    CI->>FM: onRead()

    Note over CI: Outbound data
    FM->>CI: rawWrite(write_buffer_)
    CI->>TS: doWrite(write_buffer_, end_stream)
    TS->>IOH: writev(iov) [encrypt if TLS]
    IOH-->>TS: IoResult
```

---

## 10. IO Error Handling

`IoSocketError` maps OS `errno` values to typed `IoErrorCode` enum values:

```mermaid
classDiagram
    class IoError {
        <<interface>>
        +getErrorCode(): IoErrorCode
        +getSystemError(): int
        +getErrorDetails(): string_view
    }

    class IoSocketError {
        -error_code_: IoErrorCode
        -errno_: int
        +getAgain(): IoSocketError (singleton)
        +getIoSocketEbadfError(): IoSocketError (singleton)
    }

    class IoErrorCode {
        <<enum>>
        Again: EAGAIN/EWOULDBLOCK
        NoSupport: ENOTSUP
        AddressInUse: EADDRINUSE
        InvalidArgument: EINVAL
        FileTooLarge: EFBIG
        AccessDenied: EACCES
        BadFd: EBADF
        Already: EALREADY
        PermissionDenied: EPERM
        MessageTooBig: EMSGSIZE
        Interrupt: EINTR
        UnknownError: other
    }

    IoError <|-- IoSocketError
    IoSocketError --> IoErrorCode
```

### Error Singletons

`EAGAIN` and `EBADF` are returned as singletons to avoid allocation on the hot path:

```mermaid
flowchart LR
    ReadV["readv() returns -1<br/>errno=EAGAIN"] --> Check{errno?}
    Check -->|EAGAIN| Singleton["IoSocketError::getAgain()<br/>(no allocation)"]
    Check -->|EBADF| Singleton2["IoSocketError::getIoSocketEbadfError()<br/>(no allocation)"]
    Check -->|other| New["new IoSocketError(errno)<br/>(heap alloc)"]
```

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_architecture_and_connections.md) | Architecture, Connections, Happy Eyeballs, Filter Manager |
| [Part 2](OVERVIEW_PART2_filters_and_listeners.md) | Network Filters, TCP/UDP Listeners, Listener Filters |
| **Part 3 (this file)** | Sockets, IoHandles, Socket Options, io_uring |
| [Part 4](OVERVIEW_PART4_addressing_dns_and_utilities.md) | Addressing, CIDR, DNS, Matching, Transport Socket Options |
