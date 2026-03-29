# Dynamic Modules Overview — Part 1: Architecture and ABI

## Series Navigation

| Part | Topic |
|------|-------|
| **Part 1** | **Architecture and ABI** (this document) |
| Part 2 | [HTTP Filter and Other Extensions](./OVERVIEW_PART2_http_filter_and_extensions.md) |
| Part 3 | [SDKs and Development Guide](./OVERVIEW_PART3_sdks_and_development.md) |
| Part 4 | [Callbacks, Metrics, Advanced Topics](./OVERVIEW_PART4_callbacks_metrics_advanced.md) |

---

## What Are Dynamic Modules?

Dynamic modules are **shared objects (`.so` files) loaded at runtime** via `dlopen` to extend Envoy without recompilation. They communicate with Envoy through a stable C ABI, enabling modules written in C++, Go, Rust, or any language that can produce C-compatible exports.

```mermaid
flowchart LR
    subgraph Compile["Build Time"]
        UserCode["Your filter code\n(C++ / Go / Rust)"] --> SDK["SDK\n(wraps C ABI)"]
        SDK --> SO[".so Shared Object\n(libmy-filter.so)"]
    end

    subgraph Runtime["Envoy Runtime"]
        Config["Envoy config references\nmodule by name"] --> Load["dlopen + dlsym"]
        Load --> Module["Module integrated\ninto filter chain"]
    end

    SO -->|"deployed alongside Envoy"| Load
```

### Key Properties

| Property | Detail |
|----------|--------|
| **Trust model** | Modules run in-process with full Envoy privileges — they are trusted code |
| **Loading** | Via `dlopen` (POSIX), one `DynamicModule` instance per unique `.so` file |
| **Deduplication** | Same file (by inode) reuses existing handle via `RTLD_NOLOAD` |
| **Language support** | C++, Go, Rust via official SDKs; any C-ABI language possible |
| **Extension types** | HTTP filter, network filter, listener filter, UDP filter, access logger, bootstrap, load balancer, cert validator |

---

## High-Level Architecture

```mermaid
flowchart TD
    subgraph EnvoyHost["Envoy Host Process"]
        direction TB
        subgraph ConfigLayer["Configuration"]
            Proto["DynamicModuleFilter proto\n- module name\n- filter_name\n- filter_config"]
        end

        subgraph LoaderLayer["Module Loader"]
            Loader["newDynamicModuleByName()\n- search path resolution\n- dlopen / dlsym\n- ABI version check"]
            DM["DynamicModule\n(handle_ = dlopen result)"]
        end

        subgraph FilterLayer["Filter Integration"]
            FC["DynamicModuleHttpFilterConfig\n- resolved function pointers\n- in-module config ptr\n- metrics storage"]
            F["DynamicModuleHttpFilter\n- implements StreamFilter\n- delegates to event hooks"]
        end

        subgraph ABIImpl["ABI Implementation"]
            Callbacks["Callback implementations\n(abi_impl.cc)\n- logging, metrics, headers\n- body, metadata, filter state"]
        end

        Proto --> Loader --> DM
        DM --> FC --> F
        F <--> ABIImpl
    end

    subgraph Module[".so Module"]
        direction TB
        SDK_Layer["SDK (C++/Go/Rust)"]
        UserFilter["User Filter Logic"]
        EventHooks["Event Hook Exports\n(envoy_dynamic_module_on_*)"]

        UserFilter --> SDK_Layer --> EventHooks
    end

    F -->|"call event hooks"| EventHooks
    EventHooks -->|"call callbacks"| ABIImpl
```

---

## File Map

| File | Size | Purpose |
|------|------|---------|
| `abi/abi.h` | ~346 KB | Complete C ABI definition (types, hooks, callbacks) |
| `abi_impl.cc` | ~20 KB | Common callback implementations (logging, concurrency, function registry) |
| `dynamic_modules.h` | ~4 KB | `DynamicModule` class — wraps dlopen handle |
| `dynamic_modules.cc` | ~6 KB | Loading logic — search path, dlopen, ABI version check |
| `STYLE.md` | ~6 KB | ABI naming conventions and documentation style guide |
| `sdk/cpp/sdk.h` | ~28 KB | C++ SDK interfaces (HttpFilter, HeaderMap, BodyBuffer) |
| `sdk/cpp/sdk.cc` | ~2 KB | C++ SDK registration and destructors |
| `sdk/cpp/sdk_internal.cc` | ~39 KB | C++ SDK ABI bridge implementations |
| `sdk/cpp/sdk_fake.h` | ~3 KB | C++ test fakes |
| `sdk/go/sdk.go` | ~2 KB | Go SDK entry point |
| `sdk/go/abi/internal.go` | ~46 KB | Go CGo ABI bridge |
| `sdk/go/shared/api.go` | ~8 KB | Go SDK interfaces |
| `sdk/go/shared/base.go` | ~23 KB | Go SDK base types and implementations |
| `sdk/rust/src/lib.rs` | ~317 KB | Rust SDK (macros, traits, bindings) |
| `sdk/rust/build.rs` | ~2 KB | Rust bindgen build script |

---

## The C ABI (`abi/abi.h`)

The ABI header is the single source of truth for the contract between Envoy and modules. At ~346 KB, it is comprehensive and covers all extension types.

### Type System

```mermaid
flowchart TD
    subgraph Types["ABI Types (envoy_dynamic_module_type_*)"]
        direction TB
        Buffers["Buffer Types\n- envoy_buffer (Envoy-owned)\n- module_buffer (module-owned)\n- buffer_module_ptr\n- buffer_envoy_ptr"]
        Headers["Header Types\n- envoy_http_header\n- module_http_header\n- http_header_type (enum)"]
        Pointers["Pointer Types\n- http_filter_config_envoy_ptr\n- http_filter_config_module_ptr\n- http_filter_envoy_ptr\n- http_filter_module_ptr"]
        Enums["Enums\n- log_level\n- http_callout_init_result\n- http_callout_result\n- http_stream_reset_reason"]
    end
```

### Ownership Model

```mermaid
flowchart LR
    subgraph EnvoyOwned["Envoy Owns"]
        EP["*_envoy_ptr\n(Envoy allocates, Envoy frees)"]
        EB["*_envoy_buffer\n(Envoy manages lifetime)"]
        EH["*_envoy_http_header\n(valid during callback)"]
    end

    subgraph ModuleOwned["Module Owns"]
        MP["*_module_ptr\n(Module allocates, Module frees)"]
        MB["*_module_buffer\n(Module manages lifetime)"]
        MH["*_module_http_header\n(Module provides data)"]
    end
```

### Event Hook Categories

```mermaid
mindmap
  root((Event Hooks))
    Program
      on_program_init
    HTTP Filter
      Config
        on_http_filter_config_new
        on_http_filter_config_destroy
        on_http_filter_per_route_config_new
        on_http_filter_per_route_config_destroy
        on_http_filter_config_scheduled
      Stream
        on_http_filter_new
        on_http_filter_destroy
      Request
        on_http_filter_request_headers
        on_http_filter_request_body
        on_http_filter_request_trailers
      Response
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
    Network Filter
      on_network_filter_*
    Listener Filter
      on_listener_filter_*
    UDP Filter
      on_udp_filter_*
    Access Logger
      on_access_log_*
    Bootstrap
      on_bootstrap_*
    Load Balancer
      on_load_balancer_*
    Cert Validator
      on_cert_validator_*
```

---

## Module Loading Deep Dive

### `newDynamicModule` — The Core Loader

```mermaid
flowchart TD
    Input["path, do_not_close, load_globally"] --> PreCheck["dlopen(path, RTLD_NOLOAD | RTLD_LAZY)"]
    PreCheck --> Loaded{"Already\nloaded?"}

    Loaded -->|"Yes (handle != nullptr)"| Reuse["Return existing DynamicModule\n(skip init)"]
    Loaded -->|"No (handle == nullptr)"| BuildFlags["Build dlopen flags"]

    BuildFlags --> Flags["RTLD_LAZY\n+ (load_globally ? RTLD_GLOBAL : RTLD_LOCAL)\n+ (do_not_close ? RTLD_NODELETE : 0)"]
    Flags --> Open["dlopen(path, flags)"]
    Open --> Success{"Success?"}

    Success -->|No| Error["Return error:\ndlerror() message"]
    Success -->|Yes| Resolve["dlsym('envoy_dynamic_module_on_program_init')"]
    Resolve --> Found{"Symbol\nfound?"}

    Found -->|No| Error2["Return error:\nFailed to resolve symbol"]
    Found -->|Yes| CallInit["Call envoy_dynamic_module_on_program_init()"]
    CallInit --> CheckVersion{"ABI version\nmatches?"}

    CheckVersion -->|"Exact match"| OK["Log info, return DynamicModulePtr"]
    CheckVersion -->|"Mismatch"| Warn["Log warning (deprecated),\nreturn DynamicModulePtr"]
    CallInit -->|"Returns nullptr"| InitFail["Return error:\nFailed to initialize"]
```

### `newDynamicModuleByName` — Name Resolution

```mermaid
flowchart TD
    Name["module_name: 'my-filter'"] --> EnvVar{"$ENVOY_DYNAMIC_MODULES_SEARCH_PATH\nset?"}
    EnvVar -->|Yes| Path1["$SEARCH_PATH/libmy-filter.so"]
    EnvVar -->|No| Path2["./libmy-filter.so"]

    Path1 --> Abs1["Convert to absolute path"]
    Path2 --> Abs2["Convert to absolute path"]

    Abs1 --> Exists1{"File exists?"}
    Abs2 --> Exists2{"File exists?"}

    Exists1 -->|Yes| Load1["newDynamicModule(absolute_path)"]
    Exists2 -->|Yes| Load2["newDynamicModule(absolute_path)"]

    Exists1 -->|No| SystemLoad["dlopen('libmy-filter.so')\n(no slash → searches LD_LIBRARY_PATH, /usr/lib)"]
    Exists2 -->|No| SystemLoad

    Load1 -->|Error| Report["Return detailed error\n(missing deps, ABI mismatch)"]
    SystemLoad -->|Error| ReportAll["Return error with all tried paths"]
```

---

## ABI Version Compatibility

```mermaid
flowchart LR
    V["Current: ENVOY_DYNAMIC_MODULES_ABI_VERSION = 'v0.1.0'"]

    subgraph Guarantee["Compatibility Guarantee"]
        Exact["Exact version match:\nFull compatibility"]
        Minor["Version mismatch:\nWarning logged, module still loads"]
        None["No init function:\nError, module rejected"]
    end
```

**Why warn on mismatch instead of reject?** Forward compatibility — a module compiled against an older ABI version may still work if no removed functions are called. The warning alerts operators to recompile.

---

## All Extension Types Supported

```mermaid
flowchart TD
    DM["DynamicModule\n(.so loader)"]

    DM --> HTTP["HTTP Filter\nfilters/http/dynamic_modules/"]
    DM --> Network["Network Filter\nfilters/network/dynamic_modules/"]
    DM --> Listener["Listener Filter\nfilters/listener/dynamic_modules/"]
    DM --> UDP["UDP Filter\nfilters/udp/dynamic_modules/"]
    DM --> AccessLog["Access Logger\naccess_loggers/dynamic_modules/"]
    DM --> Bootstrap["Bootstrap Extension\nbootstrap/dynamic_modules/"]
    DM --> LB["Load Balancer\nload_balancing_policies/dynamic_modules/"]
    DM --> CertVal["Cert Validator\ntransport_sockets/tls/cert_validator/dynamic_modules/"]
```

Each extension type follows the same pattern:
1. Proto config references a dynamic module by name
2. Factory loads the module via `newDynamicModuleByName`
3. Config class resolves extension-specific event hooks
4. Per-stream/connection instance delegates to the module
