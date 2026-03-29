# Transport Socket Options & Upstream Filter State

**Files:**
- `source/common/network/transport_socket_options_impl.h/.cc`
- `source/common/network/application_protocol.h/.cc`
- `source/common/network/upstream_server_name.h/.cc`
- `source/common/network/upstream_subject_alt_names.h/.cc`
- `source/common/network/upstream_socket_options_filter_state.h/.cc`
- `source/common/network/proxy_protocol_filter_state.h/.cc`
- `source/common/network/raw_buffer_socket.h/.cc`
**Namespace:** `Envoy::Network`

## Overview

`TransportSocketOptions` carries upstream connection tuning parameters (SNI, ALPN, SANs, socket options, proxy protocol) from the downstream request context into the TLS/QUIC layer when establishing upstream connections. These parameters travel via `FilterState` objects keyed by well-known string constants.

## Architecture

```mermaid
flowchart TD
    subgraph DownstreamRequest["Downstream Request Context"]
        FS["StreamInfo::FilterState"]
        FS --> USN["UpstreamServerName<br/>(SNI override)"]
        FS --> AP["ApplicationProtocols<br/>(ALPN override)"]
        FS --> USAN["UpstreamSubjectAltNames<br/>(SAN verification override)"]
        FS --> USOF["UpstreamSocketOptionsFilterState<br/>(extra socket options)"]
        FS --> PPF["ProxyProtocolFilterState<br/>(PROXY protocol data)"]
    end

    subgraph Extraction["TransportSocketOptionsUtility"]
        TSOU["fromFilterState(filter_state)"]
    end

    subgraph UpstreamConnect["Upstream Connection Setup"]
        TSO["TransportSocketOptions struct"]
        TLS["TLS TransportSocket<br/>(uses SNI, ALPN, SANs)"]
        Pool["ConnPool::createCodecClient()"]
    end

    FS --> TSOU
    TSOU --> TSO
    TSO --> Pool
    Pool --> TLS
```

## `TransportSocketOptions` struct

```mermaid
classDiagram
    class TransportSocketOptions {
        +serverNameOverride(): absl::string_view
        +verifySubjectAltNameListOverride(): vector~string~
        +applicationProtocolListOverride(): vector~string~
        +applicationProtocolFallback(): vector~string~
        +proxyProtocolOptions(): optional~ProxyProtocolData~
        +hashKey(key_vector)
    }

    class TransportSocketOptionsImpl {
        -server_name_override_: string
        -verify_san_list_override_: vector~string~
        -alpn_override_: vector~string~
        -alpn_fallback_: vector~string~
        -proxy_protocol_options_: optional~ProxyProtocolData~
        -upstream_http_uri_san_match_override_: optional~string~
    }

    TransportSocketOptions <|-- TransportSocketOptionsImpl
```

## Filter State Objects

Each filter state object has a static string key and is stored in `StreamInfo::FilterState`:

### `UpstreamServerName`

Overrides the TLS SNI hostname for upstream connections:

```mermaid
sequenceDiagram
    participant Filter as HTTP Filter
    participant FS as FilterState
    participant TSOU as TransportSocketOptionsUtility
    participant TLS as TLS Socket

    Filter->>FS: set(UpstreamServerName::key(), "api.internal.example.com")
    Note over Filter,FS: Key = "envoy.network.upstream_server_name"

    TSOU->>FS: get(UpstreamServerName::key())
    FS-->>TSOU: UpstreamServerName object
    TSOU->>TSO: TransportSocketOptions{server_name="api.internal.example.com"}
    TLS->>TLS: SSL_set_tlsext_host_name("api.internal.example.com")
```

### `ApplicationProtocols`

Overrides ALPN protocols offered to the upstream TLS server:

```mermaid
sequenceDiagram
    participant Filter as HTTP Filter
    participant FS as FilterState
    participant TLS as TLS Socket

    Filter->>FS: set(ApplicationProtocols::key(), ["h2", "http/1.1"])
    Note over Filter,FS: Key = "envoy.network.application_protocols"

    TLS->>FS: get(ApplicationProtocols::key())
    FS-->>TLS: ApplicationProtocols{["h2", "http/1.1"]}
    TLS->>TLS: SSL_set_alpn_protos(["h2", "http/1.1"])
```

### `ProxyProtocolFilterState`

Carries PROXY protocol v1/v2 header data to prepend on the upstream connection:

```mermaid
sequenceDiagram
    participant Downstream as Downstream Proxy
    participant PPFS as ProxyProtocolFilterState
    participant PPSO as ProxyProtocol TransportSocket
    participant Upstream

    Downstream->>PPFS: set PROXY protocol data<br/>(src_addr, dst_addr, protocol)
    PPFS->>PPSO: proxyProtocolOptions()
    PPSO->>Upstream: PROXY TCP4 10.0.0.1 10.0.0.2 5000 80\r<br/>
    PPSO->>Upstream: actual TLS/TCP data
```

### `UpstreamSocketOptionsFilterState`

Accumulates additional socket options to apply when creating the upstream socket:

```mermaid
flowchart TD
    Filter1["Filter A: set IP_MARK=42"] --> USOFS["UpstreamSocketOptionsFilterState"]
    Filter2["Filter B: set SO_REUSEPORT"] --> USOFS
    USOFS --> Pool["ConnPool::createUpstreamSocket()"]
    Pool --> SO["Apply all accumulated socket options"]
    SO --> US["Upstream socket ready"]
```

## `AlpnDecoratingTransportSocketOptions`

Wraps existing `TransportSocketOptions` and prepends additional ALPN protocols for dynamic protocol negotiation:

```mermaid
flowchart LR
    Base["TransportSocketOptions<br/>{alpn: [h2]}"] --> ADTSO["AlpnDecoratingTransportSocketOptions<br/>(prepend [h3])"]
    ADTSO --> Merged["Effective ALPN: [h3, h2]"]
    Merged --> TLS["TLS ALPN negotiation"]
```

## `TransportSocketOptionsUtility::fromFilterState()`

The key function that reads all filter state objects and assembles a `TransportSocketOptions`:

```mermaid
flowchart TD
    FS["StreamInfo::FilterState"] --> F1["get UpstreamServerName"]
    FS --> F2["get ApplicationProtocols"]
    FS --> F3["get UpstreamSubjectAltNames"]
    FS --> F4["get ProxyProtocolFilterState"]
    FS --> F5["get UpstreamSocketOptionsFilterState"]
    F1 & F2 & F3 & F4 & F5 --> TSO["TransportSocketOptionsImpl<br/>(assembled from all filter state)"]
    TSO --> Pool["Used by ConnPool when creating upstream connection"]
```

## `RawBufferSocket` — Plaintext Transport

`RawBufferSocket` is the no-op transport socket used for unencrypted TCP connections:

```mermaid
classDiagram
    class TransportSocket {
        <<interface>>
        +doRead(buffer): IoResult
        +doWrite(buffer, end_stream): IoResult
        +onConnected()
        +protocol(): string_view
    }

    class RawBufferSocket {
        +doRead(buffer): pass-through to IoHandle
        +doWrite(buffer, end_stream): pass-through to IoHandle
        +protocol(): "" (empty = raw TCP)
    }

    class RawBufferSocketFactory {
        +createTransportSocket(options): TransportSocketPtr
        +implementsSecureTransport(): false
    }

    TransportSocket <|-- RawBufferSocket
    RawBufferSocketFactory --> RawBufferSocket
```

## `CommonUpstreamTransportSocketFactory`

Base class for upstream transport socket factories. Provides `hashKey()` for connection pool identity (different TLS configs → different pools):

```mermaid
flowchart TD
    TSO["TransportSocketOptions<br/>(SNI, ALPN, SANs)"] --> HK["CommonUpstreamTransportSocketFactory::hashKey(TSO)"]
    HK --> Key["hash key bytes<br/>(used to look up correct pool)"]
    Key --> Pool["Correct connection pool<br/>(H2 with TLS to api.internal)"]
```

## Filter State Keys Reference

| Filter State Object | Key String | Purpose |
|--------------------|-----------|---------|
| `UpstreamServerName` | `envoy.network.upstream_server_name` | Override TLS SNI |
| `ApplicationProtocols` | `envoy.network.application_protocols` | Override ALPN |
| `UpstreamSubjectAltNames` | `envoy.network.upstream_subject_alt_names` | Override SAN verification |
| `UpstreamSocketOptionsFilterState` | `envoy.network.upstream_socket_options` | Extra socket options |
| `ProxyProtocolFilterState` | `envoy.network.proxy_protocol` | PROXY protocol data |
| `AddressObject` | `envoy.network.filter_state_dst_address` | Override destination address |
| `DownstreamNetworkNamespace` | `envoy.network.downstream_network_namespace` | Per-listener network namespace |
