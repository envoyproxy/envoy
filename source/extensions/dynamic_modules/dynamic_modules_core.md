# Dynamic Modules — Core Loading and ABI

**Files:** `dynamic_modules.h`, `dynamic_modules.cc`, `abi/abi.h`, `abi_impl.cc`  
**Namespace:** `Envoy::Extensions::DynamicModules`

## Overview

Dynamic Modules allow extending Envoy at runtime by loading shared objects (`.so` files) via `dlopen`. Instead of recompiling Envoy, developers write modules in C++, Go, or Rust using a stable C ABI, compile them as shared libraries, and configure Envoy to load them.

## Architecture

```mermaid
flowchart TD
    subgraph Envoy["Envoy Process"]
        Config["Filter Config\n(references module name)"]
        Loader["DynamicModule Loader\n(dlopen / dlsym)"]
        ABI_Impl["ABI Implementation\n(abi_impl.cc)\nCallbacks Envoy provides"]
        Filter["DynamicModuleHttpFilter\n(StreamFilter adapter)"]
    end

    subgraph SharedObject[".so Shared Object"]
        Init["envoy_dynamic_module_on_program_init()"]
        Hooks["Event Hooks\n(on_http_filter_request_headers, etc.)"]
        SDK["SDK Layer\n(C++ / Go / Rust)"]
    end

    Config -->|"name: my-module"| Loader
    Loader -->|"dlopen(libmy-module.so)"| SharedObject
    Loader -->|"dlsym()"| Init
    Filter -->|"call event hooks"| Hooks
    Hooks -->|"call callbacks"| ABI_Impl
    SDK -->|"wraps"| Hooks
```

## `DynamicModule` Class

```mermaid
classDiagram
    class DynamicModule {
        -handle_: void* (dlopen handle)
        +DynamicModule(handle)
        +~DynamicModule() dlclose
        +getFunctionPointer~T~(symbol): StatusOr~T~
        -getSymbol(symbol): void*
    }

    note for DynamicModule "Wraps a dlopen handle.\nOne instance per loaded .so file.\nDestruction calls dlclose()."
```

## Module Loading Flow

```mermaid
sequenceDiagram
    participant Config as Filter Config
    participant Loader as newDynamicModuleByName()
    participant FS as Filesystem
    participant DL as dlopen()
    participant Module as .so Module

    Config->>Loader: load "my-filter"
    Loader->>FS: check $ENVOY_DYNAMIC_MODULES_SEARCH_PATH/libmy-filter.so
    
    alt File exists in search path
        FS-->>Loader: found
    else Not found
        Loader->>DL: dlopen("libmy-filter.so") - system paths
    end

    Loader->>DL: dlopen(RTLD_NOLOAD) - check if already loaded
    
    alt Already loaded
        DL-->>Loader: existing handle
        Loader-->>Config: reuse DynamicModule
    else Not loaded
        DL-->>Loader: nullptr
        Loader->>DL: dlopen(path, RTLD_LAZY | flags)
        DL->>Module: load shared object
        DL-->>Loader: handle

        Loader->>Module: dlsym("envoy_dynamic_module_on_program_init")
        Module-->>Loader: function pointer
        Loader->>Module: call envoy_dynamic_module_on_program_init()
        Module-->>Loader: ABI version string

        alt Version matches
            Loader-->>Config: DynamicModulePtr (success)
        else Version mismatch
            Loader->>Loader: log warning (deprecated ABI)
            Loader-->>Config: DynamicModulePtr (success with warning)
        end
    end
```

## dlopen Flags

| Flag | When Used | Purpose |
|------|-----------|---------|
| `RTLD_LAZY` | Always | Resolve symbols lazily (required) |
| `RTLD_LOCAL` | Default | Symbols not shared with other modules |
| `RTLD_GLOBAL` | `load_globally=true` | Share symbols between modules |
| `RTLD_NODELETE` | `do_not_close=true` | Don't unload on dlclose (needed for Go) |
| `RTLD_NOLOAD` | Pre-check | Test if already loaded without loading |

## Module Search Path

```mermaid
flowchart TD
    Name["Module name: 'my-filter'"] --> SearchPath{"$ENVOY_DYNAMIC_MODULES_SEARCH_PATH\nset?"}
    SearchPath -->|Yes| Custom["Check: $PATH/libmy-filter.so"]
    SearchPath -->|No| Default["Check: ./libmy-filter.so"]
    Custom --> Exists1{"File exists?"}
    Default --> Exists2{"File exists?"}
    Exists1 -->|Yes| Load1["dlopen(absolute_path)"]
    Exists2 -->|Yes| Load2["dlopen(absolute_path)"]
    Exists1 -->|No| System["dlopen('libmy-filter.so')\nSearches LD_LIBRARY_PATH, /usr/lib, etc."]
    Exists2 -->|No| System
```

## ABI Overview

The ABI is defined as a pure C header (`abi/abi.h`, ~346KB) that serves as the contract between Envoy and dynamic modules. It defines two categories of functions:

```mermaid
flowchart LR
    subgraph EventHooks["Event Hooks (Module → Envoy)"]
        direction TB
        EH1["envoy_dynamic_module_on_program_init"]
        EH2["envoy_dynamic_module_on_http_filter_config_new"]
        EH3["envoy_dynamic_module_on_http_filter_request_headers"]
        EH4["envoy_dynamic_module_on_http_filter_response_body"]
        EH5["... 20+ more hooks"]
    end

    subgraph Callbacks["Callbacks (Envoy → Module)"]
        direction TB
        CB1["envoy_dynamic_module_callback_log"]
        CB2["envoy_dynamic_module_callback_http_get_request_header_value"]
        CB3["envoy_dynamic_module_callback_http_set_response_header"]
        CB4["envoy_dynamic_module_callback_http_send_response"]
        CB5["... 50+ more callbacks"]
    end

    Module[".so Module"] -->|implements| EventHooks
    Module -->|calls| Callbacks
    Envoy["Envoy Host"] -->|calls| EventHooks
    Envoy -->|implements| Callbacks
```

## ABI Naming Convention

```mermaid
mindmap
  root((ABI Naming))
    Types
      envoy_dynamic_module_type_*
      Ownership suffixes
        _module_ptr: module-owned
        _envoy_ptr: envoy-owned
        _envoy_buffer: envoy buffer
        _module_buffer: module buffer
    Event Hooks
      envoy_dynamic_module_on_*
      Module implements these
      Envoy calls these
    Callbacks
      envoy_dynamic_module_callback_*
      Envoy implements these
      Module calls these
```

## ABI Version Negotiation

```mermaid
sequenceDiagram
    participant Envoy
    participant Module as .so Module

    Envoy->>Module: envoy_dynamic_module_on_program_init()
    Module-->>Envoy: "v0.1.0" (ABI version string)
    
    Envoy->>Envoy: compare with ENVOY_DYNAMIC_MODULES_ABI_VERSION

    alt Exact match
        Note over Envoy: Log info, proceed normally
    else Mismatch
        Note over Envoy: Log warning about deprecated ABI
        Note over Envoy: Still loads - allows forward compatibility
    end
```

## Common ABI Callbacks (abi_impl.cc)

| Callback | Purpose |
|----------|---------|
| `envoy_dynamic_module_callback_log` | Write to Envoy's logger at specified level |
| `envoy_dynamic_module_callback_log_enabled` | Check if a log level is enabled |
| `envoy_dynamic_module_callback_get_concurrency` | Get number of worker threads |
| `envoy_dynamic_module_callback_register_function` | Register a function pointer by name |
| `envoy_dynamic_module_callback_get_function` | Retrieve a registered function pointer |
