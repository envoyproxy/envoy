# Part 6: Transport Sockets and TLS Handshake

## Overview

Transport sockets are an abstraction layer between the raw TCP connection and the network filter chain. They handle encryption/decryption (TLS), protocol wrapping (STARTTLS, ALTS), and transparent passthrough (raw buffer). The transport socket is selected based on the matched filter chain and wraps all I/O operations.

## Where Transport Sockets Fit

```mermaid
graph TB
    subgraph "Connection Stack"
        direction TB
        Socket["Raw TCP Socket<br/>(IoHandle)"]
        TS["Transport Socket<br/>(TLS/raw_buffer/ALTS)"]
        Conn["ConnectionImpl<br/>(read_buffer_, write_buffer_)"]
        FM["FilterManagerImpl<br/>(Network Filters)"]
    end
    
    Socket <-->|"encrypted bytes"| TS
    TS <-->|"decrypted bytes"| Conn
    Conn <-->|"application data"| FM
    
    style TS fill:#fff9c4
```

## Transport Socket Interface

```mermaid
classDiagram
    class TransportSocket {
        <<interface>>
        +doRead(Buffer) IoResult
        +doWrite(Buffer, bool end_stream) IoResult
        +onConnected()
        +closeSocket(CloseType)
        +protocol() string
        +failureReason() string
        +ssl() Ssl::ConnectionInfo
        +setTransportSocketCallbacks(TransportSocketCallbacks)
    }
    class TransportSocketCallbacks {
        <<interface>>
        +ioHandle() IoHandle
        +connection() Connection
        +shouldDrainReadBuffer() bool
        +setTransportSocketIsReadable()
        +raiseEvent(ConnectionEvent)
        +flushWriteBuffer()
    }
    class TransportSocketFactory {
        <<interface>>
    }
    class DownstreamTransportSocketFactory {
        <<interface>>
        +createDownstreamTransportSocket() TransportSocketPtr
    }
    class UpstreamTransportSocketFactory {
        <<interface>>
        +createTransportSocket(options, host) TransportSocketPtr
        +implementsSecureTransport() bool
    }

    TransportSocket ..> TransportSocketCallbacks : "uses"
    DownstreamTransportSocketFactory --> TransportSocket : "creates"
    UpstreamTransportSocketFactory --> TransportSocket : "creates"
```

**Interface location:** `envoy/network/transport_socket.h`

- `TransportSocket` (lines 116-203) — the per-connection socket wrapper
- `TransportSocketCallbacks` (lines 40-100) — connection provides these to the transport socket
- `DownstreamTransportSocketFactory` (lines 250-288) — creates sockets for incoming connections
- `UpstreamTransportSocketFactory` (lines 293-339) — creates sockets for outgoing connections

## Transport Socket Types

```mermaid
graph TD
    TSI["TransportSocket interface"]
    
    TSI --> Raw["RawBufferSocket<br/>(passthrough, no encryption)"]
    TSI --> TLS["SslSocket<br/>(TLS via BoringSSL)"]
    TSI --> ALTS["AltsTransportSocket<br/>(Google ALTS)"]
    TSI --> STARTTLS["StartTlsSocket<br/>(upgrade from raw to TLS)"]
    TSI --> ProxyProto["UpstreamProxyProtocol<br/>(prepends PROXY header)"]
    TSI --> Internal["InternalTransportSocket<br/>(in-process connections)"]
    
    style TLS fill:#c8e6c9
    style Raw fill:#e3f2fd
```

### Raw Buffer Socket

The simplest transport socket — direct passthrough with no transformation:

```
doRead(buffer)  → buffer.read(io_handle, ...)    // straight read
doWrite(buffer) → buffer.write(io_handle, ...)   // straight write
onConnected()   → raiseEvent(Connected)          // immediate
```

### TLS (SSL) Socket

The most common transport socket — handles the TLS handshake and encryption:

```mermaid
stateDiagram-v2
    [*] --> Handshaking : onConnected()
    Handshaking --> Handshaking : doRead()/doWrite() drive handshake
    Handshaking --> Connected : handshake complete
    Handshaking --> Failed : handshake error
    Connected --> Connected : doRead()/doWrite() encrypt/decrypt
    Connected --> Closing : closeSocket()
    Failed --> [*]
    Closing --> [*]
```

## TLS Handshake Flow (Downstream)

```mermaid
sequenceDiagram
    participant Client
    participant IoHandle as IoHandle (raw socket)
    participant SSL as SslSocket (BoringSSL)
    participant Conn as ConnectionImpl
    participant FM as FilterManagerImpl

    Note over Conn: Connection created, transport socket attached
    Conn->>SSL: setTransportSocketCallbacks(this)
    Conn->>SSL: onConnected()
    Note over SSL: Initialize SSL context

    Client->>IoHandle: ClientHello
    IoHandle->>Conn: Socket readable event
    Conn->>SSL: doRead(buffer)
    Note over SSL: SSL_do_handshake()
    Note over SSL: Needs to write ServerHello
    SSL->>IoHandle: ServerHello (via SSL_write internally)
    SSL-->>Conn: IoResult{0 bytes, Action::KeepOpen}

    Client->>IoHandle: Client key exchange, Finished
    IoHandle->>Conn: Socket readable event  
    Conn->>SSL: doRead(buffer)
    Note over SSL: SSL_do_handshake() completes
    SSL->>Conn: raiseEvent(Connected)
    Note over Conn: Now initializeReadFilters()
    Conn->>FM: onNewConnection() on all read filters

    Client->>IoHandle: Encrypted HTTP request
    IoHandle->>Conn: Socket readable event
    Conn->>SSL: doRead(buffer)
    Note over SSL: SSL_read() → decrypted bytes
    SSL-->>Conn: IoResult{N bytes decrypted}
    Conn->>FM: onRead() → filters see plaintext
```

## Transport Socket in Connection Lifecycle

### ConnectionImpl and Transport Socket Integration

`ConnectionImpl` (`source/common/network/connection_impl.h:49-257`) owns the transport socket and implements `TransportSocketCallbacks`:

```mermaid
graph TD
    subgraph "ConnectionImpl"
        RB["read_buffer_<br/>(WatermarkBuffer)"]
        WB["write_buffer_<br/>(WatermarkBuffer)"]
        TS_["transport_socket_<br/>(TransportSocketPtr)"]
        FMI["filter_manager_<br/>(FilterManagerImpl)"]
        IO["file_event_<br/>(IoHandle watcher)"]
    end
    
    IO -->|"read event"| OnRead["onReadReady()"]
    OnRead -->|"doRead()"| TS_
    TS_ -->|"decrypted data"| RB
    RB -->|"onRead()"| FMI
    
    FMI -->|"write(data)"| WB
    WB -->|"write event"| OnWrite["onWriteReady()"]
    OnWrite -->|"doWrite()"| TS_
    TS_ -->|"encrypted data"| IO
```

### Read Path Through Transport Socket

```
File: source/common/network/connection_impl.cc (lines 618-660)

onReadReady():
    1. result = transport_socket_->doRead(*read_buffer_)
    2. if (result.bytes_read > 0 || result.end_stream):
         onRead(result.bytes_read)
           → filter_manager_.onRead()
    3. if (result.action == Action::Close):
         closeSocket(FlushWrite)
```

### Write Path Through Transport Socket

```
File: source/common/network/connection_impl.cc (lines 504-551, then onWriteReady)

write(data, end_stream):
    1. filter_manager_.onWrite()  → write filters process data
    2. Move data to write_buffer_
    3. Schedule write event

onWriteReady():
    1. result = transport_socket_->doWrite(*write_buffer_, end_stream)
    2. Handle partial writes, high watermarks
    3. If write_buffer_ empty and end_stream → close
```

## Transport Socket Factory Selection

### Downstream (Server Side)

The transport socket factory comes from the matched `FilterChainImpl`:

```mermaid
flowchart TD
    FC["FilterChainImpl"] --> TSF["DownstreamTransportSocketFactory"]
    TSF --> |"filter chain config has<br/>transport_socket: tls"| TLS["DownstreamTlsSocketFactory<br/>(creates SslSocket)"]
    TSF --> |"no transport_socket<br/>configured"| Raw["RawBufferSocketFactory<br/>(creates RawBufferSocket)"]
    
    TLS --> |"createDownstreamTransportSocket()"| SSLSock["SslSocket<br/>(with TLS context, certs)"]
    Raw --> |"createDownstreamTransportSocket()"| RawSock["RawBufferSocket"]
```

### Upstream (Client Side)

For upstream connections, the transport socket comes from the cluster config:

```mermaid
flowchart TD
    Cluster["Cluster Config"] --> TSF["UpstreamTransportSocketFactory"]
    TSF --> TLS["UpstreamTlsSocketFactory"]
    TSF --> Raw["RawBufferSocketFactory"]
    
    TLS --> |"createTransportSocket(options, host)"| SSLSock["SslSocket<br/>(with upstream TLS context)"]
    
    subgraph "TransportSocketOptions"
        SNI["Server Name (SNI)"]
        ALPN["ALPN Protocols"]
        SAN["Subject Alt Names to verify"]
    end
    
    SNI --> TLS
    ALPN --> TLS
    SAN --> TLS
```

```
File: source/common/upstream/upstream_impl.cc (lines 477-503)

HostImplBase::createConnection():
    1. Resolve transport socket factory (via transport socket matcher)
    2. socket_factory.createTransportSocket(transport_socket_options, host)
    3. dispatcher.createClientConnection(address, source, transport_socket, ...)
```

## Transport Socket Options

`TransportSocketOptions` (`envoy/network/transport_socket.h:207-274`) carries metadata for transport socket creation:

```mermaid
graph TD
    TSO["TransportSocketOptions"]
    TSO --> SNI["serverNameOverride()<br/>TLS SNI to use"]
    TSO --> SAN["verifySubjectAltNameListOverride()<br/>SAN verification list"]
    TSO --> ALPN["applicationProtocolListOverride()<br/>ALPN protocol list"]
    TSO --> AF["applicationProtocolFallback()<br/>fallback ALPN"]
    TSO --> PP["proxyProtocolOptions()<br/>PROXY protocol header"]
    TSO --> HA["hashAlpn(alpn_list)<br/>hash for consistent hashing"]
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `envoy/network/transport_socket.h` | 116-203 | `TransportSocket` interface |
| `envoy/network/transport_socket.h` | 40-100 | `TransportSocketCallbacks` |
| `envoy/network/transport_socket.h` | 250-339 | Transport socket factory interfaces |
| `envoy/network/transport_socket.h` | 207-274 | `TransportSocketOptions` |
| `source/common/network/connection_impl.h` | 49-257 | `ConnectionImpl` owns transport socket |
| `source/common/network/connection_impl.cc` | 68-99 | Connection setup with transport socket |
| `source/common/network/connection_impl.cc` | 618-660 | `onReadReady()` reads via transport socket |
| `source/common/network/raw_buffer_socket.h` | — | Raw passthrough transport socket |
| `source/common/tls/client_ssl_socket.h` | — | Upstream TLS transport socket |
| `source/common/tls/server_ssl_socket.h` | — | Downstream TLS transport socket |
| `source/common/upstream/upstream_impl.cc` | 477-503 | Upstream connection creation |

---

**Previous:** [Part 5 — Network (L4) Filters](05-network-filters.md)  
**Next:** [Part 7 — HTTP Connection Manager](07-http-connection-manager.md)
