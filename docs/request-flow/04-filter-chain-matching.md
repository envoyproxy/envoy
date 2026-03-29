# Part 4: Filter Chain Matching and Selection

## Overview

After listener filters finish inspecting the socket, Envoy must select which **filter chain** to use for the new connection. A single listener can have multiple filter chains, each with different TLS contexts, network filters, and configurations. The `FilterChainManagerImpl` performs a hierarchical match against socket properties to find the best chain.

## Why Multiple Filter Chains?

A single listener on port 443 might serve multiple domains with different TLS certificates and different backend configurations:

```mermaid
graph TD
    subgraph "Listener :443"
        L["Listen Socket"]
        
        FC1["Filter Chain 1<br/>SNI: api.example.com<br/>TLS: cert-api<br/>→ API network filters"]
        FC2["Filter Chain 2<br/>SNI: web.example.com<br/>TLS: cert-web<br/>→ Web network filters"]
        FC3["Filter Chain 3<br/>SNI: *.internal.com<br/>TLS: cert-internal<br/>→ Internal filters"]
        FCD["Default Filter Chain<br/>→ Catch-all"]
    end
    
    L --> FC1
    L --> FC2
    L --> FC3
    L --> FCD
```

## Key Classes

```mermaid
classDiagram
    class FilterChainManager {
        <<interface>>
        +findFilterChain(socket, stream_info) FilterChain*
    }
    class FilterChainManagerImpl {
        +addFilterChains(filter_chains, builder, manager)
        +findFilterChain(socket, stream_info) FilterChain*
        -filter_chains_ : map
        -default_filter_chain_
        -fc_contexts_ : map of destination port → match tree
    }
    class FilterChain {
        <<interface>>
        +transportSocketFactory() TransportSocketFactory
        +networkFilterFactories() NetworkFilterFactoriesList
    }
    class FilterChainImpl {
        -transport_socket_factory_
        -filters_factory_ : list of FilterConfigProviderPtr
        -name_
    }
    class FilterChainFactoryBuilder {
        <<interface>>
        +buildFilterChain(chain_config, context) FilterChainAndStatus
    }

    FilterChainManagerImpl ..|> FilterChainManager
    FilterChainImpl ..|> FilterChain
    FilterChainManagerImpl --> FilterChainImpl : "holds many"
    FilterChainManagerImpl --> FilterChainFactoryBuilder : "uses to build"
```

## FilterChainImpl — What a Filter Chain Contains

`FilterChainImpl` (`source/common/listener_manager/filter_chain_manager_impl.h:105-135`) holds everything needed to set up a connection:

```mermaid
graph TD
    FC["FilterChainImpl"]
    FC --> TSF["TransportSocketFactory<br/>(e.g., TLS context with certs)"]
    FC --> NFF["NetworkFilterFactories<br/>(list of FilterConfigProviderPtr)"]
    FC --> Meta["Metadata<br/>(labels, identity)"]
    FC --> Name["Name<br/>(for debugging/stats)"]
    
    NFF --> F1["RBAC Filter Factory"]
    NFF --> F2["Rate Limit Factory"]
    NFF --> F3["HCM Factory<br/>(HTTP Connection Manager)"]
```

Each `FilterConfigProviderPtr<FilterFactoryCb>` can be:
- **Static:** factory created at config time, never changes
- **Dynamic (ECDS):** factory updated via xDS, can be missing during warm-up

## The Matching Algorithm

### Hierarchical Match Tree

`FilterChainManagerImpl` builds a multi-level trie/map structure for efficient matching:

```mermaid
flowchart TD
    Socket["Incoming Socket"] --> DP["1. Destination Port"]
    DP --> DIP["2. Destination IP<br/>(LcTrie prefix match)"]
    DIP --> SN["3. Server Name (SNI)<br/>(exact or wildcard)"]
    SN --> TP["4. Transport Protocol<br/>('tls' or 'raw_buffer')"]
    TP --> AP["5. Application Protocol<br/>(ALPN: 'h2', 'http/1.1')"]
    AP --> DSIP["6. Direct Source IP"]
    DSIP --> ST["7. Source Type<br/>(local, external, any)"]
    ST --> SIP["8. Source IP"]
    SIP --> SP["9. Source Port"]
    SP --> Result{Match found?}
    Result -->|Yes| Chain["Use matched FilterChain"]
    Result -->|No| Default{Default chain?}
    Default -->|Yes| DefChain["Use default FilterChain"]
    Default -->|No| Reject["Reject connection"]
```

### Match Priority

Each level performs matching in the following priority:

| Level | Socket Property | Match Method |
|-------|----------------|-------------|
| 1 | Destination port | Exact match |
| 2 | Destination IP | Longest prefix match (CIDR) via LcTrie |
| 3 | Server name (SNI) | Exact → suffix wildcard (*.example.com) |
| 4 | Transport protocol | Exact ("tls", "raw_buffer") |
| 5 | Application protocol | Exact ("h2", "http/1.1") |
| 6 | Direct source IP | Longest prefix match |
| 7 | Source type | Local → External → Any |
| 8 | Source IP | Longest prefix match |
| 9 | Source port | Exact match |

### Matcher-Based Matching (Alternative)

Envoy also supports a newer unified matcher framework. When configured:

```mermaid
flowchart TD
    Socket["Incoming Socket"] --> MD["Build Network::MatchingData<br/>(from socket properties)"]
    MD --> ME["Matcher::evaluateMatch(matching_data)"]
    ME --> Action["MatchAction → FilterChain"]
```

```
File: source/common/listener_manager/filter_chain_manager_impl.cc (lines 366-393)

findFilterChain(socket, stream_info):
  if matcher configured:
    → Build MatchingData from socket
    → evaluateMatch() using xDS matcher
    → Return matched FilterChain from action
  else:
    → Hierarchical multi-level lookup through fc_contexts_
```

## How Filter Chains Are Built

### At Configuration Time

When a listener is created or updated, `FilterChainManagerImpl::addFilterChains()` processes the config:

```mermaid
sequenceDiagram
    participant LI as ListenerImpl
    participant FCMI as FilterChainManagerImpl
    participant Builder as FilterChainFactoryBuilder
    participant FC as FilterChainImpl

    LI->>FCMI: addFilterChains(configs, builder, manager)
    loop For each filter_chain_config
        FCMI->>Builder: buildFilterChain(config, context)
        Builder-->>FCMI: FilterChainImpl + warm status
        FCMI->>FCMI: Insert into match tree
        Note over FCMI: Index by dest_port → dest_ip<br/>→ server_name → transport_protocol<br/>→ app_protocol → source
    end
    FCMI->>FCMI: Build default_filter_chain_ (if configured)
```

### FilterChainFactoryBuilder

The builder creates the `FilterChainImpl` by:

1. Creating `DownstreamTransportSocketFactory` (TLS context) from `transport_socket` config
2. Creating network filter factories from `filters` config
3. Wrapping everything in a `FilterChainImpl`

```mermaid
flowchart TD
    Config["filter_chain config"] --> TSConfig["transport_socket config"]
    Config --> FiltersConfig["filters config"]
    
    TSConfig --> TSFactory["TransportSocketFactory<br/>(e.g., DownstreamTlsContext)"]
    
    FiltersConfig --> F1["Filter 1 Factory"]
    FiltersConfig --> F2["Filter 2 Factory"]
    FiltersConfig --> F3["Filter N Factory"]
    
    TSFactory --> FCI["FilterChainImpl"]
    F1 --> FCI
    F2 --> FCI
    F3 --> FCI
```

## Connection Creation with Matched Chain

After `findFilterChain()` returns a chain, `ActiveStreamListenerBase::newConnection()` uses it:

```mermaid
sequenceDiagram
    participant ATS as ActiveTcpSocket
    participant ASL as ActiveStreamListenerBase
    participant FCMI as FilterChainManagerImpl
    participant FC as FilterChainImpl
    participant Disp as Dispatcher

    ATS->>ASL: newConnection(socket, stream_info)
    ASL->>FCMI: findFilterChain(socket, stream_info)
    FCMI-->>ASL: FilterChainImpl* (or nullptr)
    
    alt No matching chain
        ASL->>ASL: Close socket (no matching SNI, etc.)
    else Chain found
        ASL->>FC: transportSocketFactory()
        FC-->>ASL: TransportSocketFactory
        ASL->>ASL: factory.createDownstreamTransportSocket()
        ASL->>Disp: createServerConnection(socket, transport_socket)
        Disp-->>ASL: ServerConnectionImpl*
        ASL->>ASL: createNetworkFilterChain(conn, fc.networkFilterFactories())
    end
```

## Example: Multi-Domain TLS Listener

```mermaid
graph TD
    subgraph "Listener :443 Match Tree"
        Root["Port 443"]
        Root --> AnyIP["0.0.0.0/0 (any dest IP)"]
        
        AnyIP --> SNI1["SNI: api.example.com"]
        AnyIP --> SNI2["SNI: web.example.com"]  
        AnyIP --> SNIWild["SNI: *.internal.com"]
        
        SNI1 --> TLS1["transport: tls"]
        SNI2 --> TLS2["transport: tls"]
        SNIWild --> TLS3["transport: tls"]
        
        TLS1 --> FC1["FilterChain: api-chain<br/>Cert: api-cert<br/>Filters: [rbac, hcm-api]"]
        TLS2 --> FC2["FilterChain: web-chain<br/>Cert: web-cert<br/>Filters: [hcm-web]"]
        TLS3 --> FC3["FilterChain: internal-chain<br/>Cert: internal-cert<br/>Filters: [rbac-strict, hcm-internal]"]
    end
    
    subgraph "Request Flow"
        Client["Client → SNI: api.example.com"]
        Client --> |"tls_inspector detects SNI"| Root
        Root --> |"match"| FC1
    end
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `source/common/listener_manager/filter_chain_manager_impl.h` | 105-135 | `FilterChainImpl` class |
| `source/common/listener_manager/filter_chain_manager_impl.h` | 139-276 | `FilterChainManagerImpl` class |
| `source/common/listener_manager/filter_chain_manager_impl.cc` | 366-393 | `findFilterChain()` entry point |
| `envoy/network/filter.h` | ~380-410 | `FilterChain` and `FilterChainManager` interfaces |
| `source/common/listener_manager/active_stream_listener_base.cc` | 20-49 | Uses matched chain to create connection |

---

**Previous:** [Part 3 — Listener Filters](03-listener-filters.md)  
**Next:** [Part 5 — Network (L4) Filters: Creation and Data Flow](05-network-filters.md)
