# FilterManager

**File:** `source/common/http/filter_manager.h` / `.cc`  
**Size:** ~55 KB header, ~89 KB implementation  
**Namespace:** `Envoy::Http`

## Overview

`FilterManager` is the HTTP filter chain execution engine. It maintains ordered lists of decoder and encoder filters, drives data through them, manages per-filter state (buffering, watermarks, stop/continue iteration), and handles local replies. `DownstreamFilterManager` extends it with downstream-specific behaviors (tracing, access logging, stream teardown).

## Class Hierarchy

```mermaid
classDiagram
    class FilterManager {
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +decodeTrailers(trailers)
        +encode1xxHeaders(headers)
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
        +encodeTrailers(trailers)
        +addStreamDecoderFilter(filter)
        +addStreamEncoderFilter(filter)
        +sendLocalReply(code, body, ...)
        -decoder_filters_: StreamDecoderFilters
        -encoder_filters_: StreamEncoderFilters
    }

    class DownstreamFilterManager {
        +chargeStats()
        +finalizeRequest()
        +requestRouteConfigUpdate()
        -callbacks_: FilterManagerCallbacks
    }

    class ActiveStreamFilterBase {
        +parent_: FilterManager
        +iteration_state_: IterationState
        +filter_context_: FilterContext
        +commonHandleAfterHeadersCallback()
        +commonHandleAfterDataCallback()
    }

    class ActiveStreamDecoderFilter {
        +handle_: StreamDecoderFilterSharedPtr
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
    }

    class ActiveStreamEncoderFilter {
        +handle_: StreamEncoderFilterSharedPtr
        +encodeHeaders(headers, end_stream)
        +encodeData(data, end_stream)
    }

    FilterManager <|-- DownstreamFilterManager
    ActiveStreamFilterBase <|-- ActiveStreamDecoderFilter
    ActiveStreamFilterBase <|-- ActiveStreamEncoderFilter
    FilterManager *-- ActiveStreamDecoderFilter : decoder_filters_
    FilterManager *-- ActiveStreamEncoderFilter : encoder_filters_
```

## Filter Chain Ordering

```mermaid
flowchart LR
    subgraph Decoder ["Decoder Filters (A → B → C → Router)"]
        direction LR
        A_d["Filter A\ndecodeHeaders()"] --> B_d["Filter B\ndecodeHeaders()"] --> C_d["Filter C\ndecodeHeaders()"] --> R["Router Filter\ndecodeHeaders()"]
    end

    subgraph Encoder ["Encoder Filters (C → B → A)"]
        direction LR
        C_e["Filter C\nencodeHeaders()"] --> B_e["Filter B\nencodeHeaders()"] --> A_e["Filter A\nencodeHeaders()"]
    end

    Net_in["Network\n(downstream)"] --> A_d
    R --> Upstream["Upstream"]
    Upstream --> C_e
    A_e --> Net_out["Network\n(downstream)"]
```

## Filter Iteration State Machine

Each `ActiveStreamFilterBase` has its own `IterationState` that controls whether the chain continues after the filter returns.

```mermaid
stateDiagram-v2
    [*] --> Continue
    Continue --> StopIteration : returns StopIteration
    Continue --> StopAllBuffer : returns StopAllIterationAndBuffer
    Continue --> StopAllWatermark : returns StopAllIterationAndWatermark
    StopIteration --> Continue : continueDecoding / continueEncoding
    StopAllBuffer --> Continue : continueDecoding
    StopAllWatermark --> Continue : continueDecoding
    Continue --> [*] : end of filter chain
```

## Decode Header Flow

```mermaid
sequenceDiagram
    participant CM as ConnectionManager
    participant FM as FilterManager
    participant FA as FilterA
    participant FB as FilterB
    participant Router as RouterFilter

    CM->>FM: decodeHeaders(headers, end_stream)
    FM->>FA: decodeHeaders(headers, end_stream)
    FA-->>FM: Continue
    FM->>FB: decodeHeaders(headers, end_stream)
    FB-->>FM: StopIteration
    Note over FM,FB: Iteration halted, headers buffered
    FB->>FM: continueDecoding() -- async
    FM->>Router: decodeHeaders(headers, end_stream)
    Router-->>FM: StopIteration
    Note over FM,Router: Router initiates upstream request
```

## Local Reply Handling

```mermaid
flowchart TD
    A[sendLocalReply called] --> B{Is response already started?}
    B -->|No| C[Create synthetic response headers\nwith error code]
    B -->|Yes| D[Reset stream immediately]
    C --> E[Run through encoder filter chain]
    E --> F{Encoder filter modifies?}
    F -->|Yes| G[Use modified response]
    F -->|No| H[Use original synthetic response]
    G --> I[Send to downstream codec]
    H --> I
    I --> J[Mark LocalReplyOwnerObject in FilterState]
```

## Buffer & Watermark Management

```mermaid
flowchart LR
    subgraph FilterA
        FA_buf["Filter A\nbuffered_data_"]
    end
    subgraph FilterB
        FB_buf["Filter B\nbuffered_data_"]
    end
    subgraph Connection
        DownstreamBuf["Downstream\nWrite Buffer"]
    end

    DownstreamBuf -->|high watermark| FM_wm["FilterManager\nonAboveHighWatermark()"]
    FM_wm -->|propagate| FilterA
    FM_wm -->|propagate| FilterB
    FilterA -->|onBelowLowWatermark| FM_lw["FilterManager\nonBelowLowWatermark()"]
    FM_lw -->|resume flow| Connection
```

## Key Data Structures

### `StreamDecoderFilters`

```
vector<ActiveStreamDecoderFilterPtr>
  ├── [0] ActiveStreamDecoderFilter (wraps Filter A)
  ├── [1] ActiveStreamDecoderFilter (wraps Filter B)
  └── [2] ActiveStreamDecoderFilter (wraps Router)
         ↑ iterated forward via begin()→end()
```

### `StreamEncoderFilters`

```
vector<ActiveStreamEncoderFilterPtr>
  ├── [0] ActiveStreamEncoderFilter (wraps Filter A)
  ├── [1] ActiveStreamEncoderFilter (wraps Filter B)
  └── [2] ActiveStreamEncoderFilter (wraps Filter C)
         ↑ iterated via rbegin()→rend() (reverse: C→B→A)
```

## Filter Callback Contracts

`ActiveStreamDecoderFilter` implements `StreamDecoderFilterCallbacks` and provides:

| Callback | Behavior |
|----------|----------|
| `continueDecoding()` | Resumes iteration from the current filter |
| `stopIteration()` | Halts further filters until `continueDecoding()` |
| `addDecodedData(data, streaming)` | Injects data into the decode path |
| `injectDecodedDataToFilterChain(data, end_stream)` | Re-runs data through subsequent filters |
| `sendLocalReply(code, body, ...)` | Short-circuits with a local response |
| `dispatcher()` | Returns the event dispatcher |
| `streamInfo()` | Returns mutable `StreamInfo` for this request |
| `setUpstreamOverrideHost(host)` | Forces a specific upstream host |

`ActiveStreamEncoderFilter` implements `StreamEncoderFilterCallbacks`:

| Callback | Behavior |
|----------|----------|
| `continueEncoding()` | Resumes encoder iteration |
| `addEncodedData(data, streaming)` | Injects data into the encode path |
| `onEncoderFilterAboveWriteBufferHighWatermark()` | Propagates backpressure upstream |
| `responseRouterHeaderMutation()` | Applies route-level header mutations |

## Matching Framework Integration

Each `ActiveStreamFilterBase` optionally holds a `Matcher::MatchTree` (for per-filter match configuration). When a match tree is present:

```mermaid
sequenceDiagram
    participant FM as FilterManager
    participant Wrapper as ActiveStreamFilterBase
    participant MT as MatchTree
    participant Filter as StreamDecoderFilter

    FM->>Wrapper: decodeHeaders(headers)
    Wrapper->>MT: match(request_headers_data)
    MT-->>Wrapper: MatchResult (action or no-match)
    Wrapper->>Filter: decodeHeaders(headers) with match context
    Filter-->>Wrapper: FilterHeadersStatus
```

## `DownstreamFilterManager` Extensions

`DownstreamFilterManager` adds the following behaviors on top of `FilterManager`:

| Feature | Method |
|---------|--------|
| Access logging | `finalizeRequest()` → calls all access loggers |
| Tracing | `chargeTracingStats()`, `startTracing()` |
| Stats charging | `chargeStats(response_code)` |
| Route config update | `requestRouteConfigUpdate()` via `RdsRouteConfigUpdateRequester` |
| Stream teardown | `onStreamComplete()` / `resetStream()` |

## Thread Safety

`FilterManager` is NOT thread-safe. All methods must be called from the owning worker thread's `Event::Dispatcher`. Filters that need cross-thread operations must post work back to the dispatcher.
