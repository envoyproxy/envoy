# Part 4: `source/common/network/` — Connections, Sockets, and I/O

## Overview

The `network/` folder implements Envoy's TCP/UDP networking layer. It provides the `Connection` abstraction, socket I/O, the L4 filter manager, transport socket wrappers, and address handling. Everything in the HTTP layer sits on top of these primitives.

## Folder Structure

```mermaid
graph TD
    subgraph "source/common/network/"
        Core["Connection & I/O"]
        Filters["L4 Filters"]
        Sockets["Sockets & Addresses"]
        Listeners["Listeners"]
        Support["Support"]
        Sub["dns_resolver/ | matching/"]
    end
    
    Core --> CI["connection_impl.h/cc"]
    Core --> CIB["connection_impl_base.h/cc"]
    Core --> IOH["io_socket_handle_impl.h/cc"]
    Core --> RBS["raw_buffer_socket.h/cc"]
    
    Filters --> FMI["filter_manager_impl.h/cc"]
    Filters --> FI["filter_impl.h"]
    Filters --> FM["filter_matcher.h/cc"]
    
    Sockets --> SI["socket_impl.h/cc"]
    Sockets --> AI["address_impl.h/cc"]
    Sockets --> CSI["connection_socket_impl.h"]
    Sockets --> LSI["listen_socket_impl.h/cc"]
    Sockets --> TSO["transport_socket_options_impl.h/cc"]
    
    Listeners --> TLI["tcp_listener_impl.h/cc"]
    Listeners --> BLI["base_listener_impl.h/cc"]
    Listeners --> ULI["udp_listener_impl.h/cc"]
    Listeners --> LFB["listener_filter_buffer_impl.h/cc"]
```

## ConnectionImpl — The TCP Connection

### Class Hierarchy

```mermaid
classDiagram
    class Connection {
        <<interface>>
        +close(type)
        +write(data, end_stream)
        +readDisable(disable)
        +state() State
        +addConnectionCallbacks(cb)
    }
    class FilterManagerConnection {
        <<interface>>
        +addReadFilter(filter)
        +addWriteFilter(filter)
        +initializeReadFilters()
    }
    class ConnectionImplBase {
        -state_ : State
        -connection_stats_ : Stats
        -callbacks_ : list~ConnectionCallbacks~
        +raiseEvent(event)
        +close(type)
    }
    class ConnectionImpl {
        -transport_socket_ : TransportSocketPtr
        -filter_manager_ : FilterManagerImpl
        -read_buffer_ : WatermarkBuffer
        -write_buffer_ : WatermarkBuffer
        -file_event_ : FileEventPtr
        +onReadReady()
        +onWriteReady()
        +write(data, end_stream)
        +onRead(bytes_read)
    }
    class ServerConnectionImpl {
        +ServerConnectionImpl(dispatcher, socket, transport_socket, stream_info)
    }
    class ClientConnectionImpl {
        +connect()
        +ClientConnectionImpl(dispatcher, address, source, transport_socket, options)
    }

    Connection <|-- FilterManagerConnection
    ConnectionImplBase ..|> Connection
    ConnectionImpl --|> ConnectionImplBase
    ConnectionImpl ..|> FilterManagerConnection
    ServerConnectionImpl --|> ConnectionImpl
    ClientConnectionImpl --|> ConnectionImpl
```

### ConnectionImpl Internal Structure

```mermaid
graph TD
    subgraph "ConnectionImpl"
        FE["file_event_\n(IoHandle watcher)"]
        IO["ioHandle_\n(IoSocketHandleImpl)"]
        TS["transport_socket_\n(TLS or raw)"]
        RB["read_buffer_\n(WatermarkBuffer)"]
        WB["write_buffer_\n(WatermarkBuffer)"]
        FM["filter_manager_\n(FilterManagerImpl)"]
        SI["stream_info_"]
    end
    
    FE -->|"read event"| ReadPath
    FE -->|"write event"| WritePath
    
    subgraph ReadPath
        R1["onReadReady()"]
        R2["transport_socket_->doRead(read_buffer_)"]
        R3["onRead(bytes) → filter_manager_.onRead()"]
    end
    
    subgraph WritePath
        W1["onWriteReady()"]
        W2["transport_socket_->doWrite(write_buffer_)"]
    end
    
    IO <--> TS
    TS --> RB
    WB --> TS
    RB --> FM
```

### Read and Write Paths

```mermaid
sequenceDiagram
    participant EL as Event Loop
    participant Conn as ConnectionImpl
    participant TS as TransportSocket
    participant RB as read_buffer_
    participant FM as FilterManagerImpl
    participant WB as write_buffer_

    Note over EL,FM: READ PATH
    EL->>Conn: file_event_ (readable)
    Conn->>Conn: onReadReady()
    Conn->>TS: doRead(read_buffer_)
    TS->>RB: Decrypted bytes added
    Conn->>Conn: onRead(bytes_read)
    Conn->>FM: onRead() → onContinueReading()
    FM->>FM: Iterate read filters

    Note over EL,WB: WRITE PATH
    FM->>Conn: write(data, end_stream)
    Conn->>FM: filter_manager_.onWrite()
    FM->>FM: Iterate write filters
    Conn->>WB: Move data to write_buffer_
    Conn->>Conn: Schedule write event
    EL->>Conn: file_event_ (writable)
    Conn->>Conn: onWriteReady()
    Conn->>TS: doWrite(write_buffer_)
    TS->>TS: Encrypt and send
```

## IoSocketHandleImpl — System I/O

```mermaid
classDiagram
    class IoHandle {
        <<interface>>
        +readv(max_length, slices) IoResult
        +writev(slices) IoResult
        +sendmsg(slices, dest, cmsg) IoResult
        +recvmsg(buffer, dest, cmsg) IoResult
        +bind(address) Api::SysCallIntResult
        +listen(backlog) Api::SysCallIntResult
        +accept(addr, len) IoHandlePtr
        +connect(address) Api::SysCallIntResult
        +close() Api::SysCallIntResult
        +fd() os_fd_t
    }
    class IoSocketHandleImpl {
        -fd_ : os_fd_t
        +readv(max_length, slices) IoResult
        +writev(slices) IoResult
        +accept(addr, len) IoHandlePtr
        +initializeFileEvent(dispatcher, cb, trigger, events)
    }
    class IoSocketHandleBaseImpl {
        -fd_ : os_fd_t
        -socket_v6only_ : bool
        +fd() os_fd_t
        +isOpen() bool
    }

    IoSocketHandleImpl --|> IoSocketHandleBaseImpl
    IoSocketHandleBaseImpl ..|> IoHandle
```

`IoSocketHandleImpl` is the primary I/O primitive — it wraps POSIX system calls (`readv`, `writev`, `sendmsg`, `accept`, `bind`, `listen`, `connect`) and integrates with the event loop via `initializeFileEvent()`.

## Socket and Address Types

### Socket Hierarchy

```mermaid
classDiagram
    class Socket {
        <<interface>>
        +ioHandle() IoHandle
        +connectionInfoProvider() ConnectionInfoProvider
        +addOption(option)
    }
    class SocketImpl {
        -io_handle_ : IoHandlePtr
        -connection_info_ : ConnectionInfoSetterImpl
        -options_ : OptionsSharedPtr
    }
    class ConnectionSocketImpl {
        +setDetectedTransportProtocol(protocol)
        +setRequestedServerName(sni)
        +setRequestedApplicationProtocols(alpn)
    }
    class AcceptedSocketImpl {
        +AcceptedSocketImpl(io_handle, local, remote)
    }
    class ClientSocketImpl {
        +ClientSocketImpl(address, source)
    }
    class ListenSocketImpl {
        +ListenSocketImpl(io_handle, address)
    }

    SocketImpl ..|> Socket
    ConnectionSocketImpl --|> SocketImpl
    AcceptedSocketImpl --|> ConnectionSocketImpl
    ClientSocketImpl --|> ConnectionSocketImpl
    ListenSocketImpl --|> SocketImpl
```

### Address Types

```mermaid
classDiagram
    class Address_Instance {
        <<interface>>
        +asString() string
        +asStringView() string_view
        +ip() Ip*
        +pipe() Pipe*
        +type() Type
        +sockAddr() sockaddr*
    }
    class Ipv4Instance {
        -ip_ : sockaddr_in
        +asString() string
        +ip() Ip*
    }
    class Ipv6Instance {
        -ip_ : sockaddr_in6
        +asString() string
        +ip() Ip*
    }
    class PipeInstance {
        -address_ : sockaddr_un
        +pipe() Pipe*
    }
    class EnvoyInternalInstance {
        -address_id_ : string
    }

    Address_Instance <|-- Ipv4Instance
    Address_Instance <|-- Ipv6Instance
    Address_Instance <|-- PipeInstance
    Address_Instance <|-- EnvoyInternalInstance
```

## Transport Sockets

```mermaid
graph TD
    subgraph "Transport Socket Implementations"
        TSI["TransportSocket interface"]
        TSI --> RBS["RawBufferSocket\n(plaintext passthrough)"]
        TSI --> SSL["SslSocket\n(TLS via BoringSSL)"]
        TSI --> ALTS["AltsTransportSocket\n(Google ALTS)"]
        TSI --> STARTTLS["StartTlsSocket\n(upgrade to TLS)"]
    end
    
    subgraph "Transport Socket Options"
        TSO["TransportSocketOptionsImpl"]
        TSO --> SNI["serverNameOverride()"]
        TSO --> ALPN["applicationProtocolListOverride()"]
        TSO --> SAN["verifySubjectAltNameListOverride()"]
        TSO --> PP["proxyProtocolOptions()"]
    end
```

### RawBufferSocket

The simplest transport socket — direct passthrough:

```mermaid
graph LR
    Read["doRead(buffer)"] --> IORead["buffer.read(io_handle)"]
    Write["doWrite(buffer)"] --> IOWrite["buffer.write(io_handle)"]
    Connected["onConnected()"] --> Raise["raiseEvent(Connected)"]
```

## FilterManagerImpl — L4 Filter Engine

```mermaid
classDiagram
    class FilterManagerImpl {
        -connection_ : FilterManagerConnection&
        -upstream_filters_ : list~ActiveReadFilter~
        -downstream_filters_ : list~ActiveWriteFilter~
        +addReadFilter(filter)
        +addWriteFilter(filter)
        +addFilter(filter)
        +initializeReadFilters() bool
        +onRead()
        +onWrite()
    }
    class ActiveReadFilter {
        -filter_ : ReadFilterSharedPtr
        -initialized_ : bool
        +continueReading()
        +injectReadDataToFilterChain(buffer, end_stream)
    }
    class ActiveWriteFilter {
        -filter_ : WriteFilterSharedPtr
        +injectWriteDataToFilterChain(buffer, end_stream)
    }

    FilterManagerImpl *-- ActiveReadFilter : "upstream_filters_"
    FilterManagerImpl *-- ActiveWriteFilter : "downstream_filters_"
```

### Filter Ordering

```mermaid
graph LR
    subgraph "Read Filters (FIFO — moveIntoListBack)"
        RF1["RBAC"] --> RF2["Rate Limit"] --> RF3["HCM"]
    end
    
    subgraph "Write Filters (LIFO — moveIntoList)"
        WF1["Stats"] --> WF2["Access Log"]
    end
    
    Note["Read: first added = first called\nWrite: first added = first called (but prepended)"]
```

## Complete File Catalog

| File | Key Classes | Purpose |
|------|-------------|---------|
| `connection_impl.h/cc` | `ConnectionImpl`, `ServerConnectionImpl`, `ClientConnectionImpl` | TCP connection |
| `connection_impl_base.h/cc` | `ConnectionImplBase` | Connection base with callbacks and stats |
| `connection_socket_impl.h` | `ConnectionSocketImpl`, `AcceptedSocketImpl`, `ClientSocketImpl` | Socket wrappers |
| `filter_manager_impl.h/cc` | `FilterManagerImpl`, `ActiveReadFilter`, `ActiveWriteFilter` | L4 filter manager |
| `filter_impl.h` | `ReadFilterBaseImpl` | Base read filter (no-op) |
| `filter_matcher.h/cc` | `ListenerFilterMatcherBuilder`, matchers | Listener filter chain matching |
| `io_socket_handle_impl.h/cc` | `IoSocketHandleImpl` | POSIX socket I/O |
| `io_socket_handle_base_impl.h/cc` | `IoSocketHandleBaseImpl` | Base I/O handle |
| `socket_impl.h/cc` | `SocketImpl`, `ConnectionInfoSetterImpl` | Socket and connection info |
| `socket_interface_impl.h/cc` | `SocketInterfaceImpl` | Default socket interface |
| `socket_option_impl.h/cc` | `SocketOptionImpl` | Single socket option |
| `socket_option_factory.h/cc` | `SocketOptionFactory` | Common socket option factory |
| `address_impl.h/cc` | `Ipv4Instance`, `Ipv6Instance`, `PipeInstance`, `EnvoyInternalInstance` | Address types |
| `cidr_range.h/cc` | `CidrRange`, `IpList` | CIDR range matching |
| `lc_trie.h` | `LcTrie` | Level-compressed trie for CIDR lookup |
| `listen_socket_impl.h/cc` | `ListenSocketImpl`, `TcpListenSocket`, `UdpListenSocket` | Listen socket types |
| `tcp_listener_impl.h/cc` | `TcpListenerImpl` | TCP listener (accept loop) |
| `base_listener_impl.h/cc` | `BaseListenerImpl` | Base listener |
| `udp_listener_impl.h/cc` | `UdpListenerImpl` | UDP listener |
| `raw_buffer_socket.h/cc` | `RawBufferSocket`, `RawBufferSocketFactory` | Plain transport socket |
| `transport_socket_options_impl.h/cc` | `TransportSocketOptionsImpl`, `CommonUpstreamTransportSocketFactory` | Transport socket options |
| `utility.h/cc` | `Utility`, `UdpPacketProcessor` | Network utilities |
| `listener_filter_buffer_impl.h/cc` | `ListenerFilterBufferImpl` | Peek buffer for listener filters |
| `connection_balancer_impl.h/cc` | `ExactConnectionBalancerImpl` | Connection balancer |
| `happy_eyeballs_connection_impl.h/cc` | `HappyEyeballsConnectionImpl` | Happy Eyeballs v2 |
| `multi_connection_base_impl.h/cc` | `MultiConnectionBaseImpl` | Multi-address connections |
| `default_client_connection_factory.h/cc` | `DefaultClientConnectionFactory` | Default connection factory |
| `resolver_impl.h/cc` | `IpResolver` | Address resolution |
| `ip_address.h/cc` | `IPAddressObject` | IP filter state |
| `proxy_protocol_filter_state.h/cc` | `ProxyProtocolFilterState` | PROXY protocol state |
| `application_protocol.h/cc` | `ApplicationProtocols` | ALPN filter state |
| `upstream_server_name.h/cc` | `UpstreamServerName` | Upstream SNI state |
| `dns_resolver/dns_factory_util.h/cc` | DNS factory helpers | DNS resolver creation |
| `matching/data_impl.h` | `MatchingDataImpl` | Network matching data |

---

**Previous:** [Part 3 — HTTP Codecs, Headers, and Pools](03-http-codecs-headers-pools.md)  
**Next:** [Part 5 — Network Listeners, Filters, and Addresses](05-network-listeners-filters.md)
