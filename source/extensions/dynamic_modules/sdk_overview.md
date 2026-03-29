# Dynamic Modules — SDK Overview (C++, Go, Rust)

**Directories:** `sdk/cpp/`, `sdk/go/`, `sdk/rust/`

## Overview

Dynamic modules can be written in any language that can produce a shared object with C-ABI exports. The project provides first-class SDKs for **C++**, **Go**, and **Rust** that wrap the raw ABI into idiomatic, type-safe APIs.

## SDK Architecture

```mermaid
flowchart TD
    subgraph UserCode["User Module Code"]
        CPP_User["C++ Module\n(implements HttpFilter)"]
        Go_User["Go Module\n(implements HttpFilter)"]
        Rust_User["Rust Module\n(implements HttpFilter)"]
    end

    subgraph SDKLayer["SDK Layer (per language)"]
        CPP_SDK["C++ SDK\nsdk.h / sdk_internal.cc"]
        Go_SDK["Go SDK\nsdk.go / abi/internal.go"]
        Rust_SDK["Rust SDK\nlib.rs"]
    end

    subgraph ABI["C ABI (abi.h)"]
        Hooks["Event Hooks\n(envoy_dynamic_module_on_*)"]
        Callbacks["Callbacks\n(envoy_dynamic_module_callback_*)"]
    end

    subgraph Envoy["Envoy Host"]
        Filter["DynamicModuleHttpFilter"]
        ABI_Impl["abi_impl.cc"]
    end

    CPP_User --> CPP_SDK
    Go_User --> Go_SDK
    Rust_User --> Rust_SDK

    CPP_SDK --> ABI
    Go_SDK --> ABI
    Rust_SDK --> ABI

    ABI --> Envoy
```

## C++ SDK

### Key Interfaces

```mermaid
classDiagram
    class HttpFilter {
        <<interface>>
        +onRequestHeaders(headers, end_stream): HeadersStatus
        +onRequestBody(body, end_stream): BodyStatus
        +onRequestTrailers(trailers): TrailersStatus
        +onResponseHeaders(headers, end_stream): HeadersStatus
        +onResponseBody(body, end_stream): BodyStatus
        +onResponseTrailers(trailers): TrailersStatus
        +onStreamComplete()
        +onDestroy()
    }

    class HttpFilterFactory {
        <<interface>>
        +newFilter(handle): HttpFilterPtr
    }

    class HttpFilterConfigFactory {
        <<interface>>
        +name(): string
        +newFilterFactory(config, handle): HttpFilterFactoryPtr
    }

    class HttpFilterHandle {
        +requestHeaders(): HeaderMap
        +requestBody(): BodyBuffer
        +responseHeaders(): HeaderMap
        +responseBody(): BodyBuffer
        +continueRequest()
        +continueResponse()
        +sendLocalReply(code, body)
        +httpCallout(cluster, request, timeout)
        +log(level, message)
    }

    class HeaderMap {
        +get(key): optional~string_view~
        +set(key, value)
        +add(key, value)
        +remove(key)
        +forEach(callback)
    }

    class BodyBuffer {
        +length(): size_t
        +copyOut(start, length, dest): size_t
        +drain(length)
        +prepend(data)
        +append(data)
        +replaceWithString(str)
    }

    HttpFilter --> HttpFilterHandle
    HttpFilterFactory --> HttpFilter
    HttpFilterConfigFactory --> HttpFilterFactory
```

### C++ Registration

```cpp
class MyFilterConfigFactory : public HttpFilterConfigFactory {
public:
    std::string name() override { return "my-filter"; }
    
    HttpFilterFactoryPtr newFilterFactory(
        std::string_view config,
        HttpFilterConfigHandle& handle) override {
        return std::make_unique<MyFilterFactory>(config);
    }
};

// Register at module load time
REGISTER_HTTP_FILTER_CONFIG_FACTORY(MyFilterConfigFactory);
```

### C++ SDK Bridge

```mermaid
flowchart TD
    subgraph SDKInternal["sdk_internal.cc"]
        Bridge["ABI Event Hooks\n(extern 'C' functions)"]
        Registry["HttpFilterConfigFactoryRegistry\n(maps names to factories)"]
        Impls["BodyBufferImpl, HeaderMapImpl,\nHttpFilterHandleImpl"]
    end

    Bridge -->|"on_program_init"| Registry
    Bridge -->|"on_http_filter_config_new"| Registry
    Registry -->|"lookup factory by name"| Factory["User's HttpFilterConfigFactory"]
    Factory -->|"creates"| FF["HttpFilterFactory"]
    FF -->|"creates"| Filter["User's HttpFilter"]
    
    Filter -->|"uses"| Impls
    Impls -->|"calls"| Callbacks["ABI Callbacks\n(envoy_dynamic_module_callback_*)"]
```

## Go SDK

### Key Interfaces

```mermaid
classDiagram
    class HttpFilter {
        <<interface>>
        +OnRequestHeaders(headers HeaderMap, endStream bool) HeadersStatus
        +OnRequestBody(body BodyBuffer, endStream bool) BodyStatus
        +OnRequestTrailers(trailers HeaderMap) TrailersStatus
        +OnResponseHeaders(headers HeaderMap, endStream bool) HeadersStatus
        +OnResponseBody(body BodyBuffer, endStream bool) BodyStatus
        +OnResponseTrailers(trailers HeaderMap) TrailersStatus
        +OnStreamComplete()
        +OnDestroy()
    }

    class HttpFilterFactory {
        <<interface>>
        +NewFilter(handle HttpFilterHandle) HttpFilter
    }

    class HttpFilterConfigFactory {
        <<interface>>
        +Name() string
        +NewFilterFactory(config string, handle HttpFilterConfigHandle) HttpFilterFactory
    }

    class EmptyHttpFilter {
        Default no-op implementation
        Embed in your filter to only
        override methods you need
    }

    HttpFilter <|-- EmptyHttpFilter
```

### Go SDK Architecture

```mermaid
flowchart TD
    subgraph GoUser["User Go Code"]
        GF["MyFilter\n(embeds EmptyHttpFilter)"]
        GFF["MyFilterFactory"]
        GFCF["MyFilterConfigFactory"]
    end

    subgraph GoSDK["Go SDK Layer"]
        SDK_GO["sdk.go\nRegisterHttpFilterConfigFactories()"]
        Shared["shared/\napi.go, base.go"]
    end

    subgraph CGo["CGo Bridge (abi/internal.go)"]
        Managers["configManager\npluginManager\n(id-based lookup)"]
        Exports["//export functions\n(C ABI event hooks)"]
        Imports["// #cgo LDFLAGS\n(C ABI callbacks)"]
    end

    GF --> Shared
    GFF --> Shared
    GFCF --> SDK_GO
    SDK_GO --> CGo
    CGo --> ABI["C ABI (abi.h)"]
```

### Go-Specific Considerations

| Concern | Solution |
|---------|----------|
| Go GC moves memory | CGo bridge copies data at boundary |
| `dlclose` not supported | Config: `do_not_close: true` (RTLD_NODELETE) |
| Goroutines for async | `DynamicModuleHttpFilterScheduler` to post back to worker thread |
| Multiple instances | ID-based managers map C pointers to Go objects |

## Rust SDK

### Architecture

```mermaid
flowchart TD
    subgraph RustUser["User Rust Code"]
        RF["MyFilter\n(impl HttpFilter)"]
        RFF["MyFilterFactory"]
    end

    subgraph RustSDK["Rust SDK (lib.rs)"]
        Macros["declare_init_functions!\nregister_http_filter_config_factory!"]
        Traits["HttpFilter, HttpFilterFactory,\nHttpFilterConfigFactory traits"]
        Bindings["Auto-generated bindings\n(build.rs + bindgen)"]
    end

    subgraph Build["Build System"]
        BuildRS["build.rs\n→ bindgen from abi.h\n→ generates abi_bindings.rs"]
    end

    RustUser --> RustSDK
    RustSDK --> Bindings
    Build --> Bindings
    Bindings --> ABI["C ABI (abi.h)"]
```

### Rust Build Process

```mermaid
sequenceDiagram
    participant Cargo as cargo build
    participant BuildRS as build.rs
    participant Bindgen as bindgen
    participant ABI as abi/abi.h

    Cargo->>BuildRS: execute build script
    BuildRS->>Bindgen: generate bindings from abi.h
    Bindgen->>ABI: parse C header
    ABI-->>Bindgen: types, functions
    Bindgen-->>BuildRS: abi_bindings.rs
    BuildRS-->>Cargo: OUT_DIR/abi_bindings.rs

    Cargo->>Cargo: compile lib.rs (includes bindings)
    Cargo->>Cargo: produce libmy_filter.so (cdylib)
```

### Rust SDK Features

| Feature | Module |
|---------|--------|
| HTTP Filter | `lib.rs` (traits, macros) |
| Access Logger | `access_log.rs` |
| Cert Validator | `cert_validator.rs` |
| Buffer operations | `buffer.rs` |
| Logging | `log!`, `warn!`, `error!` macros |
| Function registry | `register_function`, `get_function` |

## SDK Comparison

| Feature | C++ SDK | Go SDK | Rust SDK |
|---------|---------|--------|----------|
| **Registration** | `REGISTER_HTTP_FILTER_CONFIG_FACTORY` | `RegisterHttpFilterConfigFactories()` | `declare_init_functions!` |
| **Memory** | Manual / RAII | GC (via CGo bridge) | Ownership system |
| **Async** | `HttpFilterHandle` methods | Goroutines + scheduler | `async` (via scheduler) |
| **Testing** | `sdk_fake.h` (FakeHeaderMap, etc.) | `shared/mocks/` (mockgen) | `lib_test.rs` |
| **dlclose** | Supported | Not supported (RTLD_NODELETE) | Supported |
| **Build output** | `.so` (shared library) | `.so` (c-shared) | `.so` (cdylib) |
| **Overhead** | Minimal (direct C calls) | CGo crossing cost | Minimal (zero-cost FFI) |

## Testing Support

```mermaid
flowchart TD
    subgraph CPP_Test["C++ Testing"]
        Fake["sdk_fake.h\nFakeHeaderMap\nFakeBodyBuffer"]
    end

    subgraph Go_Test["Go Testing"]
        Mock["shared/mocks/\nmock_api.go\nmock_base.go"]
        FakeStream["shared/fake/\nfake_stream_base.go"]
    end

    subgraph Rust_Test["Rust Testing"]
        LibTest["lib_test.rs\n(unit tests with mock ABI)"]
    end

    subgraph Integration["Integration Tests"]
        IT["test/extensions/dynamic_modules/http/integration_test.cc\n(loads real .so modules)"]
        TestData["test/extensions/dynamic_modules/test_data/\n(C, Go, Rust test modules)"]
    end
```
