# Envoy Network Layer — Overview Part 4: Addressing, DNS, Matching & Utilities

**Directory:** `source/common/network/`  
**Part:** 4 of 4 — Address System, CIDR Matching, DNS, Transport Socket Options, Matching Framework, Utilities

---

## Table of Contents

1. [Address System Overview](#1-address-system-overview)
2. [IP Address Hierarchy](#2-ip-address-hierarchy)
3. [CIDR Matching — CidrRange and IpList](#3-cidr-matching--cidrrange-and-iplist)
4. [LC-Trie — Fast CIDR Lookup](#4-lc-trie--fast-cidr-lookup)
5. [DNS Resolution](#5-dns-resolution)
6. [Transport Socket Options and Filter State](#6-transport-socket-options-and-filter-state)
7. [Network Matching Framework](#7-network-matching-framework)
8. [Connection Hash Policy](#8-connection-hash-policy)
9. [Network Utility Functions](#9-network-utility-functions)
10. [Full Component Interaction Map](#10-full-component-interaction-map)

---

## 1. Address System Overview

Envoy unifies all address types under a single `Address::Instance` interface: IPv4, IPv6, Unix domain sockets (pipes), and internal virtual addresses.

```mermaid
flowchart TD
    subgraph AddressTypes["Address Types"]
        IPv4["Ipv4Instance<br/>1.2.3.4:80<br/>(sockaddr_in)"]
        IPv6["Ipv6Instance<br/>[::1]:443<br/>(sockaddr_in6)"]
        Pipe["PipeInstance<br/>/tmp/envoy.sock<br/>(sockaddr_un)"]
        EI["EnvoyInternalInstance<br/>endpoint_id=my-svc<br/>(virtual)"]
    end

    Instance["Address::Instance<br/>(interface)"] --> IPv4
    Instance --> IPv6
    Instance --> Pipe
    Instance --> EI

    subgraph Uses["Where addresses are used"]
        BC["bind() / connect()"]
        MS["Match source/dest in RBAC"]
        XFF["x-forwarded-for header"]
        LOG["Access logging"]
        DNS["DNS resolution output"]
    end

    Instance --> BC & MS & XFF & LOG & DNS
```

---

## 2. IP Address Hierarchy

### Class Relationships

```mermaid
classDiagram
    class Instance {
        <<interface>>
        +asString(): string
        +ip(): Ip*
        +pipe(): Pipe*
        +envoyInternalAddress(): EnvoyInternalAddress*
        +sockAddr(): sockaddr*
        +type(): Type
        +hash(): size_t
    }

    class InstanceBase {
        #friendly_name_: string
        #sock_addr_storage_: sockaddr_storage
    }

    class Ipv4Instance {
        +ip(): Ipv4Ip*
        +port(): uint32_t
        +addressAsString(): string
    }

    class Ipv6Instance {
        +ip(): Ipv6Ip*
        +port(): uint32_t
        +v6only(): bool
        +isUnicastAddress(): bool
    }

    class PipeInstance {
        +pipe(): Pipe*
        +abstractNamespace(): bool
        +permissions(): mode_t
    }

    class EnvoyInternalInstance {
        +envoyInternalAddress(): EnvoyInternalAddress*
        +endpointId(): string_view
        +addressId(): string_view
    }

    Instance <|-- InstanceBase
    InstanceBase <|-- Ipv4Instance
    InstanceBase <|-- Ipv6Instance
    InstanceBase <|-- PipeInstance
    InstanceBase <|-- EnvoyInternalInstance
```

### Address Creation

```mermaid
flowchart TD
    Input["Input: string or sockaddr"] --> F{Format?}
    F -->|"1.2.3.4:80"| A["new Ipv4Instance(str)"]
    F -->|"[::1]:443"| B["new Ipv6Instance(str)"]
    F -->|sockaddr_in| C["new Ipv4Instance(sockaddr_in)"]
    F -->|sockaddr_in6| D["new Ipv6Instance(sockaddr_in6)"]
    F -->|"/tmp/sock"| E["new PipeInstance(path)"]
    F -->|"@abstract"| G["new PipeInstance(abstract=true)"]
    F -->|proto Address| H["resolveProtoAddress(proto)"]
    H --> A & B & E & I["new EnvoyInternalInstance(endpoint)"]
```

### Thread Safety

All `Address::Instance` objects are:
- Immutable after construction
- Stored as `InstanceConstSharedPtr` = `shared_ptr<const Instance>`
- Safe to share across all worker threads

---

## 3. CIDR Matching — CidrRange and IpList

### `CidrRange` — Single Prefix Match

```mermaid
classDiagram
    class CidrRange {
        +isInRange(address: Instance): bool
        +asString(): string
        +create(range_string): CidrRange
        -address_: InstanceConstSharedPtr
        -length_: uint32_t
    }
```

Matching logic:

```
address = 10.0.1.50
range   = 10.0.0.0/8

mask = 0xFF000000  (8-bit prefix)
address & mask = 10.0.0.0
range.ip & mask = 10.0.0.0
→ match!
```

### `IpList` — Multiple CIDR Ranges

```mermaid
classDiagram
    class IpList {
        +contains(address): bool
        +empty(): bool
        -ipv4_cidrs_: vector~CidrRange~
        -ipv6_cidrs_: vector~CidrRange~
    }

    IpList *-- CidrRange
```

```mermaid
flowchart TD
    Addr["10.0.1.50"] --> IL["IpList::contains()"]
    IL --> IPv4List["Scan IPv4 CIDRs:<br/>[10.0.0.0/8, 192.168.0.0/16]"]
    IPv4List --> B{10.0.1.50 in 10.0.0.0/8?}
    B -->|Yes| True["return true"]
    B -->|No| Next["10.0.1.50 in 192.168.0.0/16?"]
    Next -->|No| False["return false"]
```

---

## 4. LC-Trie — Fast CIDR Lookup

`LcTrie<T>` implements the Nilsson-Karlsson Level-Compressed Trie for O(1) (constant small number of memory accesses) IP-to-data mapping. Used in RBAC policies and access control with large CIDR tables.

### Comparison with Linear Scan

| Method | Build Cost | Lookup Cost | Best For |
|--------|-----------|-------------|---------|
| `IpList` linear scan | O(n) | O(n) | Small lists |
| `LcTrie<T>` | O(n log n) | O(1) ~3-5 memory accesses | Large tables |

### LcTrie Structure

```mermaid
flowchart TD
    subgraph Build["Build Phase"]
        Prefixes["CIDR prefixes:<br/>10.0.0.0/8 → data A<br/>192.168.0.0/16 → data B<br/>10.0.1.0/24 → data C"] --> Compress["Level-compress trie<br/>(skip uniform subtrees)"]
        Compress --> Trie["Compact trie array"]
    end

    subgraph Lookup["Lookup Phase: lookup(10.0.1.50)"]
        IP["10.0.1.50"] --> TL["trie[root]"]
        TL --> TL2["trie[level2]"]
        TL2 --> TL3["trie[level3]"]
        TL3 --> Results["[data A, data C]<br/>(all matching prefixes)"]
    end
```

### Use in RBAC

```mermaid
sequenceDiagram
    participant RBAC as RBAC Filter
    participant LCT as LcTrie~Principal~
    participant Conn as ConnectionSocket

    RBAC->>Conn: remoteAddress()
    Conn-->>RBAC: 10.0.1.50

    RBAC->>LCT: lookup(10.0.1.50)
    LCT-->>RBAC: [Principal{allow, "internal-net"}]
    RBAC->>RBAC: evaluate principals → allow request
```

---

## 5. DNS Resolution

### DNS Factory Utilities (`dns_resolver/dns_factory_util.h`)

```mermaid
flowchart TD
    Bootstrap["Bootstrap proto<br/>(dns_resolver_config)"] --> DFU["dns_factory_util<br/>makeDnsResolverConfig()"]
    DFU --> B{Typed config present?}
    B -->|Yes| TypedConfig["Use typed_dns_resolver_config<br/>(explicit factory name)"]
    B -->|No| Legacy["Use legacy c-ares or<br/>Apple DNS based on platform"]
    TypedConfig --> Factory["DnsResolverFactory<br/>(c-ares / Apple / custom)"]
    Legacy --> Factory
    Factory --> Resolver["DnsResolver instance"]
```

### Resolver Selection

| Platform | Default Resolver | Config Field |
|----------|-----------------|-------------|
| Linux | c-ares | `typed_dns_resolver_config` |
| macOS | Apple DNS (`kDNSServiceErr_*`) | `typed_dns_resolver_config` |
| Custom | Factory-provided | `typed_dns_resolver_config.type_url` |

### DNS Resolution Flow

```mermaid
sequenceDiagram
    participant CM as ClusterManager
    participant Resolver as DnsResolver (c-ares)
    participant Network as DNS Server

    CM->>Resolver: resolve("api.example.com", DnsLookupFamily::Auto, callback)
    Resolver->>Network: DNS query (A + AAAA)
    Network-->>Resolver: A: 1.2.3.4, AAAA: 2001:db8::1
    Resolver->>CM: callback([Ipv4Instance(1.2.3.4), Ipv6Instance(2001:db8::1)])
    CM->>HE: HappyEyeballsConnectionImpl([2001:db8::1, 1.2.3.4])
```

---

## 6. Transport Socket Options and Filter State

Filter state objects allow downstream request context to influence upstream TLS connection parameters.

### Filter State to TransportSocketOptions Pipeline

```mermaid
flowchart TD
    subgraph Downstream["Downstream Request Processing"]
        JA["JWT Auth Filter"] -->|setUpstreamServerName| FS1["FilterState:<br/>UpstreamServerName=api.internal"]
        LB["LB Policy"] -->|setApplicationProtocols| FS2["FilterState:<br/>ApplicationProtocols=[h2]"]
        PP["Proxy Protocol Filter"] -->|setProxyProtocolData| FS3["FilterState:<br/>ProxyProtocolFilterState"]
    end

    subgraph Extraction["Before Upstream Connect"]
        FS1 & FS2 & FS3 --> TSOU["TransportSocketOptionsUtility<br/>::fromFilterState(filter_state)"]
        TSOU --> TSO["TransportSocketOptions<br/>{sni=api.internal, alpn=[h2], proxy_proto=...}"]
    end

    subgraph Upstream["Upstream Connection"]
        TSO --> TLS["TLS TransportSocket<br/>→ SSL_set_tlsext_host_name(api.internal)<br/>→ SSL_set_alpn_protos([h2])"]
        TSO --> PP2["ProxyProtocol TransportSocket<br/>→ prepend PROXY header"]
    end
```

### Filter State Key Reference

| Object | Key | Set By | Used By |
|--------|-----|--------|---------|
| `UpstreamServerName` | `envoy.network.upstream_server_name` | HTTP filters | TLS socket SNI |
| `ApplicationProtocols` | `envoy.network.application_protocols` | HTTP filters | TLS ALPN |
| `UpstreamSubjectAltNames` | `envoy.network.upstream_subject_alt_names` | HTTP filters | TLS SAN verification |
| `UpstreamSocketOptionsFilterState` | `envoy.network.upstream_socket_options` | Any filter | Socket options on upstream |
| `ProxyProtocolFilterState` | `envoy.network.proxy_protocol` | Proxy protocol filter | PROXY header on upstream |
| `AddressObject` | `envoy.network.filter_state_dst_address` | Original dest filter | Destination address override |
| `DownstreamNetworkNamespace` | `envoy.network.downstream_network_namespace` | Listener config | Per-listener network namespace |

### `AlpnDecoratingTransportSocketOptions`

Dynamically prepends ALPN protocols (used by `ConnectivityGrid` to indicate H3 preference):

```mermaid
flowchart LR
    Base["Existing TransportSocketOptions<br/>{alpn=[h2, http/1.1]}"] --> ADTSO["AlpnDecoratingTransportSocketOptions<br/>prepend=[h3]"]
    ADTSO --> Merged["Effective ALPN: [h3, h2, http/1.1]"]
    Merged --> TLS["TLS ALPN negotiation"]
```

---

## 7. Network Matching Framework

The `matching/` subdirectory provides network-layer data inputs for the unified xDS match tree API.

### Matching Data Inputs

```mermaid
classDiagram
    class MatchingData {
        <<interface>>
        +socket(): ConnectionSocket
        +filterState(): FilterState
        +dynamicMetadata(): DynamicMetadata
    }

    class MatchingDataImpl {
        +socket(): ConnectionSocket
        +filterState(): FilterState
    }

    class UdpMatchingDataImpl {
        +localAddress(): Address::Instance
        +remoteAddress(): Address::Instance
    }

    MatchingData <|-- MatchingDataImpl
    MatchingData <|-- UdpMatchingDataImpl
```

### Match Tree Evaluation for Network Filters

```mermaid
sequenceDiagram
    participant CM as ConnectionManagerImpl
    participant MT as MatchTree
    participant MDI as MatchingDataImpl
    participant NF as NetworkFilter

    CM->>MDI: create MatchingDataImpl(accepted_socket, filter_state)
    CM->>MT: match(matching_data)
    MT->>MDI: socket().localAddress()
    MT->>MDI: socket().requestedServerName()
    MT-->>CM: MatchResult{action=apply_filter}
    CM->>NF: apply filter to connection
```

### Available Network Data Inputs

| Input Class | Data Provided | Typical Use |
|------------|--------------|-------------|
| Source IP matcher | `remoteAddress()` | IP-based ACL |
| Destination port matcher | `localAddress().ip().port()` | Port-based routing |
| SNI matcher | `requestedServerName()` | Virtual hosting |
| Transport protocol matcher | `detectedTransportProtocol()` | TLS vs plaintext |
| Application protocol matcher | `requestedApplicationProtocols()` | ALPN-based routing |
| Filter state matcher | `filterState().get(key)` | Custom routing |

---

## 8. Connection Hash Policy

`HashPolicyImpl` computes a connection-level hash for consistent hashing load balancers (e.g., ring hash, Maglev). Used by TCP proxy for connection stickiness.

```mermaid
classDiagram
    class HashPolicyImpl {
        +generateHash(downstream_address, filter_state): optional~uint64_t~
        -hash_impls_: vector~HashMethodPtr~
    }

    class SourceIPHashMethod {
        +evaluate(addr, filter_state): optional~uint64_t~
        +hash(ip_address): uint64_t
    }

    class FilterStateHashMethod {
        -key_: string
        +evaluate(addr, filter_state): optional~uint64_t~
    }

    HashPolicyImpl *-- SourceIPHashMethod
    HashPolicyImpl *-- FilterStateHashMethod
```

### Hash Computation

```mermaid
flowchart TD
    Conn["New TCP connection<br/>from 10.0.0.1:45000"] --> HP["HashPolicyImpl::generateHash()"]
    HP --> SIP["SourceIPHashMethod::evaluate()<br/>hash(10.0.0.1) = 0xDEAD..."]
    SIP --> LB["Ring Hash LB<br/>pick upstream host by hash"]
    LB --> Same["Same host for all connections<br/>from 10.0.0.1 (sticky)"]
```

---

## 9. Network Utility Functions

`utility.h` provides free functions and the `Utility` class for common networking operations:

### Address Parsing

```mermaid
flowchart TD
    A["Utility::parseInternetAddressAndPortNoThrow(str, v6only)"] --> B{Valid format?}
    B -->|"1.2.3.4:80"| IPv4["new Ipv4Instance(1.2.3.4, 80)"]
    B -->|"[::1]:443"| IPv6["new Ipv6Instance(::1, 443, v6only)"]
    B -->|invalid| Null["nullptr"]
```

### UDP Helpers

| Function | Purpose |
|----------|---------|
| `Utility::writeToSocket(handle, buffer, local_ip, peer_addr)` | Send UDP datagram with source IP |
| `Utility::readFromSocket(handle, local_addr, processor, recv_time, ...)` | Receive UDP packets with ancillary data |
| `Utility::addressFromSockAddr(ss, len, v6only)` | Convert `sockaddr_storage` to `Address::Instance` |

### `ResolvedUdpSocketConfig`

Resolves UDP socket configuration (receive buffer size, GRO enable) from protobuf config:

```mermaid
flowchart LR
    Proto["UdpSocketConfig proto<br/>{max_rx_datagram_size: 1500<br/>prefer_gro: true}"] --> RUSC["ResolvedUdpSocketConfig"]
    RUSC --> UL["UdpListenerImpl<br/>(uses resolved values)"]
```

---

## 10. Full Component Interaction Map

```mermaid
graph TD
    subgraph Listeners
        TL["TcpListenerImpl"]
        UL["UdpListenerImpl"]
        LFM["ListenerFilter Matchers"]
        LFB["ListenerFilterBufferImpl"]
    end

    subgraph Connections
        SCI["ServerConnectionImpl"]
        CCI["ClientConnectionImpl"]
        HE["HappyEyeballsConnectionImpl"]
        FM["FilterManagerImpl"]
    end

    subgraph Sockets
        CSI["ConnectionSocketImpl"]
        LSI["ListenSocketImpl"]
        SO["SocketOption System"]
        SIF["SocketInterface"]
    end

    subgraph IO
        ISHI["IoSocketHandleImpl"]
        IURI["IoUringSocketHandleImpl"]
    end

    subgraph Addressing
        Addr["Address::Instance<br/>(Ipv4/Ipv6/Pipe/Internal)"]
        CIDR["CidrRange / IpList"]
        LCT["LcTrie"]
        Resolver["DnsResolver"]
    end

    subgraph TransportState
        TSO["TransportSocketOptions"]
        FS["FilterState objects<br/>(SNI, ALPN, ProxyProto)"]
        TSOU["TransportSocketOptionsUtility"]
    end

    subgraph Matching
        MDI["MatchingDataImpl"]
        MT["MatchTree (xDS)"]
    end

    TL --> LFM --> LFB --> SCI
    SCI --> FM --> SCI
    SCI --> CSI --> ISHI
    CCI --> HE
    HE --> CCI
    CCI --> CSI
    SIF --> ISHI & IURI
    LSI --> ISHI
    SO --> CSI & LSI
    Addr --> CIDR --> LCT
    Resolver --> Addr --> HE
    FS --> TSOU --> TSO --> CCI
    MDI --> MT
    SCI --> MDI
```

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_architecture_and_connections.md) | Architecture, Connections, Happy Eyeballs, Filter Manager |
| [Part 2](OVERVIEW_PART2_filters_and_listeners.md) | Network Filters, TCP/UDP Listeners, Listener Filters |
| [Part 3](OVERVIEW_PART3_sockets_and_io.md) | Sockets, IoHandles, Socket Options, io_uring |
| **Part 4 (this file)** | Addressing, CIDR, DNS, Matching, Transport Socket Options |

---

## Index of Individual File Documentation

| File | Individual Doc |
|------|---------------|
| `connection_impl.h/.cc` | [connection_impl.md](connection_impl.md) |
| `filter_manager_impl.h/.cc` | [filter_manager_impl.md](filter_manager_impl.md) |
| `happy_eyeballs_connection_impl.h/.cc` | [happy_eyeballs_connection_impl.md](happy_eyeballs_connection_impl.md) |
| `address_impl.h/.cc` | [address_impl.md](address_impl.md) |
| `socket_impl.h`, `io_socket_handle_impl.h`, `io_uring_socket_handle_impl.h` | [socket_and_io_handle.md](socket_and_io_handle.md) |
| `tcp_listener_impl.h`, `udp_listener_impl.h`, `listen_socket_impl.h` | [listeners.md](listeners.md) |
| `transport_socket_options_impl.h`, `application_protocol.h`, etc. | [transport_socket_options.md](transport_socket_options.md) |
