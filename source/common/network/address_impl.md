# Address Implementation

**Files:** `source/common/network/address_impl.h` / `.cc`  
**Namespace:** `Envoy::Network::Address`

## Overview

The address system provides a unified `Instance` interface over IPv4, IPv6, Unix domain sockets (pipes), and Envoy-internal virtual addresses. All address objects are **immutable** and **const-shared**, making them safe to share across threads without locking.

## Class Hierarchy

```mermaid
classDiagram
    class Instance {
        <<interface>>
        +asString(): string
        +asStringView(): string_view
        +ip(): Ip*
        +pipe(): Pipe*
        +envoyInternalAddress(): EnvoyInternalAddress*
        +sockAddr(): sockaddr*
        +sockAddrLen(): socklen_t
        +type(): Type
        +hash(): size_t
    }

    class InstanceBase {
        #friendly_name_: string
        #sock_addr_storage_: sockaddr_storage
    }

    class Ipv4Instance {
        +ip(): Ip*
        +port(): uint32_t
        +addressAsString(): string
    }

    class Ipv6Instance {
        +ip(): Ip*
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

## Address Types

```mermaid
flowchart TD
    A[Address::Type] --> IP["Ip<br/>(IPv4 or IPv6)"]
    A --> Pipe["Pipe<br/>(Unix domain socket)"]
    A --> EnvoyInternal["EnvoyInternal<br/>(virtual/internal address)"]

    IP --> IPv4["Ipv4Instance<br/>sockaddr_in<br/>0.0.0.0 – 255.255.255.255"]
    IP --> IPv6["Ipv6Instance<br/>sockaddr_in6<br/>:: – ffff:ffff:..."]
    Pipe --> UDS["PipeInstance<br/>sockaddr_un<br/>/path/to/socket or @abstract"]
    EnvoyInternal --> EI["EnvoyInternalInstance<br/>virtual endpoint ID"]
```

## `Ip` Interface

Both `Ipv4Instance` and `Ipv6Instance` expose an `Ip*` from `instance.ip()`:

```mermaid
classDiagram
    class Ip {
        <<interface>>
        +addressAsString(): string
        +isAnyAddress(): bool
        +isUnicastAddress(): bool
        +ipv4(): Ipv4*
        +ipv6(): Ipv6*
        +port(): uint32_t
        +version(): IpVersion
    }

    class Ipv4 {
        <<interface>>
        +address(): in_addr
    }

    class Ipv6 {
        <<interface>>
        +address(): in6_addr
        +v6only(): bool
        +isUnicastAddress(): bool
    }

    Ip --> Ipv4
    Ip --> Ipv6
```

## Creating Addresses — `InstanceFactory`

```mermaid
flowchart TD
    F["InstanceFactory"] --> A{Address type?}
    A -->|IPv4 string "1.2.3.4:80"| B["new Ipv4Instance(ip, port)"]
    A -->|IPv6 string "[::1]:443"| C["new Ipv6Instance(ip, port)"]
    A -->|sockaddr_in| D["new Ipv4Instance(sockaddr_in)"]
    A -->|sockaddr_in6| E["new Ipv6Instance(sockaddr_in6)"]
    A -->|path string "/tmp/sock"| G["new PipeInstance(path, permissions)"]
    A -->|abstract "@sock"| H["new PipeInstance(abstract_name)"]
    A -->|internal endpoint| I["new EnvoyInternalInstance(endpoint, address)"]
```

## CIDR Range Matching — `CidrRange` and `IpList`

```mermaid
classDiagram
    class CidrRange {
        +isInRange(address): bool
        +asString(): string
        -address_: InstanceConstSharedPtr
        -length_: uint32_t
    }

    class IpList {
        +contains(address): bool
        -ipv4_cidrs_: vector~CidrRange~
        -ipv6_cidrs_: vector~CidrRange~
    }

    IpList *-- CidrRange
```

### CIDR Matching Flow

```mermaid
flowchart TD
    Addr["Address to match:<br/>10.0.1.50"] --> IL["IpList::contains()"]
    IL --> IPv4["Check IPv4 CIDR list<br/>[10.0.0.0/8, 192.168.0.0/16]"]
    IPv4 --> B{10.0.1.50 in 10.0.0.0/8?}
    B -->|Yes| Match["return true"]
    B -->|No| Next["Check next CIDR"]
    Next --> NoMatch["return false"]
```

## LC-Trie — `LcTrie<T>` (Fast CIDR Lookup)

`LcTrie` implements the Nilsson-Karlsson Level-Compressed Trie algorithm for O(1) (few memory accesses) CIDR-to-data mapping, used in RBAC and access control:

```mermaid
flowchart TD
    Build["Build phase:<br/>Insert all CIDR prefixes<br/>with associated data T"] --> Trie["Compressed trie structure<br/>(level-compressed for few memory hops)"]

    Lookup["Lookup phase:<br/>lookup(ip_address)"] --> Trie
    Trie --> Result["vector of T<br/>(all matching data for this IP)"]
```

### Performance Comparison

| Structure | Lookup Complexity | Use Case |
|-----------|------------------|---------|
| `IpList` | O(n) linear scan | Small lists (< 10 ranges), simple config |
| `LcTrie<T>` | O(1) few memory hops | Large CIDR tables (RBAC policies, access logs) |

## `Resolver` and `ResolverImpl`

```mermaid
sequenceDiagram
    participant Proto as ProtoSocketAddress
    participant RI as IpResolver
    participant AF as Address Factory

    Proto->>RI: resolve(proto_address)
    RI->>RI: parse proto address type (IPv4/IPv6/PIPE)
    RI->>AF: new Ipv4Instance(host, port)
    AF-->>RI: InstanceConstSharedPtr
    RI-->>Proto: InstanceConstSharedPtr
```

Free functions `resolveProtoAddress()` and `resolveProtoSocketAddress()` convert proto config addresses (`core::v3::Address`) to `Address::InstanceConstSharedPtr`.

## `EnvoyInternalInstance` — Internal Addressing

Used for in-process communication between Envoy components (e.g., internal listeners, direct response listeners):

```mermaid
flowchart LR
    L["Internal Listener<br/>(endpoint_id=my-service)"] -->|accepts| EI["EnvoyInternalInstance<br/>endpoint_id=my-service<br/>address_id=unique-id"]
    EI -->|routed by| CM["ConnectionManagerImpl<br/>(ApiListener mode)"]
```

## Thread Safety

All `Address::Instance` objects are:
- **Immutable** after construction
- **Const-shared** (`InstanceConstSharedPtr = shared_ptr<const Instance>`)
- **Safe to share** across worker threads without locking
