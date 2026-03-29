# Dynamic Modules — HTTP Filter

**Files:** `source/extensions/filters/http/dynamic_modules/filter.h`, `filter_config.h`, `factory.h`  
**Namespace:** `Envoy::Extensions::DynamicModules::HttpFilters`

## Overview

The HTTP filter is the primary extension point for dynamic modules. It implements Envoy's `Http::StreamFilter` interface and delegates all filter operations to event hooks in the loaded shared object, enabling request/response manipulation in any language that can produce a C-ABI `.so`.

## Class Hierarchy

```mermaid
classDiagram
    class StreamFilter {
        <<interface>>
        +decodeHeaders()
        +decodeData()
        +decodeTrailers()
        +encodeHeaders()
        +encodeData()
        +encodeTrailers()
        +onDestroy()
    }

    class DynamicModuleHttpFilter {
        -config_: DynamicModuleHttpFilterConfigSharedPtr
        -in_module_filter_: void*
        -decoder_callbacks_: StreamDecoderFilterCallbacks*
        -encoder_callbacks_: StreamEncoderFilterCallbacks*
        -http_callouts_: map of callout callbacks
        -http_stream_callouts_: map of stream callbacks
        +initializeInModuleFilter()
        +decodeHeaders(headers, end_stream)
        +decodeData(data, end_stream)
        +encodeHeaders(headers, end_stream)
        +sendLocalReply(code, body, ...)
        +sendHttpCallout(cluster, message, timeout)
        +startHttpStream(cluster, message, ...)
        +continueDecoding()
        +continueEncoding()
        +onScheduled(event_id)
    }

    class DynamicModuleHttpFilterConfig {
        -dynamic_module_: DynamicModulePtr
        -in_module_config_: void*
        -on_http_filter_*: function pointers
        -counters_: vector
        -gauges_: vector
        -hists_: vector
        +addCounter(counter): id
        +addGauge(gauge): id
        +addHistogram(hist): id
    }

    class DynamicModuleHttpFilterScheduler {
        -filter_: weak_ptr
        -dispatcher_: Dispatcher
        +commit(event_id)
    }

    StreamFilter <|-- DynamicModuleHttpFilter
    DynamicModuleHttpFilter *-- DynamicModuleHttpFilterConfig
    DynamicModuleHttpFilter --> DynamicModuleHttpFilterScheduler
```

## Filter Lifecycle

```mermaid
sequenceDiagram
    participant Factory as FilterFactory
    participant Config as DynamicModuleHttpFilterConfig
    participant Filter as DynamicModuleHttpFilter
    participant Module as .so Module

    Factory->>Filter: create(config, symbol_table, worker_index)
    Factory->>Filter: initializeInModuleFilter()
    Filter->>Module: on_http_filter_new(config_ptr, filter_envoy_ptr)
    Module-->>Filter: in_module_filter_ptr (module-side state)

    Note over Filter: Filter is now active for one HTTP stream

    Filter->>Module: on_http_filter_request_headers(filter_ptr, ...)
    Filter->>Module: on_http_filter_request_body(filter_ptr, ...)
    Filter->>Module: on_http_filter_response_headers(filter_ptr, ...)
    Filter->>Module: on_http_filter_stream_complete(filter_ptr)
    Filter->>Module: on_http_filter_destroy(filter_ptr)
    
    Note over Filter: Filter destroyed
```

## Config Lifecycle

```mermaid
sequenceDiagram
    participant Factory as Filter Factory
    participant Loader as DynamicModule Loader
    participant Module as .so Module
    participant Config as DynamicModuleHttpFilterConfig

    Factory->>Loader: newDynamicModuleByName("my-module")
    Loader-->>Factory: DynamicModulePtr
    
    Factory->>Config: create(filter_name, filter_config, module)
    Config->>Config: resolve all function pointers via dlsym
    Config->>Module: on_http_filter_config_new(config_ptr, name, config_data)
    Module-->>Config: in_module_config_ptr
    Config->>Config: freeze stat creation

    Note over Config: Config shared across all filter instances

    loop For each HTTP stream
        Config->>Module: on_http_filter_new(config_ptr, filter_envoy_ptr)
    end

    Note over Config: On config reload / shutdown
    Config->>Module: on_http_filter_config_destroy(config_ptr)
```

## Resolved Function Pointers

The config resolves all these ABI hooks at initialization and stores them as function pointers for zero-cost dispatch:

```mermaid
mindmap
  root((Function Pointers))
    Lifecycle
      on_http_filter_config_new
      on_http_filter_config_destroy
      on_http_filter_new
      on_http_filter_destroy
    Request Path
      on_http_filter_request_headers
      on_http_filter_request_body
      on_http_filter_request_trailers
    Response Path
      on_http_filter_response_headers
      on_http_filter_response_body
      on_http_filter_response_trailers
    Completion
      on_http_filter_stream_complete
      on_http_filter_local_reply
    Async
      on_http_filter_http_callout_done
      on_http_filter_http_stream_headers
      on_http_filter_http_stream_data
      on_http_filter_http_stream_trailers
      on_http_filter_http_stream_complete
      on_http_filter_http_stream_reset
    Flow Control
      on_http_filter_scheduled
      on_http_filter_downstream_above_write_buffer_high_watermark
      on_http_filter_downstream_below_write_buffer_low_watermark
    Per Route
      on_http_filter_per_route_config_new
      on_http_filter_per_route_config_destroy
```

## Request/Response Processing

```mermaid
flowchart TD
    subgraph RequestPath["Decode (Request) Path"]
        DH["decodeHeaders()"] -->|calls| OHRH["on_http_filter_request_headers"]
        DD["decodeData()"] -->|calls| OHRB["on_http_filter_request_body"]
        DT["decodeTrailers()"] -->|calls| OHRT["on_http_filter_request_trailers"]
    end

    subgraph ResponsePath["Encode (Response) Path"]
        EH["encodeHeaders()"] -->|calls| OHRPH["on_http_filter_response_headers"]
        ED["encodeData()"] -->|calls| OHRPB["on_http_filter_response_body"]
        ET["encodeTrailers()"] -->|calls| OHRPT["on_http_filter_response_trailers"]
    end

    subgraph ReturnStatus["Return Status Controls Flow"]
        Continue["Continue\n(proceed to next filter)"]
        Stop["StopIteration\n(pause processing)"]
        StopBuffer["StopAllAndBuffer\n(buffer all data)"]
        StopWM["StopAllAndWatermark\n(buffer with watermark)"]
    end

    OHRH --> ReturnStatus
    OHRB --> ReturnStatus
```

## HTTP Callouts (Async Sub-Requests)

```mermaid
sequenceDiagram
    participant Module as .so Module
    participant Filter as DynamicModuleHttpFilter
    participant AC as AsyncClient
    participant Upstream as Upstream Cluster

    Module->>Filter: callback: http_callout(cluster, message, timeout)
    Filter->>Filter: generate callout_id
    Filter->>AC: send(message, callbacks, timeout)
    AC->>Upstream: HTTP request

    Note over Filter: Filter processing paused

    Upstream-->>AC: HTTP response
    AC->>Filter: HttpCalloutCallback::onSuccess(response)
    Filter->>Module: on_http_filter_http_callout_done(filter_ptr, callout_id, result)
    Module->>Module: inspect response, decide action
    Module->>Filter: callback: continue_request() or send_local_reply()
```

## HTTP Streaming Callouts

For long-running or streaming upstream interactions:

```mermaid
sequenceDiagram
    participant Module as .so Module
    participant Filter as DynamicModuleHttpFilter
    participant Stream as AsyncClient::Stream
    participant Upstream as Upstream Cluster

    Module->>Filter: callback: start_http_stream(cluster, headers, timeout)
    Filter->>Stream: start stream
    Stream->>Upstream: request headers

    Module->>Filter: callback: send_stream_data(data, end_stream=false)
    Stream->>Upstream: request body chunk

    Upstream-->>Stream: response headers
    Stream->>Filter: HttpStreamCalloutCallback::onHeaders
    Filter->>Module: on_http_filter_http_stream_headers(filter_ptr, stream_id, headers)

    Upstream-->>Stream: response data
    Stream->>Filter: HttpStreamCalloutCallback::onData
    Filter->>Module: on_http_filter_http_stream_data(filter_ptr, stream_id, data)

    Upstream-->>Stream: complete
    Stream->>Filter: HttpStreamCalloutCallback::onComplete
    Filter->>Module: on_http_filter_http_stream_complete(filter_ptr, stream_id)
```

## Cross-Thread Scheduling

Dynamic modules can schedule work back to the filter's worker thread from any thread:

```mermaid
sequenceDiagram
    participant Module as Module (worker thread)
    participant Scheduler as DynamicModuleHttpFilterScheduler
    participant Dispatcher as Event::Dispatcher
    participant Filter as DynamicModuleHttpFilter (worker thread)

    Module->>Module: spawn background work (e.g. goroutine)
    
    Note over Module: Background thread completes work
    Module->>Scheduler: commit(event_id)
    Scheduler->>Dispatcher: post(callback)
    
    Note over Dispatcher: Event dispatched to correct worker thread
    Dispatcher->>Filter: onScheduled(event_id)
    Filter->>Module: on_http_filter_scheduled(filter_ptr, event_id)
```

## Metrics System

Modules define metrics at config time and record values at request time:

```mermaid
flowchart TD
    subgraph ConfigTime["Config Initialization (main thread)"]
        DefCounter["defineCounter(name) → id"]
        DefGauge["defineGauge(name, import_mode) → id"]
        DefHist["defineHistogram(name, unit) → id"]
        DefCounterVec["defineCounterVec(name, labels) → id"]
    end

    subgraph RequestTime["Request Processing (worker thread)"]
        IncCounter["incrementCounterValue(id, amount)"]
        SetGauge["setGaugeValue(id, value)"]
        RecordHist["recordHistogramValue(id, value)"]
        IncCounterVec["incrementCounterVecValue(id, label_values, amount)"]
    end

    ConfigTime -->|"stat_creation_frozen = true"| RequestTime

    Note1["Stat creation frozen after config init\nto avoid lock contention on hot path"]
```

## Per-Route Configuration

```mermaid
flowchart TD
    Route["Route with per_filter_config:\n  dynamic_modules:\n    config: '{subset: v2}'"] --> Factory["newDynamicModuleHttpPerRouteConfig()"]
    Factory --> Module["on_http_filter_per_route_config_new(name, config)"]
    Module --> PRConfig["DynamicModuleHttpPerRouteFilterConfig\n(stores module-side per-route state)"]
    
    Request["Request matching this route"] --> Filter["DynamicModuleHttpFilter"]
    Filter --> Lookup["Look up per-route config\nfrom route entry"]
    Lookup --> PRConfig
```
