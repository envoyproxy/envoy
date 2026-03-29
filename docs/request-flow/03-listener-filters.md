# Part 3: Listener Filters — Creation and Chain Execution

## Overview

Listener filters run immediately after a socket is accepted, **before** a connection object is created. They inspect raw bytes on the socket to determine protocol information (TLS vs plaintext, HTTP version, original destination) that is needed to select the correct filter chain.

## Why Listener Filters Exist

```mermaid
graph LR
    subgraph "Without Listener Filters"
        S1["Accepted Socket"] --> Q1["Which filter chain?<br/>Don't know protocol yet!"]
        Q1 --> X1["❌ Can't match"]
    end
    
    subgraph "With Listener Filters"
        S2["Accepted Socket"] --> LF1["tls_inspector<br/>→ detect TLS, ALPN, SNI"]
        LF1 --> LF2["http_inspector<br/>→ detect HTTP version"]
        LF2 --> Q2["Filter Chain Match<br/>using detected metadata"]
        Q2 --> OK["✅ Correct chain selected"]
    end
```

The key insight: filter chain matching needs information (SNI, ALPN, transport protocol) that can only be determined by peeking at the raw bytes on the socket.

## Common Listener Filters

| Filter | Purpose | What It Sets |
|--------|---------|-------------|
| `tls_inspector` | Peeks at ClientHello to detect TLS | SNI, ALPN, transport_protocol="tls" |
| `http_inspector` | Detects HTTP version from first bytes | application_protocol="h2c" or "http/1.1" |
| `original_dst` | Gets original destination from SO_ORIGINAL_DST | Restored destination address |
| `proxy_protocol` | Parses PROXY protocol header | Source/dest addresses from proxy header |

## Listener Filter Interface

```mermaid
classDiagram
    class ListenerFilter {
        <<interface>>
        +onAccept(ListenerFilterCallbacks) FilterStatus
        +onData(ListenerFilterBuffer) FilterStatus
        +onClose(ListenerFilterCallbacks) void
        +maxReadBytes() size_t
    }
    class ListenerFilterCallbacks {
        <<interface>>
        +socket() ConnectionSocket
        +dispatcher() Event::Dispatcher
        +continueFilterChain(bool success)
        +dynamicMetadata() StructMap
    }
    class ListenerFilterManager {
        <<interface>>
        +addAcceptFilter(listener_filter_matcher, filter)
        +addAcceptFilter(filter)
    }
    class ActiveTcpSocket {
        +continueFilterChain(success)
        +newConnection()
        -accept_filters_
        -iter_
    }

    ListenerFilter ..> ListenerFilterCallbacks : "uses"
    ActiveTcpSocket ..|> ListenerFilterManager : "implements"
    ActiveTcpSocket ..|> ListenerFilterCallbacks : "implements"
    ActiveTcpSocket --> ListenerFilter : "holds list of"
```

**Interface location:** `envoy/network/filter.h` (lines 334-365)

Key methods:
- `onAccept(callbacks)` — called when the socket is accepted, can inspect/modify socket metadata
- `onData(buffer)` — called when data is available on the socket (for filters that need to read bytes)
- `maxReadBytes()` — how many bytes this filter needs to peek at
- `onClose()` — cleanup when the socket is closed during filter processing

## Listener Filter Factory Pattern

### Factory Registration

Each listener filter type has a factory that implements `NamedListenerFilterConfigFactory`:

```mermaid
flowchart LR
    subgraph "Configuration Time"
        Config["Listener Config<br/>(YAML/xDS)"] --> Factory["NamedListenerFilterConfigFactory"]
        Factory --> CB["FilterFactoryCb<br/>(lambda)"]
    end
    subgraph "Runtime (per socket)"
        CB --> |"invoked"| Filter["ListenerFilter instance"]
        Filter --> Socket["Inspect socket"]
    end
```

### Factory Creation in ListenerImpl

```
File: source/common/listener_manager/listener_impl.cc (lines 434-459)

createListenerFilterFactories():
  1. For each listener_filter in config:
     a. Look up factory by name in registry
     b. Call factory.createListenerFilterFactoryFromProto(config, context)
     c. Store returned FilterFactoryCb in listener_filter_factories_
  2. Add built-in filters:
     a. buildOriginalDstListenerFilter() — if use_original_dst is set
     b. buildProxyProtocolListenerFilter() — if proxy_protocol is configured
```

### Built-in Listener Filters

Some listener filters are added implicitly based on listener configuration:

```mermaid
flowchart TD
    A["ListenerImpl::create()"] --> B["createListenerFilterFactories()"]
    B --> C["Parse config.listener_filters()"]
    C --> D["Register user-configured filters"]
    D --> E{use_original_dst?}
    E -->|Yes| F["Add original_dst filter"]
    E -->|No| G{proxy_protocol?}
    F --> G
    G -->|Yes| H["Add proxy_protocol filter"]
    G -->|No| I["Done"]
    H --> I
```

## Filter Chain Execution

### Creating the Chain

When a socket is accepted, `ActiveStreamListenerBase::onSocketAccepted()` creates the listener filter chain:

```mermaid
sequenceDiagram
    participant ASL as ActiveStreamListenerBase
    participant ATS as ActiveTcpSocket
    participant LI as ListenerImpl
    participant FCU as FilterChainUtility

    ASL->>ATS: onSocketAccepted(active_socket)
    ASL->>LI: filterChainFactory().createListenerFilterChain(active_socket)
    LI->>FCU: buildFilterChain(manager, listener_filter_factories_)
    
    loop For each factory
        FCU->>ATS: factory(manager) → addAcceptFilter(filter)
        Note over ATS: Filter stored in accept_filters_
    end
    
    ASL->>ATS: startFilterChain()
    ATS->>ATS: continueFilterChain(true)
```

```
File: source/common/listener_manager/listener_impl.cc (lines 706-714)

ListenerImpl::createListenerFilterChain(manager):
  FilterChainUtility::buildFilterChain(manager, listener_filter_factories_)
  → For each factory: factory(manager) calls addAcceptFilter()
```

### Iterating Filters

`ActiveTcpSocket::continueFilterChain()` drives the iteration:

```mermaid
flowchart TD
    Start["continueFilterChain(success)"] --> CheckSuccess{success?}
    CheckSuccess -->|No| Close["Close socket"]
    CheckSuccess -->|Yes| Loop["Iterate: iter_ through accept_filters_"]
    
    Loop --> CheckMatcher{Filter matcher<br/>matches socket?}
    CheckMatcher -->|No| Skip["Skip filter, next"]
    CheckMatcher -->|Yes| CallAccept["filter.onAccept(callbacks)"]
    
    CallAccept --> Status{Return status?}
    Status -->|Continue| NextFilter["Move to next filter"]
    Status -->|StopIteration| NeedData{maxReadBytes > 0?}
    
    NeedData -->|Yes| CreateBuffer["Create ListenerFilterBuffer"]
    CreateBuffer --> WaitData["Wait for data on socket"]
    WaitData --> DataArrived["filter.onData(buffer)"]
    DataArrived --> DataStatus{Return status?}
    DataStatus -->|Continue| NextFilter
    DataStatus -->|StopIteration| WaitData
    
    NeedData -->|No| WaitAsync["Wait for async callback<br/>(e.g., DNS lookup)"]
    WaitAsync --> AsyncDone["continueFilterChain(true)"]
    AsyncDone --> NextFilter
    
    NextFilter --> Loop
    Skip --> Loop
    
    Loop -->|All done| NewConn["newConnection()"]
```

```
File: source/common/listener_manager/active_tcp_socket.cc (lines 109-173)

continueFilterChain(success):
  1. If !success → close socket, return
  2. For each filter in accept_filters_ (starting from iter_):
     a. If filter matcher doesn't match → skip
     b. Call filter->onAccept(*this)
     c. If StopIteration:
        - If filter needs data (maxReadBytes > 0):
          Create ListenerFilterBuffer, wait for bytes
        - Else: wait for async continueFilterChain() callback
        - Return (will resume later)
     d. If Continue → advance to next filter
  3. All filters done → newConnection()
```

### Example: TLS Inspector Filter Flow

```mermaid
sequenceDiagram
    participant ATS as ActiveTcpSocket
    participant TLS as TlsInspectorFilter
    participant Sock as Socket

    ATS->>TLS: onAccept(callbacks)
    Note over TLS: maxReadBytes() = 64KB
    TLS-->>ATS: StopIteration (need data)
    
    ATS->>ATS: Create ListenerFilterBuffer
    ATS->>Sock: Enable read events
    
    Sock->>ATS: Data available (ClientHello)
    ATS->>TLS: onData(buffer)
    Note over TLS: Parse TLS ClientHello
    Note over TLS: Extract SNI, ALPN
    TLS->>Sock: setRequestedServerName(sni)
    TLS->>Sock: setDetectedTransportProtocol("tls")
    TLS->>Sock: setRequestedApplicationProtocols(alpn_list)
    TLS-->>ATS: Continue
    
    ATS->>ATS: Next filter or newConnection()
```

## Timeout Handling

Listener filters have a configurable timeout. If filters don't complete within this time:

```mermaid
flowchart TD
    A["Socket accepted"] --> B["Start listener_filters_timeout timer"]
    B --> C["Run filters"]
    C --> D{Filters complete in time?}
    D -->|Yes| E["newConnection()"]
    D -->|No / Timeout| F{continue_on_listener_filters_timeout?}
    F -->|true| E
    F -->|false| G["Close socket"]
```

The timeout prevents listener filters from holding a socket indefinitely (e.g., waiting for bytes that never arrive from a non-TLS client on a TLS-expected listener).

## Filter Order Matters

Listener filter ordering is critical because each filter may set metadata that subsequent filters or the filter chain matcher depends on:

```mermaid
graph LR
    F1["1. original_dst<br/>→ sets real dest IP"] --> F2["2. proxy_protocol<br/>→ sets source IP/port"]
    F2 --> F3["3. tls_inspector<br/>→ sets SNI, ALPN"]
    F3 --> F4["4. http_inspector<br/>→ sets HTTP version"]
    F4 --> FCM["Filter Chain Matching<br/>uses all metadata"]
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `envoy/network/filter.h` | 334-365 | `ListenerFilter` interface |
| `source/common/listener_manager/listener_impl.cc` | 434-459 | Creates listener filter factories |
| `source/common/listener_manager/listener_impl.cc` | 706-714 | `createListenerFilterChain()` |
| `source/common/listener_manager/active_tcp_socket.cc` | 109-173 | `continueFilterChain()` iteration |
| `source/common/listener_manager/active_tcp_socket.h` | 28-92 | `ActiveTcpSocket` class |
| `source/server/configuration_impl.cc` | 46-57 | `buildFilterChain()` for listener filters |

---

**Previous:** [Part 2 — Listener Layer: Socket Accept](02-listener-socket-accept.md)  
**Next:** [Part 4 — Filter Chain Matching and Selection](04-filter-chain-matching.md)
