# Part 2: `source/common/http/` — Connection Management & Filter Manager

## Overview

The `http/` folder is the largest and most important folder in `source/common/`. It contains the HTTP Connection Manager, the HTTP filter chain manager, the HTTP codecs, header map implementations, connection pools, and all supporting HTTP infrastructure.

This document covers the two central classes: `ConnectionManagerImpl` and `FilterManager`.

## Folder Structure

```mermaid
graph TD
    subgraph "source/common/http/"
        Core["Core Files"]
        H1["http1/ — HTTP/1.1 codec"]
        H2["http2/ — HTTP/2 codec"]
        H3["http3/ — HTTP/3 pool"]
        Match["matching/ — HTTP matchers"]
        SSE["sse/ — Server-Sent Events"]
    end
    
    Core --> CMI["conn_manager_impl.h/cc\n(ConnectionManagerImpl)"]
    Core --> FM["filter_manager.h/cc\n(FilterManager)"]
    Core --> CC["codec_client.h/cc\n(CodecClient)"]
    Core --> CPB["conn_pool_base.h/cc\n(HttpConnPoolImplBase)"]
    Core --> HMI["header_map_impl.h/cc\n(HeaderMapImpl)"]
    Core --> UTL["utility.h/cc\n(HTTP utilities)"]
    Core --> CMU["conn_manager_utility.h/cc"]
    Core --> FCH["filter_chain_helper.h/cc"]
    Core --> ACI["async_client_impl.h/cc"]
```

## ConnectionManagerImpl — The HTTP Brain

### Class Diagram

```mermaid
classDiagram
    class ConnectionManagerImpl {
        -codec_ : ServerConnectionPtr
        -streams_ : LinkedList~ActiveStream~
        -config_ : ConnectionManagerConfig&
        -read_callbacks_ : ReadFilterCallbacks*
        -drain_state_ : DrainState
        -user_agent_ : UserAgent
        +onData(buffer, end_stream) FilterStatus
        +onNewConnection() FilterStatus
        +newStream(encoder) RequestDecoder
        +createCodec(buffer)
        +doEndStream(stream)
        +checkForDeferredClose()
    }
    class ActiveStream {
        -filter_manager_ : DownstreamFilterManager
        -response_encoder_ : ResponseEncoder*
        -request_headers_ : RequestHeaderMapSharedPtr
        -state_ : State
        -stream_id_ : uint64_t
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +encodeHeaders(headers, end_stream)
    }
    class ActiveStreamHandle {
        -stream_ : ActiveStream*
        -is_valid_ : bool
        +get() ActiveStream*
    }
    class ConnectionManagerConfig {
        <<interface>>
        +createCodec() ServerConnectionPtr
        +createFilterChain() bool
        +routeConfig() Router::Config
        +serverName() string
        +idleTimeout() Duration
    }

    ConnectionManagerImpl --> ActiveStream : "streams_"
    ConnectionManagerImpl --> ConnectionManagerConfig : "config_"
    ActiveStream --> ActiveStreamHandle : "handle_"
    ActiveStream --> DownstreamFilterManager : "filter_manager_"
```

### ConnectionManagerImpl Internal Architecture

```mermaid
graph TD
    subgraph "ConnectionManagerImpl"
        Codec["codec_\n(ServerConnection)"]
        Streams["streams_\n(LinkedList of ActiveStream)"]
        Config["config_\n(ConnectionManagerConfig)"]
        RC["read_callbacks_\n(connection reference)"]
        UA["user_agent_\n(UserAgent parser)"]
        Drain["drain_state_\n(DrainState)"]
        
        subgraph "Per-Stream (ActiveStream)"
            AS_FM["filter_manager_\n(DownstreamFilterManager)"]
            AS_RE["response_encoder_\n(ResponseEncoder*)"]
            AS_RH["request_headers_"]
            AS_State["state_\n(flags, timers)"]
        end
    end
    
    RC -->|"network data"| Codec
    Codec -->|"newStream()"| Streams
    Streams --> AS_FM
    Config -->|"createFilterChain()"| AS_FM
```

### Key Methods — What They Do

| Method | File:Lines | Purpose |
|--------|-----------|---------|
| `onData()` | `conn_manager_impl.cc:415-467` | Receives raw bytes from network, dispatches to codec |
| `onNewConnection()` | `conn_manager_impl.cc:471-482` | HTTP/3 codec creation (protocol known at connection time) |
| `newStream()` | `conn_manager_impl.cc:324-379` | Creates `ActiveStream` for each new HTTP request |
| `createCodec()` | `conn_manager_impl.cc:394-414` | Lazy codec creation (HTTP/1, HTTP/2, or auto-detect) |
| `doEndStream()` | `conn_manager_impl.cc` | Cleans up a finished stream |
| `checkForDeferredClose()` | `conn_manager_impl.cc` | Closes connection when all streams are done (drain) |

### ActiveStream Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Created : newStream()
    Created --> HeadersDecoded : decodeHeaders()
    
    state HeadersDecoded {
        [*] --> RouteResolved : refreshCachedRoute()
        RouteResolved --> FilterChainBuilt : createFilterChain()
        FilterChainBuilt --> FiltersRunning : filter_manager_.decodeHeaders()
    }
    
    HeadersDecoded --> BodyDecoding : decodeData()
    BodyDecoding --> TrailersDecoding : decodeTrailers()
    
    HeadersDecoded --> Responding : end_stream=true (no body)
    TrailersDecoding --> Responding : filters complete
    BodyDecoding --> Responding : end_stream=true
    
    state Responding {
        [*] --> EncodeHeaders : encodeHeaders()
        EncodeHeaders --> EncodeBody : encodeData()
        EncodeBody --> EncodeDone : end_stream=true
    }
    
    Responding --> Destroyed : stream complete
    Destroyed --> [*]
```

## FilterManager — HTTP Filter Chain Engine

### Class Hierarchy

```mermaid
classDiagram
    class FilterManager {
        -decoder_filters_ : StreamDecoderFilters
        -encoder_filters_ : StreamEncoderFilters
        -filters_ : vector~StreamFilterBase*~
        -filter_manager_callbacks_ : FilterManagerCallbacks&
        -stream_info_ : StreamInfo
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
        +encodeTrailers(trailers)
        +createFilterChain()
    }
    class DownstreamFilterManager {
        +sendLocalReply(code, body, ...)
        +log()
        +onDestroy()
        -account_ : BufferMemoryAccount
    }
    class ActiveStreamFilterBase {
        -iteration_state_ : IterationState
        -headers_continued_ : bool
        +commonContinue()
        +commonHandleAfterHeadersCallback(status)
        +commonHandleAfterDataCallback(status)
    }
    class ActiveStreamDecoderFilter {
        -handle_ : StreamDecoderFilterSharedPtr
        +decodeHeaders(headers, end_stream)
        +continueDecoding()
        +sendLocalReply()
        +encodeHeaders()
    }
    class ActiveStreamEncoderFilter {
        -handle_ : StreamEncoderFilterSharedPtr
        +encodeHeaders(headers, end_stream)
        +continueEncoding()
        +addEncodedData()
    }

    DownstreamFilterManager --|> FilterManager
    ActiveStreamDecoderFilter --|> ActiveStreamFilterBase
    ActiveStreamEncoderFilter --|> ActiveStreamFilterBase
    FilterManager *-- ActiveStreamDecoderFilter
    FilterManager *-- ActiveStreamEncoderFilter
```

### Filter Chain Data Structures

```mermaid
graph TD
    subgraph "FilterManager internals"
        subgraph "StreamDecoderFilters (forward iteration)"
            DF["entries_ : vector~ActiveStreamDecoderFilterPtr~"]
            DF1["[0] CORS"] --> DF2["[1] Auth"] --> DF3["[2] Rate Limit"] --> DF4["[3] Router"]
        end
        
        subgraph "StreamEncoderFilters (reverse iteration)"
            EF["entries_ : vector~ActiveStreamEncoderFilterPtr~"]
            EF1["[0] CORS"] --> EF2["[1] Auth"] --> EF3["[2] Rate Limit"] --> EF4["[3] Router"]
            Note1["Iterated via rbegin():\nRouter → Rate Limit → Auth → CORS"]
        end
        
        subgraph "filters_ (all filters)"
            AF["vector~StreamFilterBase*~"]
        end
    end
```

### Decode Path — Step by Step

```mermaid
sequenceDiagram
    participant AS as ActiveStream
    participant FM as FilterManager
    participant F1 as CORS (decoder)
    participant F2 as Auth (decoder)
    participant F3 as Router (decoder)

    AS->>FM: decodeHeaders(headers, end_stream)
    FM->>FM: commonDecodePrefix() → start filter
    
    FM->>F1: handle_->decodeHeaders(headers, end_stream)
    F1-->>FM: FilterHeadersStatus::Continue
    FM->>FM: commonHandleAfterHeadersCallback(Continue)
    
    FM->>F2: handle_->decodeHeaders(headers, end_stream)
    F2-->>FM: FilterHeadersStatus::StopAllIterationAndBuffer
    Note over FM: Iteration paused; F2 does async auth
    
    Note over F2: Auth check completes
    F2->>FM: continueDecoding()
    FM->>FM: commonContinue() → doHeaders()
    
    FM->>F3: handle_->decodeHeaders(headers, end_stream)
    F3-->>FM: FilterHeadersStatus::StopIteration
    Note over F3: Router starts upstream request
```

### Encode Path — Step by Step

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant FM as FilterManager
    participant F2 as Auth (encoder)
    participant F1 as CORS (encoder)
    participant AS as ActiveStream
    participant RE as ResponseEncoder

    Router->>FM: callbacks_->encodeHeaders(200, headers)
    FM->>FM: encodeHeaders() → reverse iterate
    
    FM->>F2: handle_->encodeHeaders(headers, end_stream)
    F2-->>FM: FilterHeadersStatus::Continue
    
    FM->>F1: handle_->encodeHeaders(headers, end_stream)
    F1-->>FM: FilterHeadersStatus::Continue
    
    FM->>AS: filter_manager_callbacks_.encodeHeaders()
    AS->>RE: response_encoder_->encodeHeaders(headers)
    Note over RE: Serialize to wire format
```

## FilterChainFactoryCallbacksImpl

This class bridges filter factory lambdas to the FilterManager:

```mermaid
classDiagram
    class FilterChainFactoryCallbacksImpl {
        -filter_manager_ : FilterManager&
        +addStreamDecoderFilter(filter)
        +addStreamEncoderFilter(filter)
        +addStreamFilter(filter)
        +addAccessLogHandler(handler)
    }
    class FilterChainFactoryCallbacks {
        <<interface>>
        +addStreamDecoderFilter(filter)
        +addStreamEncoderFilter(filter)
        +addStreamFilter(filter)
    }

    FilterChainFactoryCallbacksImpl ..|> FilterChainFactoryCallbacks
    FilterChainFactoryCallbacksImpl --> FilterManager : "adds filters to"
```

## ConnectionManagerConfig — HCM Settings

```mermaid
graph TD
    CMC["ConnectionManagerConfig"]
    
    CMC --> Codec["createCodec()\n→ HTTP codec factory"]
    CMC --> FC["createFilterChain()\n→ HTTP filter chain"]
    CMC --> RC["routeConfig()\n→ route table"]
    CMC --> Timeouts["requestTimeout()\nidleTimeout()\nmaxStreamDuration()"]
    CMC --> Limits["maxRequestHeadersKb()\nmaxRequestHeadersCount()"]
    CMC --> Headers["serverName()\nscheme()\nvia()"]
    CMC --> Features["proxy100Continue()\nrepresentIPv4RemoteAddressAsIPv4MappedIPv6()\nstreamErrorOnInvalidHttpMessage()"]
    CMC --> Tracing["tracingConfig()\noperationName()"]
    CMC --> Drain["drainTimeout()\ndelayedCloseTimeout()"]
```

## Key Supporting Classes

### conn_manager_utility.h

| Method | Purpose |
|--------|---------|
| `determineNextProtocol()` | Detects HTTP protocol from ALPN or data |
| `autoCreateCodec()` | Creates HTTP/1 or HTTP/2 codec based on detection |
| `mutateRequestHeaders()` | Adds `x-forwarded-for`, `x-request-id`, tracing headers |
| `mutateResponseHeaders()` | Adds `via` header, strips internal headers |
| `maybeNormalizePath()` | Normalizes URL path (RFC compliance) |
| `maybeNormalizeHost()` | Normalizes Host header |

### filter_chain_helper.h

| Class/Function | Purpose |
|---------------|---------|
| `FilterChainUtility::createFilterChainForFactories()` | Iterates filter factories to build chain |
| `FilterChainHelper` | Template for processing filter config with dependency checking |
| `MissingConfigFilter` | Placeholder filter when ECDS config not yet available |

### dependency_manager.h

| Class | Purpose |
|-------|---------|
| `DependencyManager` | Validates filter dependencies (e.g., filter A requires filter B before it) |

## File Catalog

| File | Key Classes | Purpose |
|------|-------------|---------|
| `conn_manager_impl.h/cc` | `ConnectionManagerImpl`, `ActiveStream` | HTTP connection manager (HCM) |
| `conn_manager_config.h` | `ConnectionManagerConfig`, stats structs | HCM configuration interface |
| `conn_manager_utility.h/cc` | `ConnectionManagerUtility` | Protocol detection, header mutation |
| `filter_manager.h/cc` | `FilterManager`, `DownstreamFilterManager`, `ActiveStream*Filter` | HTTP filter chain management |
| `filter_chain_helper.h/cc` | `FilterChainUtility`, `FilterChainHelper` | Filter chain building |
| `dependency_manager.h/cc` | `DependencyManager` | Filter dependency validation |
| `utility.h/cc` | `Url`, `PercentEncoding`, local reply helpers | HTTP utilities |
| `codes.h/cc` | HTTP status codes | Code definitions |
| `status.h/cc` | `Status` | Codec status type |
| `exception.h` | HTTP exceptions | Error types |
| `path_utility.h/cc` | Path utilities | Path normalization |
| `date_provider_impl.h/cc` | `DateProvider` | Date header caching |
| `user_agent.h/cc` | `UserAgent` | User-Agent parsing |
| `context_impl.h/cc` | HTTP context | HTTP context implementation |
| `request_id_extension_impl.h/cc` | Request ID extension | UUID-based request IDs |

---

**Next:** [Part 3 — HTTP Codecs, Headers, and Connection Pools](03-http-codecs-headers-pools.md)
