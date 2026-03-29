# Part 2: TCP-over-HTTP Tunneling

## Overview

TCP-over-HTTP tunneling allows Envoy to encapsulate raw TCP connections inside HTTP streams. This is primarily used when Envoy's TCP proxy needs to reach an upstream through an HTTP proxy, or when services need to tunnel arbitrary protocols over HTTP/2 multiplexed connections for efficiency.

## Architecture

```mermaid
graph LR
    subgraph "Downstream"
        Client["TCP Client"]
    end
    
    subgraph "Envoy (TCP Proxy)"
        TCPProxy["TCP Proxy Filter"]
        HttpUpstream["HttpUpstream"]
    end
    
    subgraph "HTTP Proxy / Upstream"
        HTTPProxy["HTTP Proxy\nor Direct Upstream"]
    end
    
    subgraph "Target"
        Backend["Backend Service"]
    end
    
    Client -->|"Raw TCP"| TCPProxy
    TCPProxy -->|"HTTP CONNECT\nor POST"| HttpUpstream
    HttpUpstream -->|"HTTP/2 stream"| HTTPProxy
    HTTPProxy -->|"Raw TCP"| Backend
```

## TCP Proxy Tunneling Classes

```mermaid
classDiagram
    class TcpProxy_Filter {
        -tunneling_config_ : TunnelingConfigHelper
        +onNewConnection() FilterStatus
        +onData(buffer, end_stream) FilterStatus
        -onInitFailure(reason)
        -createUpstreamConnection()
    }
    class TunnelingConfigHelperImpl {
        -hostname_ : string or formatter
        -use_post_ : bool
        -headers_to_add_ : HeaderParser
        -propagate_response_headers_ : bool
        +host(info) string
        +usePost() bool
        +isValidResponse(headers) bool
        +headersToAdd() HeaderParser
        +propagateResponseTrailers() bool
    }
    class HttpConnPool {
        -host_ : HostConstSharedPtr
        -cluster_ : ThreadLocalCluster
        +newStream(GenericConnectionPoolCallbacks)
        +onPoolReady(encoder, host)
    }
    class HttpUpstream {
        -encoder_ : RequestEncoder*
        -downstream_info_ : StreamInfo
        +setRequestEncoder(encoder, is_ssl)
        +encodeData(data, end_stream) void
        +onResetStream(reason)
        +setTunnelResponseHeaders(headers)
    }
    class CombinedUpstream {
        -upstream_filter_manager_
        +setRequestEncoder(encoder)
        +encodeData(data, end_stream)
    }

    TcpProxy_Filter --> TunnelingConfigHelperImpl
    TcpProxy_Filter --> HttpConnPool : "creates"
    HttpConnPool --> HttpUpstream : "creates"
    CombinedUpstream --|> HttpUpstream
```

## TCP Proxy → HTTP CONNECT Tunnel Flow

```mermaid
sequenceDiagram
    participant Client as TCP Client
    participant TCPProxy as TCP Proxy Filter
    participant Pool as HttpConnPool
    participant HttpUp as HttpUpstream
    participant Upstream as Upstream HTTP Proxy

    Client->>TCPProxy: TCP connection
    TCPProxy->>TCPProxy: Check tunneling_config_
    TCPProxy->>Pool: newStream(callbacks)
    Pool->>Pool: cluster.httpConnPool() → get HTTP/2 pool
    Pool->>Upstream: Create HTTP/2 connection (if needed)
    Pool-->>TCPProxy: onPoolReady(encoder, host)
    
    TCPProxy->>HttpUp: setRequestEncoder(encoder, is_ssl)
    
    alt use_post = false (default)
        HttpUp->>Upstream: CONNECT target:port HTTP/2
        Note over HttpUp: Headers: :method=CONNECT\n:authority=target:port
    else use_post = true
        HttpUp->>Upstream: POST / HTTP/2
        Note over HttpUp: Headers: :method=POST\n:path=/<br>custom headers from config
    end
    
    HttpUp->>HttpUp: encoder_->enableTcpTunneling()
    
    Upstream-->>HttpUp: 200 OK (tunnel established)
    HttpUp->>HttpUp: Validate response (isValidResponse)
    HttpUp->>TCPProxy: Tunnel ready
    
    Note over Client,Upstream: Bidirectional data flow
    Client->>TCPProxy: Raw TCP data
    TCPProxy->>HttpUp: encodeData(data)
    HttpUp->>Upstream: HTTP DATA frame
    
    Upstream->>HttpUp: HTTP DATA frame
    HttpUp->>TCPProxy: onData(data)
    TCPProxy->>Client: Raw TCP data
```

## Tunneling Configuration

```mermaid
graph TD
    TC["TunnelingConfigHelperImpl"]
    
    TC --> Host["hostname()\n→ target host for CONNECT\n(supports %FILTER_STATE% substitution)"]
    TC --> Post["usePost()\n→ use POST instead of CONNECT"]
    TC --> Headers["headersToAdd()\n→ extra headers on tunnel request"]
    TC --> Valid["isValidResponse()\n→ validate upstream 2xx response"]
    TC --> PropHeaders["propagateResponseHeaders()\n→ pass tunnel headers to downstream"]
    TC --> PropTrailers["propagateResponseTrailers()\n→ pass tunnel trailers to downstream"]
    TC --> BufferLimit["bufferLimit()\n→ per-tunnel buffer limit"]
```

## HTTP/1.1 Proxy CONNECT (Transport Socket Level)

For connecting through an HTTP/1.1 forward proxy, Envoy uses a transport socket wrapper:

```mermaid
sequenceDiagram
    participant Envoy as Envoy Client
    participant TS as UpstreamHttp11ConnectSocket
    participant Proxy as HTTP/1.1 Proxy
    participant Target as Target Host

    Envoy->>TS: onConnected()
    TS->>Proxy: CONNECT target:443 HTTP/1.1\r\nHost: target:443\r\n\r\n
    Proxy->>Target: TCP connect
    Target-->>Proxy: Connected
    Proxy-->>TS: HTTP/1.1 200 Connection Established\r\n\r\n
    
    Note over TS: Strip CONNECT response
    Note over TS: Pass to inner transport socket (TLS)
    
    TS->>Proxy: [TLS ClientHello via inner socket]
    Proxy->>Target: [forward TLS bytes]
    Target-->>Proxy: [TLS ServerHello]
    Proxy-->>TS: [forward TLS bytes]
    TS-->>Envoy: [Decrypted application data]
```

```mermaid
classDiagram
    class UpstreamHttp11ConnectSocket {
        -inner_socket_ : TransportSocketPtr
        -header_buffer_ : Buffer
        -need_to_strip_connect_response_ : bool
        +doRead(buffer) IoResult
        +doWrite(buffer, end_stream) IoResult
        +onConnected()
    }

    note for UpstreamHttp11ConnectSocket "Wraps an inner transport socket\n(usually TLS) and prepends\nHTTP/1.1 CONNECT handshake"
```

## UDP-over-HTTP Tunneling

Envoy also supports tunneling UDP traffic over HTTP:

```mermaid
graph LR
    subgraph "UDP Proxy"
        UDPListener["UDP Listener"]
        UDPProxy["UDP Proxy Filter"]
        TunnelPool["TunnelingConnectionPoolImpl"]
    end
    
    subgraph "HTTP Tunnel"
        HTTPConn["HTTP/2 Connection"]
        ConnectUDP["CONNECT-UDP stream"]
    end
    
    UDPListener -->|"UDP datagrams"| UDPProxy
    UDPProxy -->|"Encapsulate"| TunnelPool
    TunnelPool -->|"HTTP CONNECT or POST"| HTTPConn
    HTTPConn --> ConnectUDP
```

## Multiplexing TCP Tunnels over HTTP/2

A key advantage of TCP-over-HTTP tunneling is multiplexing:

```mermaid
graph TD
    subgraph "Single HTTP/2 Connection"
        Stream1["Stream 1: CONNECT client-a:3306\n→ MySQL connection"]
        Stream3["Stream 3: CONNECT client-b:5432\n→ PostgreSQL connection"]
        Stream5["Stream 5: CONNECT client-c:6379\n→ Redis connection"]
    end
    
    subgraph "TCP Proxy Instances"
        TP1["TCP Proxy 1"]
        TP2["TCP Proxy 2"]
        TP3["TCP Proxy 3"]
    end
    
    TP1 --> Stream1
    TP2 --> Stream3
    TP3 --> Stream5
    
    Note["Multiple TCP connections\nmultiplexed over a single\nHTTP/2 connection"]
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `source/common/tcp_proxy/tcp_proxy.h` | `TunnelingConfigHelperImpl` | Tunneling config |
| `source/common/tcp_proxy/tcp_proxy.cc:724-760` | TCP proxy tunneling | Tunnel setup |
| `source/common/tcp_proxy/upstream.cc` | `HttpUpstream`, `HttpConnPool`, `CombinedUpstream` | HTTP tunnel upstream |
| `source/extensions/upstreams/http/tcp/upstream_request.cc` | `TcpUpstream`, `TcpConnPool` | TCP upstream for CONNECT |
| `source/extensions/transport_sockets/http_11_proxy/connect.cc` | `UpstreamHttp11ConnectSocket` | HTTP/1.1 proxy CONNECT |
| `source/extensions/filters/udp/udp_proxy/udp_proxy_filter.cc` | `TunnelingConnectionPoolImpl` | UDP tunneling |

---

**Previous:** [Part 1 — Overview & HTTP CONNECT](01-overview-http-connect.md)  
**Next:** [Part 3 — Internal Listeners & Reverse Connections](03-internal-listeners.md)
