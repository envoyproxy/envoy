# Dynamic Modules Overview — Part 4: Callbacks, Metrics, and Advanced Topics

## Series Navigation

| Part | Topic |
|------|-------|
| Part 1 | [Architecture and ABI](./OVERVIEW_PART1_architecture_and_abi.md) |
| Part 2 | [HTTP Filter and Other Extensions](./OVERVIEW_PART2_http_filter_and_extensions.md) |
| Part 3 | [SDKs and Development Guide](./OVERVIEW_PART3_sdks_and_development.md) |
| **Part 4** | **Callbacks, Metrics, and Advanced Topics** (this document) |

---

## Complete Callback Reference

### Common Callbacks (Available to All Extensions)

```mermaid
mindmap
  root((Common Callbacks))
    Logging
      callback_log(level, message)
      callback_log_enabled(level): bool
    Concurrency
      callback_get_concurrency(): uint32
    Function Registry
      callback_register_function(name, fn_ptr)
      callback_get_function(name): fn_ptr
```

### HTTP Filter Callbacks — Header Manipulation

```mermaid
flowchart TD
    subgraph HeaderCallbacks["Header Callbacks"]
        Get["get_request_header_value\nget_response_header_value\nget_request_trailer_value\nget_response_trailer_value"]
        Set["set_request_header\nset_response_header\nset_request_trailer\nset_response_trailer"]
        Add["add_request_header\nadd_response_header"]
        Remove["remove_request_header\nremove_response_header\nremove_request_trailer\nremove_response_trailer"]
        ForEach["for_each_request_header\nfor_each_response_header\nfor_each_request_trailer\nfor_each_response_trailer"]
        Count["get_request_headers_count\nget_response_headers_count"]
    end
```

### HTTP Filter Callbacks — Body Manipulation

```mermaid
flowchart TD
    subgraph BodyCallbacks["Body Callbacks"]
        GetBuf["get_request_body_buffer\nget_response_body_buffer"]
        Append["append_body_buffer"]
        Prepend["prepend_body_buffer"]
        Drain["drain_body_buffer"]
        Replace["replace_body_buffer"]
        CopyOut["copy_out_body_buffer"]
        Length["get_body_buffer_length"]
    end
```

### HTTP Filter Callbacks — Flow Control

```mermaid
sequenceDiagram
    participant Module as .so Module
    participant Filter as DynamicModuleHttpFilter
    participant HCM as HttpConnectionManager

    Note over Module: Module wants to pause processing
    Module-->>Filter: return StopIteration from onRequestHeaders

    Note over Module: Later, async work completes
    Module->>Filter: callback: continue_request()
    Filter->>HCM: continueDecoding()
    Note over HCM: Resume filter chain iteration
```

### HTTP Filter Callbacks — Metadata and Filter State

```mermaid
flowchart TD
    subgraph Metadata["Dynamic Metadata"]
        GetMeta["get_metadata_value(namespace, key)"]
        SetMetaStr["set_metadata_string_value(namespace, key, value)"]
        SetMetaNum["set_metadata_number_value(namespace, key, value)"]
    end

    subgraph FilterState["Filter State"]
        GetFS["get_filter_state(key)"]
        SetFS["set_filter_state(key, value, span, stream_sharing)"]
    end

    subgraph Attributes["Stream Attributes"]
        GetAttrStr["get_attribute_string(id)\npath, method, scheme, authority,\nprotocol, upstream_host, etc."]
        GetAttrNum["get_attribute_number(id)\nresponse_code, response_flags, etc."]
    end
```

### HTTP Filter Callbacks — Async Operations

```mermaid
flowchart TD
    subgraph SimpleCallout["HTTP Callout"]
        SC["http_callout(cluster, request, timeout)"]
        SCR["→ on_http_filter_http_callout_done(id, result)"]
    end

    subgraph StreamCallout["HTTP Stream"]
        SST["start_http_stream(cluster, headers, timeout)"]
        SSD["send_stream_data(stream_id, data, end_stream)"]
        SST2["send_stream_trailers(stream_id, trailers)"]
        SSR["reset_http_stream(stream_id)"]
        SRH["→ on_http_filter_http_stream_headers"]
        SRD["→ on_http_filter_http_stream_data"]
        SRT["→ on_http_filter_http_stream_trailers"]
        SRC["→ on_http_filter_http_stream_complete"]
        SRST["→ on_http_filter_http_stream_reset"]
    end

    SC --> SCR
    SST --> SSD --> SST2
    SST --> SRH --> SRD --> SRT --> SRC
    SSR --> SRST
```

---

## Metrics System

### Metric Definition (Config Time)

Metrics must be defined during `on_http_filter_config_new`. After config initialization, stat creation is frozen to avoid lock contention on the hot path.

```mermaid
sequenceDiagram
    participant Module as .so Module
    participant Config as DynamicModuleHttpFilterConfig
    participant Stats as Envoy Stats System

    Note over Config: stat_creation_frozen_ = false

    Module->>Config: callback: define_counter("my_counter")
    Config->>Stats: counterFromStatName(namespace.my_counter)
    Config->>Config: store ModuleCounterHandle, return id=0

    Module->>Config: callback: define_gauge("my_gauge", Accumulate)
    Config->>Stats: gaugeFromStatName(namespace.my_gauge)
    Config->>Config: store ModuleGaugeHandle, return id=0

    Module->>Config: callback: define_histogram("latency", Milliseconds)
    Config->>Stats: histogramFromStatName(namespace.latency)
    Config->>Config: store ModuleHistogramHandle, return id=0

    Note over Config: stat_creation_frozen_ = true
    Note over Config: No more define_* calls allowed
```

### Metric Recording (Request Time)

```mermaid
sequenceDiagram
    participant Module as .so Module (worker thread)
    participant Filter as DynamicModuleHttpFilter
    participant Config as DynamicModuleHttpFilterConfig
    participant Stats as Envoy Counter/Gauge/Histogram

    Module->>Filter: callback: increment_counter(id=0, amount=1)
    Filter->>Config: getCounterById(0)
    Config-->>Filter: ModuleCounterHandle
    Filter->>Stats: counter.add(1)

    Module->>Filter: callback: set_gauge(id=0, value=42)
    Filter->>Config: getGaugeById(0)
    Config-->>Filter: ModuleGaugeHandle
    Filter->>Stats: gauge.set(42)

    Module->>Filter: callback: record_histogram(id=0, value=150)
    Filter->>Config: getHistogramById(0)
    Config-->>Filter: ModuleHistogramHandle
    Filter->>Stats: histogram.recordValue(150)
```

### Metric Vec (Labeled Metrics)

For metrics with dynamic labels (e.g., per-status-code counters):

```mermaid
flowchart TD
    Define["define_counter_vec('requests', ['method', 'status'])"] --> ID["Returns vec_id=0"]
    ID --> Record["increment_counter_vec(vec_id=0, labels=['GET', '200'], amount=1)"]
    Record --> Stats["dynamicmodulescustom.requests{method=GET, status=200} += 1"]
```

### Metric Types

| Type | Operations | Use Case |
|------|-----------|----------|
| **Counter** | `add(amount)` | Monotonically increasing (requests, errors) |
| **CounterVec** | `add(labels, amount)` | Labeled counter (per method/status) |
| **Gauge** | `set(value)`, `increase(amount)`, `decrease(amount)` | Current value (connections, queue depth) |
| **GaugeVec** | `set/increase/decrease(labels, value)` | Labeled gauge |
| **Histogram** | `recordValue(value)` | Distributions (latency, size) |
| **HistogramVec** | `recordValue(labels, value)` | Labeled histogram |

---

## Cross-Thread Scheduling

### Worker Thread Scheduling

```mermaid
flowchart TD
    subgraph WorkerThread["Worker Thread (filter's thread)"]
        Filter["DynamicModuleHttpFilter"]
        Dispatcher["Event::Dispatcher"]
    end

    subgraph OtherThread["Background Thread (goroutine, async task)"]
        AsyncWork["Async computation"]
        Scheduler["DynamicModuleHttpFilterScheduler"]
    end

    AsyncWork --> Scheduler
    Scheduler -->|"post(event_id)"| Dispatcher
    Dispatcher -->|"onScheduled(event_id)"| Filter
    Filter -->|"on_http_filter_scheduled(event_id)"| Module["Module resumes\nprocessing"]
```

### Config-Level Scheduling

Similar to filter-level, but for operations that need the main thread:

```mermaid
flowchart TD
    subgraph MainThread["Main Thread"]
        Config["DynamicModuleHttpFilterConfig"]
        MainDispatcher["Main Dispatcher"]
    end

    subgraph WorkerThread["Any Thread"]
        ConfigScheduler["DynamicModuleHttpFilterConfigScheduler"]
    end

    ConfigScheduler -->|"post(event_id)"| MainDispatcher
    MainDispatcher -->|"onScheduled(event_id)"| Config
    Config -->|"on_http_filter_config_scheduled(event_id)"| Module["Module callback\non main thread"]
```

### Scheduler Lifecycle

```mermaid
sequenceDiagram
    participant Module as .so Module
    participant Filter as DynamicModuleHttpFilter
    participant Scheduler as DynamicModuleHttpFilterScheduler

    Module->>Filter: callback: scheduler_new()
    Filter->>Scheduler: create(weak_ptr(filter), dispatcher)
    Filter-->>Module: scheduler_ptr

    Note over Module: Background work...
    Module->>Scheduler: commit(event_id=42)
    Scheduler->>Scheduler: dispatcher_.post(callback)

    Note over Filter: On worker thread event loop
    Filter->>Filter: onScheduled(42)
    Filter->>Module: on_http_filter_scheduled(filter_ptr, 42)

    Note over Module: Done with scheduler
    Module->>Filter: callback: scheduler_delete(scheduler_ptr)
```

**Safety:** The scheduler holds a `weak_ptr` to the filter. If the filter is destroyed before the scheduled event fires, the `weak_ptr::lock()` returns null and the event is silently dropped.

---

## Socket Options

Modules can store and retrieve socket options per stream:

```mermaid
flowchart TD
    Module["Module sets socket option"] --> Store["storeSocketOptionInt/Bytes\n(level, name, state, direction, value)"]
    Store --> Storage["StoredSocketOption\nin DynamicModuleHttpFilter"]

    Later["Module retrieves option"] --> Get["tryGetSocketOptionInt/Bytes\n(level, name, state, direction)"]
    Get --> Storage
```

---

## Function Registry

Modules can register and look up function pointers by name, enabling cross-module communication:

```mermaid
sequenceDiagram
    participant ModA as Module A (.so)
    participant Registry as Function Registry (Envoy)
    participant ModB as Module B (.so)

    ModA->>Registry: register_function("auth_check", fn_ptr)
    Registry->>Registry: store name → pointer

    ModB->>Registry: get_function("auth_check")
    Registry-->>ModB: fn_ptr
    ModB->>ModB: call fn_ptr(args)
```

---

## Per-Route Configuration

```mermaid
flowchart TD
    subgraph RouteConfig["Route-Level Config"]
        Route["Route entry with\nper_filter_config:\n  dynamic_modules:\n    name: subset-filter\n    config: '{subset: v2}'"]
    end

    subgraph Creation["Config Creation"]
        Factory["newDynamicModuleHttpPerRouteConfig()"]
        Module["on_http_filter_per_route_config_new\n(name, config) → module_ptr"]
    end

    subgraph Usage["Request Processing"]
        Filter["DynamicModuleHttpFilter"]
        Lookup["Look up per-route config\nfrom matched route"]
        Access["Pass per-route module_ptr\nto event hooks"]
    end

    Route --> Factory --> Module
    Filter --> Lookup --> Access
```

---

## Error Handling

```mermaid
flowchart TD
    subgraph LoadErrors["Module Load Errors"]
        E1["File not found\n→ InvalidArgumentError"]
        E2["Missing symbol\n→ InvalidArgumentError"]
        E3["Init returns nullptr\n→ InvalidArgumentError"]
        E4["dlopen failure\n→ InvalidArgumentError + dlerror()"]
    end

    subgraph RuntimeErrors["Runtime Error Handling"]
        E5["Module event hook returns error status\n→ filter handles gracefully"]
        E6["Module calls invalid callback\n→ ENVOY_BUG assertion"]
        E7["Filter destroyed during callout\n→ callout cancelled automatically"]
    end
```

---

## Comparison with Other Extension Mechanisms

```mermaid
flowchart TD
    subgraph DynMod["Dynamic Modules"]
        DM1["In-process (.so)"]
        DM2["C ABI contract"]
        DM3["C++/Go/Rust SDKs"]
        DM4["Full Envoy API access"]
        DM5["Near-native performance"]
    end

    subgraph WASM["Wasm Extensions"]
        W1["Sandboxed (Wasm VM)"]
        W2["proxy-wasm ABI"]
        W3["Any Wasm language"]
        W4["Limited API surface"]
        W5["VM overhead"]
    end

    subgraph Lua["Lua Scripts"]
        L1["Interpreted in-process"]
        L2["Lua C API"]
        L3["Lua only"]
        L4["Limited API"]
        L5["Interpreter overhead"]
    end

    subgraph ExtProc["External Processing"]
        EP1["Out-of-process (gRPC)"]
        EP2["gRPC protocol"]
        EP3["Any language"]
        EP4["Network latency"]
        EP5["Process isolation"]
    end
```

| Feature | Dynamic Modules | Wasm | Lua | Ext Processing |
|---------|----------------|------|-----|----------------|
| **Isolation** | None (in-process) | Sandboxed | None | Full (separate process) |
| **Performance** | Native | ~2-10x overhead | ~5-20x overhead | Network RTT |
| **Language** | C++, Go, Rust | Any → Wasm | Lua | Any |
| **API surface** | Full Envoy | Limited proxy-wasm | Limited | gRPC protocol |
| **Trust** | Full trust required | Untrusted OK | Semi-trusted | Untrusted OK |
| **Deployment** | .so alongside Envoy | .wasm file | Inline/file | Separate service |
| **Hot reload** | New config load | Yes | Yes | Service restart |

---

## Troubleshooting

### Module Won't Load

```mermaid
flowchart TD
    Error["Module load error"] --> Check1{"File exists in\nsearch path?"}
    Check1 -->|No| Fix1["Set ENVOY_DYNAMIC_MODULES_SEARCH_PATH\nor place .so in working directory"]
    Check1 -->|Yes| Check2{"Dependencies\nresolved?"}
    Check2 -->|No| Fix2["Run: ldd libmy-filter.so\nInstall missing libraries"]
    Check2 -->|Yes| Check3{"envoy_dynamic_module_on_program_init\nexported?"}
    Check3 -->|No| Fix3["Ensure SDK registration macro\nis included in your code"]
    Check3 -->|Yes| Check4{"ABI version\nmatches?"}
    Check4 -->|No| Fix4["Recompile module against\ncurrent Envoy SDK version"]
    Check4 -->|Yes| Fix5["Check Envoy logs for\ndetailed error message"]
```

### Go Module Crashes on Unload

```
Set do_not_close: true in DynamicModuleConfig
Go runtime does not support dlclose
```

### Module Symbol Conflicts

```
Set load_globally: false (default)
Each module gets its own symbol namespace via RTLD_LOCAL
Only use RTLD_GLOBAL if modules need to share symbols
```
