# Dynamic Modules Overview — Part 3: SDKs and Development Guide

## Series Navigation

| Part | Topic |
|------|-------|
| Part 1 | [Architecture and ABI](./OVERVIEW_PART1_architecture_and_abi.md) |
| Part 2 | [HTTP Filter and Other Extensions](./OVERVIEW_PART2_http_filter_and_extensions.md) |
| **Part 3** | **SDKs and Development Guide** (this document) |
| Part 4 | [Callbacks, Metrics, Advanced Topics](./OVERVIEW_PART4_callbacks_metrics_advanced.md) |

---

## SDK Stack

```mermaid
flowchart TD
    subgraph Layer1["User Code"]
        User["MyFilter\n(business logic)"]
    end

    subgraph Layer2["SDK (per language)"]
        CPP["C++ SDK\nsdk.h"]
        Go["Go SDK\nshared/api.go"]
        Rust["Rust SDK\nlib.rs"]
    end

    subgraph Layer3["ABI Bridge"]
        CPP_Bridge["sdk_internal.cc\n(maps C++ objects to C ABI)"]
        Go_Bridge["abi/internal.go\n(CGo exports/imports)"]
        Rust_Bridge["build.rs\n(bindgen → abi_bindings.rs)"]
    end

    subgraph Layer4["C ABI"]
        ABI["abi/abi.h\n(pure C header)"]
    end

    User --> CPP & Go & Rust
    CPP --> CPP_Bridge --> ABI
    Go --> Go_Bridge --> ABI
    Rust --> Rust_Bridge --> ABI
```

---

## C++ SDK — Developing a Filter

### Step 1: Implement the Interfaces

```mermaid
flowchart TD
    HFCF["HttpFilterConfigFactory\n(creates factories per config)"] --> HFF["HttpFilterFactory\n(creates filters per stream)"]
    HFF --> HF["HttpFilter\n(handles one HTTP stream)"]
```

### Step 2: Use the Handle API

```mermaid
classDiagram
    class HttpFilterHandle {
        Request Access
        +requestHeaders(): HeaderMap
        +requestBody(): BodyBuffer
        +requestTrailers(): HeaderMap
        Response Access
        +responseHeaders(): HeaderMap
        +responseBody(): BodyBuffer
        +responseTrailers(): HeaderMap
        Flow Control
        +continueRequest()
        +continueResponse()
        +sendLocalReply(code, body)
        +clearRouteCache()
        Async
        +httpCallout(cluster, request, timeout)
        +startHttpStream(cluster, headers, ...)
        Attributes
        +getAttributeString(id): optional string
        +getAttributeNumber(id): optional int64
        Metadata
        +getMetadataValue(namespace, key): optional string
        +setMetadataStringValue(namespace, key, value)
        Logging
        +log(level, message)
    }

    class HeaderMap {
        +get(key): optional string_view
        +set(key, value)
        +add(key, value)
        +remove(key)
        +forEach(callback)
        +size(): size_t
    }

    class BodyBuffer {
        +length(): size_t
        +copyOut(start, len, dest): size_t
        +drain(length)
        +prepend(data)
        +append(data)
        +replaceWithString(str)
    }
```

### Step 3: Register

```cpp
REGISTER_HTTP_FILTER_CONFIG_FACTORY(MyFilterConfigFactory);
```

### C++ Build Output

Compile to a shared library:
```
g++ -shared -fPIC -o libmy-filter.so my_filter.cc -lenvoy_dynamic_modules_sdk
```

### C++ SDK Internals

```mermaid
sequenceDiagram
    participant Envoy as Envoy (C ABI)
    participant Internal as sdk_internal.cc
    participant Registry as HttpFilterConfigFactoryRegistry
    participant User as User's Filter

    Envoy->>Internal: envoy_dynamic_module_on_program_init()
    Internal-->>Envoy: ABI version string

    Envoy->>Internal: envoy_dynamic_module_on_http_filter_config_new(name, config)
    Internal->>Registry: getFactory(name)
    Registry-->>Internal: HttpFilterConfigFactory*
    Internal->>User: factory.newFilterFactory(config, handle)
    User-->>Internal: HttpFilterFactoryPtr
    Internal-->>Envoy: config_module_ptr

    Envoy->>Internal: envoy_dynamic_module_on_http_filter_new(config_ptr, filter_envoy_ptr)
    Internal->>User: factory.newFilter(handle)
    User-->>Internal: HttpFilterPtr
    Internal-->>Envoy: filter_module_ptr

    Envoy->>Internal: envoy_dynamic_module_on_http_filter_request_headers(filter_ptr, ...)
    Internal->>User: filter.onRequestHeaders(headers, end_stream)
    User-->>Internal: HeadersStatus
    Internal-->>Envoy: status enum
```

---

## Go SDK — Developing a Filter

### Key Interfaces

```go
type HttpFilterConfigFactory interface {
    Name() string
    NewFilterFactory(config string, handle HttpFilterConfigHandle) HttpFilterFactory
}

type HttpFilterFactory interface {
    NewFilter(handle HttpFilterHandle) HttpFilter
}

type HttpFilter interface {
    OnRequestHeaders(headers HeaderMap, endStream bool) HeadersStatus
    OnRequestBody(body BodyBuffer, endStream bool) BodyStatus
    OnRequestTrailers(trailers HeaderMap) TrailersStatus
    OnResponseHeaders(headers HeaderMap, endStream bool) HeadersStatus
    OnResponseBody(body BodyBuffer, endStream bool) BodyStatus
    OnResponseTrailers(trailers HeaderMap) TrailersStatus
    OnStreamComplete()
    OnDestroy()
}
```

### EmptyHttpFilter — Default No-Op

```mermaid
classDiagram
    class EmptyHttpFilter {
        +OnRequestHeaders(): Continue
        +OnRequestBody(): Continue
        +OnRequestTrailers(): Continue
        +OnResponseHeaders(): Continue
        +OnResponseBody(): Continue
        +OnResponseTrailers(): Continue
        +OnStreamComplete(): no-op
        +OnDestroy(): no-op
    }

    class MyFilter {
        Embed EmptyHttpFilter
        Override only what you need
        +OnRequestHeaders(): custom logic
    }

    EmptyHttpFilter <|-- MyFilter
```

### Go Registration

```go
func init() {
    sdk.RegisterHttpFilterConfigFactories(&MyFilterConfigFactory{})
}
```

### Go CGo Bridge Architecture

```mermaid
flowchart TD
    subgraph GoSide["Go Runtime"]
        GoFilter["Go HttpFilter instance"]
        GoManager["pluginManager\n(id → Go object)"]
        ConfigManager["configManager\n(id → Go config)"]
    end

    subgraph CGo["CGo Bridge"]
        Export["//export envoy_dynamic_module_on_http_filter_request_headers"]
        Import["C.envoy_dynamic_module_callback_http_get_request_header_value"]
    end

    subgraph CSide["C ABI"]
        Hooks["Event Hooks"]
        Callbacks["Callbacks"]
    end

    GoFilter --> GoManager
    GoManager --> Export
    Export --> Hooks
    GoFilter --> Import
    Import --> Callbacks
```

### Go-Specific: ID-Based Object Management

Go's garbage collector can move objects, so pointers can't cross the CGo boundary. The SDK uses ID-based maps:

```mermaid
sequenceDiagram
    participant Envoy as Envoy
    participant CGo as CGo Bridge
    participant Manager as pluginManager
    participant Go as Go Filter

    Envoy->>CGo: on_http_filter_new(config_ptr, filter_envoy_ptr)
    CGo->>Manager: allocate new ID
    CGo->>Go: factory.NewFilter(handle)
    Go-->>CGo: HttpFilter instance
    CGo->>Manager: store(id, instance)
    CGo-->>Envoy: id (as module_ptr)

    Envoy->>CGo: on_http_filter_request_headers(module_ptr=id, ...)
    CGo->>Manager: get(id)
    Manager-->>CGo: HttpFilter instance
    CGo->>Go: filter.OnRequestHeaders(...)
    Go-->>CGo: HeadersStatus
    CGo-->>Envoy: status
```

### Go Build

```bash
CGO_ENABLED=1 go build -buildmode=c-shared -o libmy-filter.so ./...
```

**Important:** Set `do_not_close: true` in the Envoy config because Go does not support `dlclose`.

---

## Rust SDK — Developing a Filter

### Rust Traits

```rust
pub trait HttpFilterConfigFactory: Send + Sync {
    fn name(&self) -> &str;
    fn new_filter_factory(
        &self,
        config: &str,
        handle: HttpFilterConfigHandle,
    ) -> Box<dyn HttpFilterFactory>;
}

pub trait HttpFilterFactory: Send + Sync {
    fn new_filter(&self, handle: HttpFilterHandle) -> Box<dyn HttpFilter>;
}

pub trait HttpFilter {
    fn on_request_headers(&mut self, headers: &HeaderMap, end_stream: bool) -> HeadersStatus;
    fn on_request_body(&mut self, body: &BodyBuffer, end_stream: bool) -> BodyStatus;
    // ... etc
}
```

### Rust Registration Macro

```rust
declare_init_functions!(MyFilterConfigFactory);
```

This macro generates the `envoy_dynamic_module_on_program_init` export and the registration glue.

### Rust Build System

```mermaid
flowchart TD
    subgraph Build["cargo build"]
        ABI_H["abi/abi.h"] --> BuildRS["build.rs"]
        BuildRS --> Bindgen["bindgen"]
        Bindgen --> Bindings["abi_bindings.rs\n(auto-generated Rust FFI)"]
        Bindings --> LibRS["lib.rs\n(SDK implementation)"]
        LibRS --> SO["libmy_filter.so\n(cdylib)"]
    end
```

### Cargo.toml

```toml
[lib]
crate-type = ["cdylib"]

[dependencies]
envoy-dynamic-modules-rust-sdk = { path = "..." }
```

### Rust SDK Additional Features

| Module | Purpose |
|--------|---------|
| `buffer.rs` | Buffer view and manipulation types |
| `access_log.rs` | Access logger trait and registration |
| `cert_validator.rs` | Certificate validator trait |
| `lib.rs` | Core SDK: logging macros, function registry, HTTP filter traits |

---

## Development Workflow

```mermaid
flowchart TD
    subgraph Develop["1. Develop"]
        Write["Write filter in\nC++ / Go / Rust"]
        Test["Unit test with\nSDK fakes/mocks"]
    end

    subgraph Build["2. Build"]
        Compile["Compile to .so\n(shared library)"]
    end

    subgraph Deploy["3. Deploy"]
        Copy["Place libmy-filter.so\nin search path"]
        Config["Configure Envoy\nwith dynamic_module_config"]
    end

    subgraph Run["4. Run"]
        Start["Envoy starts"]
        Load["dlopen loads module"]
        Init["ABI version negotiated"]
        Serve["Module processes traffic"]
    end

    Develop --> Build --> Deploy --> Run
```

### Envoy Configuration Example

```yaml
http_filters:
- name: envoy.filters.http.dynamic_modules
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.dynamic_modules.v3.DynamicModuleFilter
    dynamic_module_config:
      name: my-filter          # loads libmy-filter.so
      do_not_close: false      # true for Go modules
    filter_name: my-filter     # selects implementation in module
    filter_config: '{"key": "value"}'  # passed to module
```

---

## Testing Framework

```mermaid
flowchart TD
    subgraph UnitTests["Unit Tests (per language)"]
        CPP_Fake["C++ SDK Fakes\nsdk_fake.h\n- FakeHeaderMap\n- FakeBodyBuffer"]
        Go_Mock["Go Mocks\nshared/mocks/\n- mockgen generated"]
        Go_Fake["Go Fakes\nshared/fake/\n- FakeStreamBase"]
        Rust_Test["Rust Tests\nlib_test.rs"]
    end

    subgraph IntegrationTests["Integration Tests (Envoy)"]
        HFilter["HTTP filter integration\n(C, Go, Rust modules)"]
        NFilter["Network filter integration"]
        Bootstrap["Bootstrap integration"]
        AccessLog["Access logger integration"]
    end

    subgraph TestModules["Test Modules"]
        NoOp["no_op - minimal module"]
        InitFail["program_init_fail"]
        ABIMismatch["abi_version_mismatch"]
        HTTP["http test modules\n(various scenarios)"]
    end

    UnitTests --> IntegrationTests
    TestModules --> IntegrationTests
```

### Parameterized Multi-Language Tests

Tests run the same scenarios across C, Go, and Rust:

```mermaid
flowchart LR
    TestCase["Test: header manipulation"] --> C_Module["C module"]
    TestCase --> Go_Module["Go module"]
    TestCase --> Rust_Module["Rust module"]

    C_Module --> Verify["Same assertions"]
    Go_Module --> Verify
    Rust_Module --> Verify
```

The `DynamicModuleTestLanguages` test parameter provides all language variants, and `testSharedObjectPath()` resolves the correct `.so` path for each.
