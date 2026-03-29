# FilterChainManagerImpl

**Files:** `source/common/listener_manager/filter_chain_manager_impl.h` / `.cc`  
**Size:** ~19 KB header, ~39 KB implementation  
**Namespace:** `Envoy::Server`

## Overview

`FilterChainManagerImpl` selects the correct filter chain for an incoming connection based on the connection's metadata (destination IP, SNI, ALPN, source IP/port). It uses a nested trie structure for fast O(1) matching across multiple criteria. It also manages filter chain lifecycle, draining, and factory context creation.

## Class Hierarchy

```mermaid
classDiagram
    class FilterChainManagerImpl {
        +findFilterChain(socket, info): FilterChain*
        +addFilterChains(filter_chain_configs, default_fc, builder, context_creator)
        +createFilterChainFactoryContext(proto_config): FilterChainFactoryContextPtr
        -fc_contexts_: map~string, FilterChainImpl~
        -default_filter_chain_: FilterChainImplPtr
        -filter_chains_by_matcher_: FilterChainsByMatcher
    }

    class FilterChainManager {
        <<interface>>
        +findFilterChain(socket, info): FilterChain*
    }

    class FilterChainImpl {
        +transportSocketFactory(): TransportSocketFactory
        +networkFilterFactories(): vector~NetworkFilterFactoryCb~
        +name(): string
        +startDraining()
        -transport_socket_factory_: TransportSocketFactoryPtr
        -filter_factories_: vector~NetworkFilterFactoryCb~
    }

    class DrainableFilterChain {
        <<interface>>
        +startDraining()
    }

    class FilterChainInfoImpl {
        +name(): string
    }

    class PerFilterChainFactoryContextImpl {
        +drainDecision(): DrainDecision
        +scope(): Stats::Scope
    }

    FilterChainManager <|-- FilterChainManagerImpl
    DrainableFilterChain <|-- FilterChainImpl
    FilterChainImpl *-- FilterChainInfoImpl
    FilterChainManagerImpl *-- FilterChainImpl
    FilterChainManagerImpl *-- PerFilterChainFactoryContextImpl
```

## Filter Chain Selection — Matching Criteria

Matching criteria are evaluated in a strict priority order. The first matching filter chain wins:

```mermaid
flowchart TD
    Conn["Incoming Connection<br/>(metadata from socket + listener filters)"] --> M1{"1. Destination Port"}
    M1 --> M2{"2. Destination IP<br/>(LC-Trie)"}
    M2 --> M3{"3. Server Name (SNI)<br/>(exact + wildcard)"}
    M3 --> M4{"4. Transport Protocol<br/>(tls / raw_buffer)"}
    M4 --> M5{"5. Application Protocols<br/>(ALPN: h2, http/1.1)"}
    M5 --> M6{"6. Direct Source IP<br/>(pre-XFF)"}
    M6 --> M7{"7. Source Type<br/>(LOCAL / EXTERNAL / ANY)"}
    M7 --> M8{"8. Source IP<br/>(LC-Trie)"}
    M8 --> M9{"9. Source Port"}
    M9 --> FC["Matched FilterChainImpl"]
    M9 -->|no match| Default["default_filter_chain_<br/>(if configured)"]
    Default -->|no default| Reject["Connection rejected"]
```

## Internal Matching Structure — `FilterChainsByMatcher`

The matching is implemented as a nested map/trie structure, with each level narrowing the candidate set:

```mermaid
flowchart TD
    Root["FilterChainsByMatcher (root)"] --> DstPort["map: dest_port → ..."]
    DstPort --> DstIP["LcTrie: dest_ip_prefix → ..."]
    DstIP --> SNI["map: server_name → ..."]
    SNI --> TransProto["map: transport_protocol → ..."]
    TransProto --> ALPN["map: application_protocol → ..."]
    ALPN --> DirectSrcIP["LcTrie: direct_source_ip → ..."]
    DirectSrcIP --> SrcType["map: source_type → ..."]
    SrcType --> SrcIP["LcTrie: source_ip → ..."]
    SrcIP --> SrcPort["map: source_port → FilterChainImpl"]
```

## `findFilterChain` Flow

```mermaid
sequenceDiagram
    participant ATL as ActiveTcpListener
    participant FCM as FilterChainManagerImpl
    participant Socket as ConnectionSocket
    participant LT as LcTrie (dest IPs)
    participant SNI_Map as SNI Map

    ATL->>FCM: findFilterChain(socket, stream_info)
    FCM->>Socket: connectionInfoProvider().localAddress()
    FCM->>LT: lookup(dest_ip)
    LT-->>FCM: candidate set narrowed by dest IP
    FCM->>Socket: requestedServerName()
    FCM->>SNI_Map: lookup(sni)
    SNI_Map-->>FCM: further narrowed
    FCM->>Socket: detectedTransportProtocol()
    FCM->>Socket: requestedApplicationProtocols()
    FCM->>Socket: connectionInfoProvider().remoteAddress()
    FCM-->>ATL: FilterChain* (best match or default)
```

## SNI Matching — Exact and Wildcard

```mermaid
flowchart TD
    SNI["Requested SNI:<br/>api.example.com"] --> B{Exact match in map?}
    B -->|Yes| Found["Matched: api.example.com"]
    B -->|No| C{Wildcard match?<br/>*.example.com}
    C -->|Yes| WFound["Matched: *.example.com"]
    C -->|No| D{Empty SNI match?<br/>(catch-all)"}
    D -->|Yes| CatchAll["Matched: catch-all chain"]
    D -->|No| NoMatch["No SNI match at this level"]
```

## `addFilterChains` — Building the Trie

```mermaid
sequenceDiagram
    participant LI as ListenerImpl
    participant FCM as FilterChainManagerImpl
    participant Builder as FilterChainFactoryBuilder

    LI->>FCM: addFilterChains(filter_chain_configs, default_fc, builder, context_creator)

    loop for each FilterChain proto
        FCM->>Builder: buildFilterChain(fc_config, context)
        Builder-->>FCM: NetworkFilterFactoriesAndMetadata
        FCM->>FCM: create FilterChainImpl
        FCM->>FCM: insert into FilterChainsByMatcher trie
    end

    alt default_filter_chain configured
        FCM->>Builder: buildFilterChain(default_fc_config, context)
        FCM->>FCM: set default_filter_chain_
    end
```

## `FilterChainImpl` — Per Filter Chain

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
    }
```

What it holds per filter chain:

| Field | Purpose |
|-------|---------|
| `transport_socket_factory_` | Creates the TransportSocket (TLS or raw) for this chain |
| `filter_factories_` | Ordered list of network filter factory callbacks |
| `filter_chain_match_` | The matching criteria proto |
| `info_` | Metadata (name, filter chain info) |

## Connection to Filter Chain — Runtime

```mermaid
sequenceDiagram
    participant Socket as AcceptedSocket
    participant FCM as FilterChainManagerImpl
    participant FCI as FilterChainImpl
    participant TSF as TransportSocketFactory
    participant CI as ConnectionImpl

    Socket->>FCM: findFilterChain(socket, info)
    FCM-->>Socket: FilterChainImpl*

    Socket->>FCI: transportSocketFactory()
    FCI-->>Socket: TransportSocketFactory*
    Socket->>TSF: createTransportSocket(options)
    TSF-->>Socket: TransportSocketPtr

    Socket->>CI: new ConnectionImpl(io_handle, transport_socket)
    Socket->>FCI: networkFilterFactories()
    loop for each factory
        CI->>CI: addReadFilter(factory(context))
    end
```

## Drain Flow

When a filter chain is replaced, the old `FilterChainImpl` is drained:

```mermaid
stateDiagram-v2
    [*] --> Active : filter chain created
    Active --> Draining : startDraining() called
    Draining --> Draining : existing connections continue
    Draining --> [*] : all connections closed
```

## Match Criteria Reference

| Priority | Match Field | Proto Field | Example Value |
|----------|-------------|-------------|--------------|
| 1 | Destination port | `destination_port` | `443` |
| 2 | Destination IP prefix | `prefix_ranges` | `10.0.0.0/8` |
| 3 | Server name (SNI) | `server_names` | `api.example.com`, `*.example.com` |
| 4 | Transport protocol | `transport_protocol` | `tls`, `raw_buffer` |
| 5 | Application protocols | `application_protocols` | `h2`, `http/1.1` |
| 6 | Direct source IP | `direct_source_prefix_ranges` | `172.16.0.0/12` |
| 7 | Source type | `source_type` | `LOCAL`, `EXTERNAL`, `ANY` |
| 8 | Source IP | `source_prefix_ranges` | `192.168.0.0/16` |
| 9 | Source port | `source_ports` | `5000` |
