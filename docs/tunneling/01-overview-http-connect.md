# Part 1: Reverse Tunneling — Overview & HTTP CONNECT

## Introduction

Envoy supports several tunneling mechanisms that allow TCP, UDP, and HTTP traffic to be encapsulated inside HTTP connections. This enables traversal through HTTP proxies, internal mesh routing without real network I/O, and protocol bridging. This document covers the foundational HTTP CONNECT mechanism.

## Tunneling Modes Overview

```mermaid
block-beta
    columns 3
    
    block:A["Downstream CONNECT"]:1
        A1["Client sends CONNECT\nEnvoy terminates\nForwards as TCP"]
    end
    
    block:B["Upstream TCP Tunneling"]:1
        B1["TCP Proxy encapsulates\nraw TCP inside\nHTTP CONNECT/POST"]
    end
    
    block:C["Internal Listeners"]:1
        C1["In-process connections\nNo real network I/O\nMetadata passthrough"]
    end
```

```mermaid
graph TB
    subgraph "Mode 1: Downstream CONNECT"
        C1["Client"] -->|"CONNECT host:port"| E1["Envoy HCM"]
        E1 -->|"Raw TCP"| U1["Upstream TCP"]
    end
    
    subgraph "Mode 2: Upstream TCP Tunneling"
        C2["Client"] -->|"Raw TCP"| E2["Envoy TCP Proxy"]
        E2 -->|"CONNECT host:port\nover HTTP/2"| P2["HTTP Proxy"]
        P2 -->|"Raw TCP"| U2["Upstream"]
    end
    
    subgraph "Mode 3: Internal Listener"
        C3["Downstream\nConnection"] -->|"envoy_internal address"| IL["Internal Listener\n(in-process)"]
        IL -->|"Metadata\npassthrough"| E3["Second filter chain"]
    end
```

## HTTP CONNECT — How It Works

### The CONNECT Method

HTTP CONNECT creates a tunnel through an HTTP proxy. The client sends a CONNECT request with the target host:port, and the proxy establishes a TCP connection to that target. After a `200` response, raw bytes flow bidirectionally.

```mermaid
sequenceDiagram
    participant Client
    participant Proxy as Envoy (Proxy)
    participant Target as Target Server

    Client->>Proxy: CONNECT target.com:443 HTTP/1.1\nHost: target.com:443
    Proxy->>Target: TCP connect to target.com:443
    Target-->>Proxy: TCP connection established
    Proxy-->>Client: HTTP/1.1 200 Connection Established
    
    Note over Client,Target: Tunnel established — raw bytes flow
    Client->>Proxy: [TLS ClientHello / raw bytes]
    Proxy->>Target: [forward bytes]
    Target->>Proxy: [TLS ServerHello / raw bytes]
    Proxy->>Client: [forward bytes]
```

### CONNECT Detection in Envoy

```mermaid
flowchart TD
    A["Request arrives at HCM"] --> B["ActiveStream::decodeHeaders()"]
    B --> C{"HeaderUtility::isConnect(headers)?"}
    C -->|"method == CONNECT"| D["Mark as CONNECT request"]
    C -->|"No"| E["Normal HTTP request"]
    D --> F["Route matching via\nConnectRouteEntryImpl"]
    F --> G{"connectConfig() set?"}
    G -->|Yes| H["Router selects TCP upstream\nUpstreamProtocol::TCP"]
    G -->|No| I["Router selects HTTP upstream"]
```

### CONNECT Route Matching

```mermaid
classDiagram
    class ConnectRouteEntryImpl {
        +matches(headers, random) RouteConstSharedPtr
    }
    class RouteEntryImplBase {
        -connect_config_ : ConnectConfig
        +connectConfig() ConnectConfig*
    }
    
    ConnectRouteEntryImpl --|> RouteEntryImplBase
    
    note for ConnectRouteEntryImpl "Matches when method == CONNECT\nor method == CONNECT-UDP"
```

### Router CONNECT Pool Selection

```mermaid
flowchart TD
    A["Router::createConnPool()"] --> B{"route_entry_->connectConfig()\n&& isConnect(headers)?"}
    B -->|Yes| C{"CONNECT-UDP?"}
    C -->|Yes| D["UpstreamProtocol::UDP"]
    C -->|No| E["UpstreamProtocol::TCP"]
    B -->|No| F["UpstreamProtocol::HTTP"]
    
    E --> G["GenericConnPoolFactory\n→ TcpConnPool"]
    F --> H["GenericConnPoolFactory\n→ HttpConnPool"]
    D --> I["GenericConnPoolFactory\n→ UdpConnPool"]
```

## CONNECT Request Flow — Detailed

```mermaid
sequenceDiagram
    participant Client
    participant HCM as ConnectionManagerImpl
    participant AS as ActiveStream
    participant FM as FilterManager
    participant Router as Router Filter
    participant UR as UpstreamRequest
    participant TCP as TcpUpstream

    Client->>HCM: CONNECT target:443 HTTP/1.1
    HCM->>AS: newStream(encoder)
    AS->>AS: decodeHeaders(CONNECT headers)
    AS->>FM: decodeHeaders() → filter chain
    FM->>Router: decodeHeaders(CONNECT headers)
    
    Router->>Router: route_entry_->connectConfig() → TCP
    Router->>Router: createConnPool() → TcpConnPool
    Router->>UR: new UpstreamRequest(tcp_pool)
    UR->>UR: acceptHeadersFromRouter(end_stream=false)
    
    Note over UR: paused_for_connect_ = true
    Note over UR: Don't forward data yet
    
    UR->>TCP: TcpConnPool::newStream()
    TCP->>TCP: Connect to target:443
    TCP-->>UR: onPoolReady()
    
    TCP->>UR: Send synthetic 200 response
    UR->>Router: onUpstreamHeaders(200)
    Router->>FM: encodeHeaders(200)
    FM->>AS: encodeHeaders(200)
    AS->>Client: HTTP/1.1 200 Connection Established
    
    Note over UR: paused_for_connect_ = false
    Note over Client,TCP: Tunnel active — bidirectional raw bytes
    
    Client->>HCM: [raw bytes]
    HCM->>AS: decodeData(bytes)
    AS->>FM: decodeData(bytes)
    FM->>Router: decodeData(bytes)
    Router->>UR: acceptDataFromRouter(bytes)
    UR->>TCP: Forward to upstream TCP
    
    TCP->>UR: Upstream data
    UR->>Router: onUpstreamData(bytes)
    Router->>FM: encodeData(bytes)
    FM->>AS: encodeData(bytes)
    AS->>Client: [raw bytes]
```

### Pause-for-CONNECT Mechanism

The Router doesn't forward request body until the upstream responds with `200`:

```mermaid
stateDiagram-v2
    [*] --> Paused : CONNECT request received
    Paused --> Paused : Body data buffered
    Paused --> Active : Upstream sends 200
    Active --> Active : Bidirectional data flow
    Active --> [*] : Connection closed
    
    note right of Paused : paused_for_connect_ = true\nData queued, not forwarded
    note right of Active : paused_for_connect_ = false\nBuffered data flushed
```

```
File: source/common/router/upstream_request.cc (lines 350-357)

acceptHeadersFromRouter():
    if isConnect(headers):
        paused_for_connect_ = true
        // Don't send body yet

File: source/common/router/upstream_codec_filter.cc (lines 143-146)

CodecBridge::decodeHeaders(response_headers):
    if is 2xx response:
        Unpause → forward buffered body data
```

## Upgrade Filter Chain for CONNECT

CONNECT is treated as an upgrade type. The HCM creates a special filter chain:

```mermaid
flowchart TD
    A["FilterManager::createFilterChain()"] --> B{"isConnect or isUpgrade?"}
    B -->|Yes| C["createUpgradeFilterChain()"]
    B -->|No| D["createFilterChain(normal)"]
    
    C --> E["Build upgrade-specific filters"]
    E --> F["May skip non-upgrade filters"]
    F --> G["Add Router (terminal)"]
```

## Key Source Files

| File | Key Classes/Functions | Purpose |
|------|----------------------|---------|
| `source/common/http/header_utility.cc:178` | `isConnect()` | CONNECT detection |
| `source/common/router/config_impl.cc:1177` | `ConnectRouteEntryImpl` | CONNECT route matching |
| `source/common/router/router.cc:752-786` | `createConnPool()` | TCP vs HTTP pool selection |
| `source/common/router/upstream_request.cc:350` | `paused_for_connect_` | Pause body until 200 |
| `source/common/router/upstream_codec_filter.cc:143` | `decodeHeaders()` | Unpause on 2xx |
| `source/extensions/upstreams/http/tcp/` | `TcpUpstream`, `TcpConnPool` | TCP upstream for CONNECT |
| `source/common/http/filter_manager.cc:1448` | `createUpgradeFilterChain()` | CONNECT filter chain |

---

**Next:** [Part 2 — TCP-over-HTTP Tunneling](02-tcp-over-http-tunneling.md)
