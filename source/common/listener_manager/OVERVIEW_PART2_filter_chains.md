# Listener Manager — Overview Part 2: Filter Chains & Matching

**Directory:** `source/common/listener_manager/`  
**Part:** 2 of 4 — FilterChainManagerImpl, Matching Trie, FilterChainImpl, Factory Contexts

---

## Table of Contents

1. [Filter Chain System Overview](#1-filter-chain-system-overview)
2. [FilterChainManagerImpl — Matching Engine](#2-filtermanagerimpl--matching-engine)
3. [Matching Criteria Priority](#3-matching-criteria-priority)
4. [Nested Trie Structure](#4-nested-trie-structure)
5. [SNI Matching — Exact and Wildcard](#5-sni-matching--exact-and-wildcard)
6. [FilterChainImpl — Per Chain](#6-filterchainimpl--per-chain)
7. [Factory Contexts](#7-factory-contexts)
8. [Building Filter Chains from Config](#8-building-filter-chains-from-config)
9. [Default Filter Chain](#9-default-filter-chain)
10. [Connection to Filter Chain Selection at Runtime](#10-connection-to-filter-chain-selection-at-runtime)

---

## 1. Filter Chain System Overview

A listener can have multiple filter chains, each with different matching criteria and different network filters. When a connection is accepted, `FilterChainManagerImpl` selects the best-matching chain based on connection metadata (populated by listener filters).

**Why Multiple Filter Chains:**
- **Multi-protocol support**: Same listener can handle HTTPS, HTTP/2, and raw TCP
- **Virtual hosting**: Route connections by SNI (Server Name Indication) without terminating TLS
- **Client segmentation**: Apply different policies based on source IP ranges
- **Flexible routing**: Match on any combination of: port, IP, SNI, protocol, ALPN, source

**How Matching Works:**
- Listener filters extract connection metadata before filter chain matching
- Examples: TLS Inspector extracts SNI and ALPN, Original Dst finds true destination
- Metadata is used to walk a nested trie structure for fast O(log n) matching
- Most specific match wins (exact before wildcard, longest prefix before shorter)

**Example Scenario in Diagram:**
- **tls-api chain**: Exact SNI match `api.example.com` + TLS → HTTP Connection Manager
- **tls-web chain**: Wildcard SNI `*.example.com` + TLS → HTTP Connection Manager
- **plaintext chain**: No TLS (raw_buffer) → TCP Proxy
- **default chain**: Catch-all for anything not matched above → TCP Proxy

When `api.example.com` connection arrives with TLS, it matches **tls-api** (most specific).

```mermaid
flowchart TD
    subgraph ListenerConfig["Listener Config"]
        FC1["filter_chain: tls-api<br/>match: {sni: api.example.com, transport: tls}<br/>filters: [http_connection_manager]"]
        FC2["filter_chain: tls-web<br/>match: {sni: *.example.com, transport: tls}<br/>filters: [http_connection_manager]"]
        FC3["filter_chain: plaintext<br/>match: {transport: raw_buffer}<br/>filters: [tcp_proxy]"]
        DFC["default_filter_chain:<br/>filters: [tcp_proxy]"]
    end

    subgraph Runtime["Runtime Matching"]
        Conn["Incoming connection:<br/>SNI=api.example.com<br/>transport=tls"] --> FCM["FilterChainManagerImpl<br/>::findFilterChain()"]
        FCM --> FC1
    end
```

---

## 2. FilterChainManagerImpl — Matching Engine

```mermaid
classDiagram
    class FilterChainManagerImpl {
        +findFilterChain(socket, info): FilterChain*
        +addFilterChains(configs, default, builder, creator)
        -fc_contexts_: map~string, FilterChainImpl~
        -default_filter_chain_: FilterChainImplPtr
        -filter_chains_by_matcher_: FilterChainsByMatcher
    }

    class FilterChainsByMatcher {
        -dst_ports_map_: map~port, ...~
        -dst_ips_trie_: LcTrie
        -server_names_map_: map~sni, ...~
        -transport_protos_map_: map~proto, ...~
        -app_protos_map_: map~alpn, ...~
        -direct_src_ips_trie_: LcTrie
        -src_types_map_: map~type, ...~
        -src_ips_trie_: LcTrie
        -src_ports_map_: map~port, FilterChainImpl~
    }

    FilterChainManagerImpl *-- FilterChainsByMatcher
```

---

## 3. Matching Criteria Priority

Criteria are evaluated in strict order. At each level, the most specific match wins:

**Matching Strategy:**
- **Hierarchical**: Each level narrows the candidate filter chains
- **Priority-based**: Earlier criteria are matched first (destination before source)
- **Most specific wins**: Exact match beats wildcard, longest prefix beats shorter prefix
- **Fast**: Trie-based data structures enable O(log n) lookups for IP ranges

**Why This Order:**
1. **Destination first**: Where the client is connecting to (port, IP, SNI)
2. **Protocol negotiation**: Transport and application protocols
3. **Source last**: Who is connecting (useful for ACLs and segmentation)

**Practical Example:**
- Connection to `api.example.com:443` from `192.168.1.5:54321`
- TLS with ALPN `h2` (HTTP/2)
- Matching walks: port 443 → dest IP → SNI api.example.com → TLS → h2 → source 192.168.0.0/16
- Result: Most specific chain matching all these criteria

**Performance:**
- Destination IP and Source IP use **LcTrie** (Level-Compressed Trie) for fast prefix matching
- SNI uses hash map with exact and wildcard support
- Port, protocol, ALPN use simple hash maps
- Overall complexity: O(log n) for trie levels + O(1) for hash maps

```mermaid
flowchart TD
    Start["Accepted Connection"] --> P1["1. Destination Port<br/>(exact match)"]
    P1 --> P2["2. Destination IP<br/>(longest prefix match via LcTrie)"]
    P2 --> P3["3. Server Name / SNI<br/>(exact then wildcard)"]
    P3 --> P4["4. Transport Protocol<br/>(tls / raw_buffer / exact)"]
    P4 --> P5["5. Application Protocols / ALPN<br/>(h2, http/1.1)"]
    P5 --> P6["6. Direct Source IP<br/>(pre-XFF, LcTrie)"]
    P6 --> P7["7. Source Type<br/>(LOCAL / EXTERNAL / ANY)"]
    P7 --> P8["8. Source IP<br/>(LcTrie)"]
    P8 --> P9["9. Source Port<br/>(exact match)"]
    P9 --> Result["Matched FilterChainImpl<br/>or default_filter_chain_<br/>or REJECT"]
```

### Matching Priority Reference

| Priority | Field | Match Type | Example |
|----------|-------|-----------|---------|
| 1 | `destination_port` | Exact | `443` |
| 2 | `prefix_ranges` | Longest prefix (LcTrie) | `10.0.0.0/8` |
| 3 | `server_names` | Exact, then wildcard `*.` | `api.example.com` |
| 4 | `transport_protocol` | Exact | `tls` |
| 5 | `application_protocols` | Exact | `h2` |
| 6 | `direct_source_prefix_ranges` | Longest prefix (LcTrie) | `172.16.0.0/12` |
| 7 | `source_type` | Enum | `EXTERNAL` |
| 8 | `source_prefix_ranges` | Longest prefix (LcTrie) | `192.168.0.0/16` |
| 9 | `source_ports` | Exact | `5000` |

---

## 4. Nested Trie Structure

The internal matching structure is a deeply nested map/trie. Each level narrows the candidate set:

**How Nested Matching Works:**
- Start at root with all possible filter chains
- At each level, match one criterion and descend into matched subtree
- Each level has fewer candidates than the previous level
- Leaf nodes contain the actual `FilterChainImpl` pointer

**Example Walk Through Diagram:**

**Level 1 - Destination Port:**
- Split by port 443 and "any port"
- Incoming connection to port 443 → descend into `dst_port=443` branch

**Level 2 - Destination IP:**
- Under port 443, split by IP prefix `10.0.0.0/8` and "any IP"
- Incoming dest IP `10.1.2.3` matches `10.0.0.0/8` → descend

**Level 3 - SNI:**
- Under port 443 + IP 10.0.0.0/8, split by SNI
- Three options: exact `api.example.com`, wildcard `*.example.com`, any SNI
- Incoming SNI `api.example.com` → exact match descends

**Level 4 - Transport Protocol:**
- Under exact SNI match, split by transport
- Options: `tls` or `raw_buffer`
- TLS connection → descend into `transport=tls`

**Level 5 - ALPN:**
- Under TLS, split by ALPN protocol
- Options: `h2` (HTTP/2), `http/1.1`, any ALPN
- Connection negotiated HTTP/2 → select `chain-tls-h2`

**Key Benefits:**
- **Fast**: Each level is O(1) map lookup or O(log n) trie lookup
- **Flexible**: Can match any combination of criteria
- **Memory efficient**: Shared prefixes in trie reduce memory usage
- **Extensible**: New matching criteria can be added as new trie levels

```mermaid
flowchart TD
    Root["Root"] --> DstPort443["dst_port=443"]
    Root --> DstPortAny["dst_port=any"]

    DstPort443 --> DstIP10["dst_ip=10.0.0.0/8"]
    DstPort443 --> DstIPAny["dst_ip=any"]

    DstIP10 --> SNIapi["sni=api.example.com"]
    DstIP10 --> SNIwild["sni=*.example.com"]
    DstIP10 --> SNIany["sni=any"]

    SNIapi --> TLS["transport=tls"]
    SNIapi --> Raw["transport=raw_buffer"]

    TLS --> H2["alpn=h2 → chain-tls-h2"]
    TLS --> H1["alpn=http/1.1 → chain-tls-h1"]
    TLS --> ALPNAny["alpn=any → chain-tls-default"]
```

---

## 5. SNI Matching — Exact and Wildcard

**SNI (Server Name Indication) matching enables virtual hosting without terminating TLS:**

**Why SNI Matching Matters:**
- Allows multiple services on same IP:port with different filter chains
- TLS Inspector extracts SNI from ClientHello without decrypting payload
- Enables routing before TLS termination (SNI-based routing)
- Critical for multi-tenant systems and API gateways

**Matching Precedence:**
1. **Exact match**: `api.example.com` - highest priority
2. **Wildcard match**: `*.example.com` - medium priority
3. **Empty/catch-all**: Matches connections without SNI or unmatched SNI - lowest priority

**Wildcard Rules:**
- Only leading wildcard `*.` supported: `*.example.com`
- Matches exactly one subdomain level: `*.example.com` matches `api.example.com` but NOT `v1.api.example.com`
- Not supported: `api.*.com`, `*.*.example.com`, `*example.com`

**Example:**
- Connection with SNI `api.example.com`:
  - Exact match exists → use exact match chain (highest priority)
- Connection with SNI `web.example.com`:
  - No exact match → check wildcard `*.example.com` → matches
- Connection with SNI `other.com` or no SNI:
  - No exact or wildcard match → use empty catch-all if configured

```mermaid
flowchart TD
    Incoming["SNI: api.example.com"] --> Exact{"Exact match<br/>'api.example.com' in map?"}
    Exact -->|Yes| Match1["Use exact match chain"]
    Exact -->|No| Wild{"Wildcard match<br/>'*.example.com' in map?"}
    Wild -->|Yes| Match2["Use wildcard chain"]
    Wild -->|No| Empty{"Empty SNI entry<br/>(catch-all)?"}
    Empty -->|Yes| Match3["Use catch-all chain"]
    Empty -->|No| Next["Continue to next level<br/>or default chain"]
```

### Wildcard Rules

- Only leading `*.` wildcards are supported: `*.example.com`
- Not supported: `api.*.com`, `api.*`
- Wildcard matches only one domain level: `*.example.com` matches `api.example.com` but NOT `v1.api.example.com`

---

## 6. FilterChainImpl — Per Chain

Each filter chain holds everything needed to construct a connection's transport socket and network filter chain:

```mermaid
classDiagram
    class FilterChainImpl {
        +transportSocketFactory(): TransportSocketFactory
        +networkFilterFactories(): vector~NetworkFilterFactoryCb~
        +name(): string
        +startDraining()
        -transport_socket_factory_: TransportSocketFactoryPtr
        -filter_factories_: vector~NetworkFilterFactoryCb~
        -filter_chain_match_: FilterChainMatch
        -info_: FilterChainInfoImpl
        -state_: Active / Draining
    }

    class TransportSocketFactory {
        <<interface>>
        +createTransportSocket(options): TransportSocketPtr
        +implementsSecureTransport(): bool
    }

    class NetworkFilterFactoryCb {
        <<callback>>
        +operator()(FilterManager): void
    }

    FilterChainImpl *-- TransportSocketFactory
    FilterChainImpl *-- NetworkFilterFactoryCb
```

### What a `FilterChainImpl` Contains

| Component | Purpose |
|-----------|---------|
| `transport_socket_factory_` | Creates TLS or raw transport sockets for connections matched to this chain |
| `filter_factories_` | Ordered list of factory callbacks that add network filters to a connection |
| `filter_chain_match_` | The original match criteria proto (for debugging/stats) |
| `info_` | Metadata: name, filter chain info object |
| `state_` | Active or Draining |

---

## 7. Factory Contexts

Factory contexts provide filter factories access to Envoy internals (cluster manager, stats, runtime, etc.):

```mermaid
classDiagram
    class ListenerFactoryContextBaseImpl {
        +clusterManager(): ClusterManager
        +serverScope(): Scope
        +dispatcher(): Dispatcher
        +api(): Api
        +drainDecision(): DrainDecision
    }

    class PerListenerFactoryContextImpl {
        +listenerScope(): Scope
        +listenerInfo(): ListenerInfo
        +initManager(): Init::Manager
    }

    class PerFilterChainFactoryContextImpl {
        +drainDecision(): DrainDecision
        +overloadManager(): OverloadManager
        +scope(): Scope
    }

    ListenerFactoryContextBaseImpl <|-- PerListenerFactoryContextImpl
    PerListenerFactoryContextImpl --> PerFilterChainFactoryContextImpl
```

### Context Hierarchy

```mermaid
flowchart TD
    ServerCtx["Server-level context<br/>(main thread, shared)"] --> LFCBase["ListenerFactoryContextBaseImpl<br/>(per listener)"]
    LFCBase --> PLFC["PerListenerFactoryContextImpl<br/>(per listener, with stats scope)"]
    PLFC --> PFCFC["PerFilterChainFactoryContextImpl<br/>(per filter chain)"]
```

---

## 8. Building Filter Chains from Config

```mermaid
sequenceDiagram
    participant LI as ListenerImpl
    participant FCM as FilterChainManagerImpl
    participant Builder as ListenerFilterChainFactoryBuilder
    participant Creator as FilterChainFactoryContextCreator
    participant PLCF as ProdListenerComponentFactory

    LI->>FCM: addFilterChains(proto_filter_chains, default_fc, builder, creator)

    loop for each filter_chain proto
        FCM->>Creator: createFilterChainFactoryContext(fc_proto)
        Creator-->>FCM: PerFilterChainFactoryContextImpl

        FCM->>Builder: buildFilterChain(fc_proto, context)
        Builder->>PLCF: createNetworkFilterFactoryList(filters, context)
        PLCF-->>Builder: vector~NetworkFilterFactoryCb~
        Builder->>PLCF: createTransportSocketFactory(transport_socket, context)
        PLCF-->>Builder: TransportSocketFactoryPtr
        Builder-->>FCM: FilterChainImpl

        FCM->>FCM: insert into FilterChainsByMatcher trie
    end
```

---

## 9. Default Filter Chain

If no `filter_chain_match` matches the incoming connection, the `default_filter_chain` is used (if configured):

```mermaid
flowchart TD
    Conn["Incoming connection"] --> FCM["findFilterChain()"]
    FCM --> Match{"Any filter chain<br/>matches?"}
    Match -->|Yes| FC["Matched FilterChainImpl"]
    Match -->|No| Default{"default_filter_chain_<br/>configured?"}
    Default -->|Yes| DFC["Use default chain"]
    Default -->|No| Reject["Close connection<br/>stat: no_filter_chain_match++"]
```

---

## 10. Connection to Filter Chain Selection at Runtime

The full runtime sequence from accepted socket to matched filter chain:

```mermaid
sequenceDiagram
    participant ATL as ActiveTcpListener
    participant ATS as ActiveTcpSocket
    participant TI as TLS Inspector
    participant Socket as ConnectionSocket
    participant FCM as FilterChainManagerImpl

    ATL->>ATS: startFilterChain()
    ATS->>TI: onAccept(callbacks)
    TI->>TI: peek TLS ClientHello
    TI->>Socket: setRequestedServerName("api.example.com")
    TI->>Socket: setTransportProtocol("tls")
    TI->>Socket: setApplicationProtocols(["h2", "http/1.1"])
    TI-->>ATS: Continue

    ATS->>FCM: findFilterChain(socket, stream_info)

    Note over FCM: Walk nested trie:
    Note over FCM: 1. dst_port=443
    Note over FCM: 2. dst_ip=10.0.1.5 (match 10.0.0.0/8)
    Note over FCM: 3. sni=api.example.com (exact match)
    Note over FCM: 4. transport=tls
    Note over FCM: 5. alpn=h2

    FCM-->>ATS: FilterChainImpl (tls-api-h2)
    ATS->>ATL: newActiveConnection(chain, socket)
```

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_architecture.md) | Architecture, ListenerManagerImpl, Worker Dispatch, Lifecycle |
| **Part 2 (this file)** | Filter Chain Manager, Matching, ListenerImpl Config |
| [Part 3](OVERVIEW_PART3_active_tcp.md) | ActiveTcpListener, ActiveTcpSocket, Listener Filters, Connection Tracking |
| [Part 4](OVERVIEW_PART4_lds_and_advanced.md) | LDS API, UDP, Draining, Internal Listeners, Advanced Topics |
