# Part 9: HTTP Filter Manager — Decode and Encode Paths

## Overview

The HTTP `FilterManager` is the heart of Envoy's L7 processing. It manages two ordered chains of filters — **decoder filters** for request processing and **encoder filters** for response processing — and drives iteration through them with careful state management for buffering, watermarks, and local replies.

## FilterManager Architecture

```mermaid
graph TD
    subgraph "FilterManager"
        subgraph "Decoder Chain (Request Path)"
            direction LR
            DF1["CORS Filter"] --> DF2["Auth Filter"] --> DF3["Rate Limit"] --> DF4["Router Filter<br/>(terminal)"]
        end
        subgraph "Encoder Chain (Response Path)"
            direction RL
            EF4["Router Filter"] --> EF3["Rate Limit"] --> EF2["Auth Filter"] --> EF1["CORS Filter"]
        end
    end
    
    Request["Request from Codec"] --> DF1
    DF4 --> Upstream["To Upstream"]
    Upstream --> EF4
    EF1 --> Response["Response to Codec"]
    
    style DF4 fill:#ffcdd2
    style EF4 fill:#ffcdd2
```

**Key insight:** Decoder filters run in configuration order (A→B→C→Router). Encoder filters run in **reverse** order (Router→C→B→A).

## Key Classes

```mermaid
classDiagram
    class FilterManager {
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
        +encodeTrailers(trailers)
        +createFilterChain()
        -decoder_filters_ : StreamDecoderFilters
        -encoder_filters_ : StreamEncoderFilters
        -filters_ : vector~StreamFilterBase~
    }
    class DownstreamFilterManager {
        +sendLocalReply()
        +log()
        +onDestroy()
    }
    class ActiveStreamDecoderFilter {
        -handle_ : StreamDecoderFilterSharedPtr
        +decodeHeaders()
        +continueDecoding()
        +addDecodedData()
        +sendLocalReply()
    }
    class ActiveStreamEncoderFilter {
        -handle_ : StreamEncoderFilterSharedPtr
        +encodeHeaders()
        +continueEncoding()
        +addEncodedData()
    }
    class ActiveStreamFilterBase {
        -iteration_state_ : IterationState
        +commonContinue()
        +commonHandleAfterHeadersCallback()
    }

    DownstreamFilterManager --|> FilterManager
    ActiveStreamDecoderFilter --|> ActiveStreamFilterBase
    ActiveStreamEncoderFilter --|> ActiveStreamFilterBase
    FilterManager --> ActiveStreamDecoderFilter : "decoder_filters_"
    FilterManager --> ActiveStreamEncoderFilter : "encoder_filters_"
```

**Location:** `source/common/http/filter_manager.h` (lines 434-768)

## HTTP Filter Interfaces

```mermaid
classDiagram
    class StreamDecoderFilter {
        <<interface>>
        +decodeHeaders(headers, end_stream) FilterHeadersStatus
        +decodeData(data, end_stream) FilterDataStatus
        +decodeTrailers(trailers) FilterTrailersStatus
        +decodeMetadata(metadata) FilterMetadataStatus
        +setDecoderFilterCallbacks(callbacks)
    }
    class StreamEncoderFilter {
        <<interface>>
        +encodeHeaders(headers, end_stream) FilterHeadersStatus
        +encodeData(data, end_stream) FilterDataStatus
        +encodeTrailers(trailers) FilterTrailersStatus
        +setEncoderFilterCallbacks(callbacks)
    }
    class StreamFilter {
        <<interface>>
    }
    class StreamFilterBase {
        <<interface>>
        +onStreamComplete()
        +onMatchCallback(data)
        +onDestroy()
    }

    StreamFilter --|> StreamDecoderFilter
    StreamFilter --|> StreamEncoderFilter
    StreamDecoderFilter --|> StreamFilterBase
    StreamEncoderFilter --|> StreamFilterBase
```

**Location:** `envoy/http/filter.h` (lines 619-836)

## Filter Chain Creation

### How HTTP Filter Factories Work

```mermaid
sequenceDiagram
    participant AS as ActiveStream
    participant FM as FilterManager
    participant FCCBI as FilterChainFactoryCallbacksImpl
    participant HCMConfig as HttpConnectionManagerConfig
    participant FCU as FilterChainUtility
    participant Factory1 as CORS Factory
    participant Factory2 as Router Factory

    AS->>FM: createFilterChain()
    FM->>FCCBI: new FilterChainFactoryCallbacksImpl(filter_manager)
    FM->>HCMConfig: createFilterChain(callbacks)
    HCMConfig->>FCU: createFilterChainForFactories(callbacks, filter_factories_)
    
    loop For each factory
        FCU->>Factory1: factory(callbacks)
        Factory1->>FCCBI: addStreamDecoderFilter(cors_filter)
        Note over FCCBI: Creates ActiveStreamDecoderFilter<br/>adds to decoder_filters_
        
        FCU->>Factory2: factory(callbacks)
        Factory2->>FCCBI: addStreamDecoderFilter(router_filter)
        Note over FCCBI: Creates ActiveStreamDecoderFilter<br/>adds to decoder_filters_
    end
```

### FilterChainFactoryCallbacksImpl

This wrapper receives filter instances and creates the appropriate active filter wrappers:

```mermaid
flowchart TD
    CB["FilterChainFactoryCallbacksImpl"]
    
    CB -->|"addStreamDecoderFilter(filter)"| ADF["new ActiveStreamDecoderFilter<br/>→ decoder_filters_"]
    CB -->|"addStreamEncoderFilter(filter)"| AEF["new ActiveStreamEncoderFilter<br/>→ encoder_filters_"]
    CB -->|"addStreamFilter(filter)"| Both["new ActiveStreamDecoderFilter<br/>+ new ActiveStreamEncoderFilter"]
    CB -->|"addAccessLogHandler(handler)"| AL["access_log_handlers_"]
```

```
File: source/common/http/filter_manager.h (lines 648-676)

FilterChainFactoryCallbacksImpl:
    addStreamDecoderFilter(filter) → wraps in ActiveStreamDecoderFilter
    addStreamEncoderFilter(filter) → wraps in ActiveStreamEncoderFilter
    addStreamFilter(filter) → adds to BOTH chains
    addAccessLogHandler(handler) → stores for access logging
```

```
File: source/common/http/filter_chain_helper.cc (lines 18-45)

createFilterChainForFactories(callbacks, filter_factories):
    For each factory in filter_factories:
        config = provider.config()
        if missing → return false (ECDS not ready)
        config.value()(callbacks)  // invoke the factory lambda
```

## Decoder Path (Request Processing)

### Iteration Order

Decoder filters are iterated in **forward order** (first added = first called):

```mermaid
graph LR
    Entry["decodeHeaders()"] --> F1["Filter A<br/>decodeHeaders()"]
    F1 -->|Continue| F2["Filter B<br/>decodeHeaders()"]
    F2 -->|Continue| F3["Filter C<br/>decodeHeaders()"]
    F3 -->|Continue| FR["Router<br/>decodeHeaders()"]
    FR -->|StopIteration| Done["Iteration stops"]
```

### decodeHeaders() Flow

```mermaid
flowchart TD
    A["FilterManager::decodeHeaders(headers, end_stream)"] --> B["commonDecodePrefix() → find starting filter"]
    B --> C["iterate_action = FilterManager::decodeHeaders()"]
    
    C --> Loop["For each ActiveStreamDecoderFilter"]
    Loop --> Call["status = filter->decodeHeaders(headers, end_stream)"]
    Call --> After["commonHandleAfterHeadersCallback(status)"]
    
    After --> S1{Status?}
    S1 -->|Continue| Next["Next filter"]
    S1 -->|StopIteration| Stop1["Stop: resume via continueDecoding()"]
    S1 -->|StopAllIterationAndBuffer| Stop2["Stop: buffer all subsequent data"]
    S1 -->|StopAllIterationAndWatermark| Stop3["Stop: buffer with watermarks"]
    
    Next --> Loop
    
    style Stop1 fill:#fff9c4
    style Stop2 fill:#ffcdd2
    style Stop3 fill:#ffcdd2
```

```
File: source/common/http/filter_manager.cc (lines 361-434)

decodeHeaders(headers, end_stream):
    For each decoder filter (forward order):
        1. Set filter's end_stream flag
        2. status = filter->handle_->decodeHeaders(headers, end_stream)
        3. commonHandleAfterHeadersCallback(status, end_stream)
           - Continue → advance
           - StopIteration → stop, filter will call continueDecoding()
           - StopAllBuffer → stop, buffer all following data
           - StopAllWatermark → stop, buffer with watermark control
```

### decodeData() and decodeTrailers()

Same pattern — iterate forward, call each filter, handle stop/continue:

```mermaid
flowchart LR
    subgraph "Request Lifecycle"
        direction TB
        H["decodeHeaders()"] --> D["decodeData() (N times)"]
        D --> T["decodeTrailers()"]
    end
    
    subgraph "Each Step"
        direction TB
        I["Iterate filters forward"]
        I --> Check["Handle status"]
        Check --> Resume["Continue or buffer"]
    end
```

## Encoder Path (Response Processing)

### Iteration Order

Encoder filters are iterated in **reverse order** (last added = first called):

```mermaid
graph RL
    FR["Router<br/>encodeHeaders()"] --> F3["Filter C<br/>encodeHeaders()"]
    F3 -->|Continue| F2["Filter B<br/>encodeHeaders()"]
    F2 -->|Continue| F1["Filter A<br/>encodeHeaders()"]
    F1 -->|Continue| Done["Send to codec"]
    Entry["Response arrives"] --> FR
```

### encodeHeaders() Flow

```mermaid
flowchart TD
    A["FilterManager::encodeHeaders(headers, end_stream)"] --> B["commonEncodePrefix() → find starting filter"]
    B --> C["Iterate (reverse order)"]
    C --> Loop["For each ActiveStreamEncoderFilter"]
    Loop --> Call["status = filter->handle_->encodeHeaders(headers, end_stream)"]
    Call --> After["commonHandleAfterHeadersCallback(status)"]
    After --> S1{Status?}
    S1 -->|Continue| Next["Next filter (reverse)"]
    S1 -->|StopIteration| Stop["Stop: resume via continueEncoding()"]
    Next --> Loop
    Loop -->|"All done"| Codec["filter_manager_callbacks_.encodeHeaders()<br/>→ send to codec"]
    
    style Codec fill:#c8e6c9
```

## Iteration States

```mermaid
stateDiagram-v2
    [*] --> Continue : filter returns Continue
    [*] --> StopSingle : filter returns StopIteration
    [*] --> StopAllBuffer : filter returns StopAllIterationAndBuffer
    [*] --> StopAllWatermark : filter returns StopAllIterationAndWatermark
    
    StopSingle --> Continue : continueDecoding/Encoding()
    StopAllBuffer --> Continue : continueDecoding/Encoding()
    StopAllWatermark --> Continue : continueDecoding/Encoding()
    
    state "Buffering Behavior" as Buffering {
        StopSingle : Next callback (data/trailers)\nskips this filter
        StopAllBuffer : Buffer ALL data until\ncontinue is called
        StopAllWatermark : Buffer with high/low\nwatermark flow control
    }
```

### What Each Status Means

| Status | Effect on Headers | Effect on Data | Effect on Trailers |
|--------|------------------|----------------|-------------------|
| `Continue` | Pass to next filter | Pass to next filter | Pass to next filter |
| `StopIteration` | Stop at this filter | Data skips this filter until `continue` | Trailers skip until `continue` |
| `StopAllIterationAndBuffer` | Stop at this filter | Buffer all data | Buffer trailers |
| `StopAllIterationAndWatermark` | Stop at this filter | Buffer with watermarks | Buffer trailers |

## Resuming Iteration with continueDecoding()

```mermaid
sequenceDiagram
    participant FA as Filter A
    participant FB as Filter B (async)
    participant FC as Filter C
    participant FM as FilterManager

    FM->>FA: decodeHeaders(headers)
    FA-->>FM: Continue
    FM->>FB: decodeHeaders(headers)
    FB-->>FM: StopAllIterationAndBuffer
    Note over FM: Iteration paused at Filter B
    
    Note over FB: Async work (e.g., ext_authz call)
    
    FB->>FM: continueDecoding()
    Note over FM: commonContinue() resumes
    FM->>FM: doHeaders() → FC sees headers
    FM->>FC: decodeHeaders(headers)
    FC-->>FM: Continue
    FM->>FM: doData() → FC sees buffered data
    FM->>FC: decodeData(buffered_data)
    FC-->>FM: Continue
```

```
File: source/common/http/filter_manager.h (lines ~160-180)

commonContinue():
    1. doHeaders(headers) → resume headers iteration from next filter
    2. doData(data) → resume data iteration if data was buffered
    3. doTrailers(trailers) → resume trailers if buffered
    4. doMetadata() → resume metadata if buffered
```

## Filter Sending a Local Reply

Any filter can short-circuit the request by sending a local reply:

```mermaid
flowchart TD
    A["Filter B: sendLocalReply(403, 'Forbidden')"] --> B["FilterManager::sendLocalReply()"]
    B --> C["Build response headers (403)"]
    C --> D["encodeHeaders() — starts encode path"]
    D --> E["Encode filters run (from B backward to A)"]
    E --> F["Response sent to codec"]
    F --> G["decodeHeaders() iteration stops"]
    
    Note1["Filter C and Router never see<br/>this request"]
    
    style A fill:#ffcdd2
```

## Dual Filters (StreamFilter)

Many filters implement both decode and encode interfaces:

```mermaid
graph TD
    subgraph "Lua Filter (StreamFilter)"
        DF["Decoder side<br/>decodeHeaders()<br/>decodeData()"]
        EF["Encoder side<br/>encodeHeaders()<br/>encodeData()"]
    end
    
    subgraph "Filter Chain"
        D1["... → Lua (decode) → ..."] 
        E1["... → Lua (encode) → ..."]
    end
    
    DF -.-> D1
    EF -.-> E1
```

When added via `addStreamFilter()`, the same filter object gets **two** wrappers — an `ActiveStreamDecoderFilter` and an `ActiveStreamEncoderFilter`. The filter sees both the request and response.

## Typical HTTP Filter Chain

```mermaid
graph TB
    subgraph "Common Filter Chain (decode order)"
        direction TB
        F1["CORS<br/>(StreamFilter)"]
        F2["CSRF<br/>(StreamDecoderFilter)"]
        F3["External Authorization<br/>(StreamDecoderFilter)"]
        F4["Rate Limit<br/>(StreamDecoderFilter)"]
        F5["Header Manipulation<br/>(StreamDecoderFilter)"]
        F6["Lua<br/>(StreamFilter)"]
        F7["Router<br/>(StreamDecoderFilter — TERMINAL)"]
    end
    
    F1 --> F2 --> F3 --> F4 --> F5 --> F6 --> F7
    
    style F7 fill:#ffcdd2,stroke:#c62828,stroke-width:2px
```

## Key Source Files

| File | Lines | What It Does |
|------|-------|-------------|
| `source/common/http/filter_manager.h` | 434-768 | `FilterManager` class |
| `source/common/http/filter_manager.h` | 771-1084 | `DownstreamFilterManager` |
| `source/common/http/filter_manager.h` | 225-299 | `ActiveStreamDecoderFilter` |
| `source/common/http/filter_manager.h` | 304-343 | `ActiveStreamEncoderFilter` |
| `source/common/http/filter_manager.h` | 106-221 | `ActiveStreamFilterBase` |
| `source/common/http/filter_manager.h` | 648-676 | `FilterChainFactoryCallbacksImpl` |
| `source/common/http/filter_manager.cc` | 361-434 | `decodeHeaders()` iteration |
| `source/common/http/filter_manager.cc` | 436-516 | `decodeData()` iteration |
| `source/common/http/filter_manager.cc` | 754-831 | `encodeHeaders()` iteration |
| `source/common/http/filter_chain_helper.cc` | 18-45 | `createFilterChainForFactories()` |
| `envoy/http/filter.h` | 619-836 | HTTP filter interfaces |

---

**Previous:** [Part 8 — HTTP Codec Layer](08-http-codec.md)  
**Next:** [Part 10 — Router Filter and Upstream Request Flow](10-router-filter.md)
