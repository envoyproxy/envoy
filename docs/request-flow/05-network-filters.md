# Part 5: Network (L4) Filters — Creation and Data Flow

## Overview

Network filters operate at Layer 4, processing raw bytes flowing through a TCP connection. They sit between the transport socket (which handles TLS) and the application layer. The most important network filter is the HTTP Connection Manager (HCM), which bridges L4 and L7.

## Network Filter Interfaces

```mermaid
classDiagram
    class ReadFilter {
        <<interface>>
        +onData(Buffer data, bool end_stream) FilterStatus
        +onNewConnection() FilterStatus
        +initializeReadFilterCallbacks(ReadFilterCallbacks)
    }
    class WriteFilter {
        <<interface>>
        +onWrite(Buffer data, bool end_stream) FilterStatus
        +initializeWriteFilterCallbacks(WriteFilterCallbacks)
    }
    class Filter {
        <<interface>>
    }
    class ReadFilterCallbacks {
        <<interface>>
        +continueReading()
        +injectReadDataToFilterChain(buffer, end_stream)
        +connection() Connection
        +upstreamHost() HostDescription
    }
    class WriteFilterCallbacks {
        <<interface>>
        +injectWriteDataToFilterChain(buffer, end_stream)
        +connection() Connection
    }
    class FilterManager {
        <<interface>>
        +addReadFilter(filter)
        +addWriteFilter(filter)
        +addFilter(filter)
        +initializeReadFilters()
    }

    Filter --|> ReadFilter
    Filter --|> WriteFilter
    ReadFilter ..> ReadFilterCallbacks : "initialized with"
    WriteFilter ..> WriteFilterCallbacks : "initialized with"
    FilterManager --> ReadFilter : "manages"
    FilterManager --> WriteFilter : "manages"
```

**Interface location:** `envoy/network/filter.h`

- `ReadFilter` (lines 243-286) — processes incoming data (downstream → upstream direction)
- `WriteFilter` (lines 123-149) — processes outgoing data (upstream → downstream direction)
- `Filter` (lines 291-294) — combines both read and write

## Network Filter Factory Pattern

### NamedNetworkFilterConfigFactory

Each network filter extension provides a factory:

```mermaid
flowchart TD
    subgraph "Configuration Time"
        Proto["Protobuf Config"] --> Factory["NamedNetworkFilterConfigFactory<br/>::createFilterFactoryFromProto()"]
        Factory --> Lambda["FilterFactoryCb (lambda)<br/>captures shared config"]
    end
    
    subgraph "Runtime (per connection)"
        Lambda --> |"invoked with FilterManager"| Create["Create filter instance"]
        Create --> Add["filter_manager.addReadFilter(filter)<br/>or addWriteFilter(filter)"]
    end
```

```
File: envoy/server/filter_config.h (lines 156-191)

class NamedNetworkFilterConfigFactory : public ProtocolOptionsFactory {
    virtual absl::StatusOr<Network::FilterFactoryCb>
        createFilterFactoryFromProto(config, context) = 0;
};
```

### Example: TCP Proxy Filter Factory

```
File: source/extensions/filters/network/tcp_proxy/config.cc (lines 14-18)

Network::FilterFactoryCb createFilterFactoryFromProtoTyped(...) {
    auto filter_config = std::make_shared<TcpProxy::Config>(proto_config, context);
    return [filter_config, &context](Network::FilterManager& filter_manager) {
        filter_manager.addReadFilter(
            std::make_shared<TcpProxy::Filter>(filter_config, context.clusterManager()));
    };
}
```

Key pattern: the **config** object is shared (created once), but a new **filter** instance is created per connection.

## Building the Network Filter Chain

### buildFilterChain

When a new connection is created, `createNetworkFilterChain()` triggers filter chain construction:

```mermaid
sequenceDiagram
    participant ASL as ActiveStreamListenerBase
    participant LI as ListenerImpl
    participant FCU as FilterChainUtility
    participant FM as FilterManager (Connection)
    participant F1 as RBAC Factory
    participant F2 as HCM Factory

    ASL->>LI: createNetworkFilterChain(connection, filter_factories)
    LI->>FCU: buildFilterChain(connection, filter_factories)
    
    loop For each FilterConfigProvider
        FCU->>FCU: provider.config() → FilterFactoryCb
        alt Config missing (ECDS not ready)
            FCU-->>LI: return false (reject connection)
        else Config available
            FCU->>FM: factory(filter_manager)
            Note over FM: Factory calls addReadFilter() or addWriteFilter()
        end
    end
    
    FCU->>FM: initializeReadFilters()
    Note over FM: Calls onNewConnection() on each ReadFilter
    FM->>F1: filter.onNewConnection()
    FM->>F2: filter.onNewConnection()
```

```
File: source/server/configuration_impl.cc (lines 32-44)

bool FilterChainUtility::buildFilterChain(FilterManager& filter_manager,
                                          const NetworkFilterFactoriesList& factories) {
    for (const auto& filter_config_provider : factories) {
        auto config = filter_config_provider->config();
        if (!config.has_value()) return false;   // ECDS not ready
        Network::FilterFactoryCb& factory = config.value();
        factory(filter_manager);                  // adds filters to manager
    }
    return filter_manager.initializeReadFilters(); // calls onNewConnection()
}
```

## FilterManagerImpl — The Engine

`FilterManagerImpl` (`source/common/network/filter_manager_impl.h:107-196`) stores and iterates network filters:

### Internal Structure

```mermaid
graph LR
    subgraph "FilterManagerImpl"
        subgraph "upstream_filters_ (Read Path)"
            direction LR
            RF1["ActiveReadFilter<br/>RBAC"] --> RF2["ActiveReadFilter<br/>Rate Limit"] --> RF3["ActiveReadFilter<br/>HCM"]
        end
        subgraph "downstream_filters_ (Write Path)"
            direction RL
            WF1["ActiveWriteFilter<br/>Stats"] --> WF2["ActiveWriteFilter<br/>Access Log"]
        end
    end
    
    style RF1 fill:#e3f2fd
    style RF2 fill:#e3f2fd
    style RF3 fill:#e3f2fd
    style WF1 fill:#fff3e0
    style WF2 fill:#fff3e0
```

- **`upstream_filters_`** — read filters in FIFO order (first added = first called)
- **`downstream_filters_`** — write filters in LIFO order (first added = first called, reverse insertion)

### ActiveReadFilter and ActiveWriteFilter

Each filter is wrapped in an `ActiveReadFilter` or `ActiveWriteFilter` that implements the callback interfaces:

```mermaid
classDiagram
    class ActiveReadFilter {
        -filter_ : ReadFilterSharedPtr
        -parent_ : FilterManagerImpl
        -initialized_ : bool
        +continueReading()
        +injectReadDataToFilterChain()
        +connection()
    }
    class ActiveWriteFilter {
        -filter_ : WriteFilterSharedPtr
        -parent_ : FilterManagerImpl
        +injectWriteDataToFilterChain()
        +connection()
    }

    ActiveReadFilter ..|> ReadFilterCallbacks
    ActiveWriteFilter ..|> WriteFilterCallbacks
```

### Adding Filters

```
File: source/common/network/filter_manager_impl.cc (lines 14-30)

addReadFilter(filter):
    1. Create ActiveReadFilter wrapping the filter
    2. filter->initializeReadFilterCallbacks(*active_filter)
    3. LinkedList::moveIntoListBack(active_filter, upstream_filters_)  // FIFO

addWriteFilter(filter):
    1. Create ActiveWriteFilter wrapping the filter
    2. filter->initializeWriteFilterCallbacks(*active_filter)
    3. LinkedList::moveIntoList(active_filter, downstream_filters_)  // LIFO (prepend)

addFilter(filter):  // dual read+write filter
    addReadFilter(filter)
    addWriteFilter(filter)
```

## Data Flow — Read Path

### How Data Reaches Filters

```mermaid
flowchart TD
    A["Socket read event"] --> B["ConnectionImpl::onFileEvent(Read)"]
    B --> C["ConnectionImpl::onReadReady()"]
    C --> D["transport_socket_->doRead(read_buffer_)"]
    D --> E["read_buffer_ filled with decrypted bytes"]
    E --> F["ConnectionImpl::onRead(bytes_read)"]
    F --> G["filter_manager_.onRead()"]
    G --> H["onContinueReading(nullptr, connection)"]
    H --> I["Iterate through upstream_filters_"]
```

### Filter Iteration (Read)

```mermaid
sequenceDiagram
    participant FMI as FilterManagerImpl
    participant RF1 as RBAC Filter
    participant RF2 as Rate Limit Filter
    participant RF3 as HCM Filter

    FMI->>FMI: onContinueReading()
    
    alt First time (not initialized)
        FMI->>RF1: onNewConnection()
        RF1-->>FMI: Continue
    end
    FMI->>RF1: onData(buffer, end_stream)
    RF1-->>FMI: Continue
    
    alt First time
        FMI->>RF2: onNewConnection()
        RF2-->>FMI: Continue
    end
    FMI->>RF2: onData(buffer, end_stream)
    RF2-->>FMI: Continue
    
    alt First time
        FMI->>RF3: onNewConnection()
        RF3-->>FMI: Continue
    end
    FMI->>RF3: onData(buffer, end_stream)
    Note over RF3: HCM processes HTTP bytes
    RF3-->>FMI: StopIteration (consumed all data)
```

```
File: source/common/network/filter_manager_impl.cc (lines 62-97)

onContinueReading(filter, connection):
    For each ActiveReadFilter (starting from 'filter' or first):
        1. If not initialized:
           status = filter->onNewConnection()
           If StopIteration → stop, resume later
        2. If initialized and buffer has data:
           status = filter->onData(buffer, end_stream)
           If StopIteration → stop, resume later
        3. If Continue → advance to next filter
```

### Filter Return Values

| Return Value | Meaning |
|-------------|---------|
| `FilterStatus::Continue` | Data passes to the next filter |
| `FilterStatus::StopIteration` | Stop iteration; filter will call `continueReading()` when ready |

### Resuming Iteration

When a filter calls `continueReading()`, iteration resumes from the **next** filter:

```mermaid
flowchart LR
    F1["Filter 1<br/>Continue ✓"] --> F2["Filter 2<br/>StopIteration ⏸"]
    F2 -.->|"later: continueReading()"| F3["Filter 3<br/>Continue ✓"]
    F3 --> F4["Filter 4<br/>Continue ✓"]
```

## Data Flow — Write Path

### How Data Is Written

```mermaid
flowchart TD
    A["Application calls connection.write(data)"] --> B["ConnectionImpl::write()"]
    B --> C{"through_filter_chain?"}
    C -->|Yes| D["filter_manager_.onWrite()"]
    D --> E["Iterate downstream_filters_"]
    E --> F{All Continue?}
    F -->|Yes| G["Move data to write_buffer_"]
    F -->|No (StopIteration)| H["Buffer data, wait for resume"]
    G --> I["Schedule write event"]
    I --> J["ConnectionImpl::onWriteReady()"]
    J --> K["transport_socket_->doWrite(write_buffer_)"]
    K --> L["Encrypted bytes sent to socket"]
    C -->|No| G
```

```
File: source/common/network/filter_manager_impl.cc (lines 174-206)

onWrite():
    For each ActiveWriteFilter:
        status = filter->onWrite(buffer, end_stream)
        If StopIteration → stop, wait for injectWriteDataToFilterChain()
    If all Continue → return Continue (data ready to send)
```

## Common Network Filters

```mermaid
graph TD
    subgraph "Typical Network Filter Stack"
        direction TB
        NF1["RBAC Filter<br/>(L4 access control)"]
        NF2["Rate Limit Filter<br/>(connection rate limiting)"]
        NF3["TCP Proxy Filter<br/>(L4 proxying)"]
    end
    
    subgraph "HTTP-Aware Stack"
        direction TB
        HF1["RBAC Filter"]
        HF2["HTTP Connection Manager<br/>(terminal, bridges to L7)"]
    end
    
    style NF3 fill:#ffcdd2
    style HF2 fill:#c8e6c9
```

The **terminal** network filter is either:
- `tcp_proxy` — for L4 TCP proxying
- `http_connection_manager` — for HTTP proxying (most common)

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `envoy/network/filter.h` | 123-341 | All network filter interfaces |
| `source/common/network/filter_manager_impl.h` | 107-196 | `FilterManagerImpl` class |
| `source/common/network/filter_manager_impl.cc` | 14-30 | Adding filters |
| `source/common/network/filter_manager_impl.cc` | 62-97 | Read path iteration |
| `source/common/network/filter_manager_impl.cc` | 174-206 | Write path iteration |
| `source/server/configuration_impl.cc` | 32-44 | `buildFilterChain()` |
| `envoy/server/filter_config.h` | 156-191 | `NamedNetworkFilterConfigFactory` |
| `source/common/network/connection_impl.cc` | 367-396 | `onRead()` dispatches to filters |
| `source/common/network/connection_impl.cc` | 504-551 | `write()` dispatches to filters |

---

**Previous:** [Part 4 — Filter Chain Matching](04-filter-chain-matching.md)  
**Next:** [Part 6 — Transport Sockets and TLS](06-transport-sockets.md)
