# HeaderMapImpl

**File:** `source/common/http/header_map_impl.h` / `.cc`  
**Size:** ~34 KB header, ~19 KB implementation  
**Namespace:** `Envoy::Http`

## Overview

`HeaderMapImpl` is Envoy's high-performance HTTP header container. It provides **O(1) access** to well-known headers via statically generated inline slots (avoiding hash map lookups for common headers like `:status`, `Content-Type`, `Authorization`) while storing arbitrary custom headers in a flat list. It also maintains a running byte-count for watermark enforcement.

## Design: Inline vs. Non-Inline Headers

```mermaid
flowchart TD
    A[Header Lookup] --> B{Is it a well-known\ninline header?}
    B -->|Yes| C["Direct slot access\nO(1) — no hash"]
    B -->|No| D["Scan flat list\nO(n) — n is small"]

    subgraph InlineStorage["Inline Header Slots (compiled-in)"]
        S1[":status slot"]
        S2["content-type slot"]
        S3["authorization slot"]
        S4["... ~50+ slots"]
    end

    C --> InlineStorage
```

## Class Hierarchy

```mermaid
classDiagram
    class HeaderMapImpl {
        +addCopy(key, value)
        +addViaMove(key, value)
        +setReference(key, value)
        +get(key): HeaderEntry*
        +remove(key)
        +iterate(callback)
        +size(): size_t
        +byteSize(): uint64_t
        -headers_: HeaderList
        -inline_headers_: InlineHeaderVector
        -cached_byte_size_: uint64_t
    }

    class TypedHeaderMapImpl~T~ {
        <<template>>
    }

    class RequestHeaderMapImpl
    class ResponseHeaderMapImpl
    class RequestTrailerMapImpl
    class ResponseTrailerMapImpl

    HeaderMapImpl <|-- TypedHeaderMapImpl
    TypedHeaderMapImpl <|-- RequestHeaderMapImpl
    TypedHeaderMapImpl <|-- ResponseHeaderMapImpl
    TypedHeaderMapImpl <|-- RequestTrailerMapImpl
    TypedHeaderMapImpl <|-- ResponseTrailerMapImpl
```

## Inline Header Access (Generated via Macros)

Macros expand at compile time to generate typed accessor methods for each well-known header:

```cpp
// Macro generates:
//   const HeaderEntry* path() const;       // getter
//   void removePath();                     // remover
//   void setPath(absl::string_view value); // setter
DEFINE_INLINE_HEADER_FUNCS(path)
DEFINE_INLINE_HEADER_STRING_FUNCS(content_type)
DEFINE_INLINE_HEADER_NUMERIC_FUNCS(content_length)
```

```mermaid
flowchart LR
    subgraph "RequestHeaderMap (inline slots)"
        slot_path[":path\n(slot index 0)"]
        slot_method[":method\n(slot index 1)"]
        slot_scheme[":scheme\n(slot index 2)"]
        slot_auth[":authority\n(slot index 3)"]
        slot_ct["content-type\n(slot index N)"]
    end

    caller["headers.path()"] --> slot_path
    caller2["headers.contentType()"] --> slot_ct
```

## Memory Layout

```
HeaderMapImpl
├── inline_headers_: array<HeaderEntryImpl*, N>
│     ├── [0] → HeaderEntryImpl { key=":path", value="/api/v1" }
│     ├── [1] → HeaderEntryImpl { key=":method", value="GET" }
│     └── ... (nullptr if not set)
│
└── headers_: std::list<HeaderEntryImpl>
      ├── HeaderEntryImpl { key="x-request-id", value="abc123" }
      ├── HeaderEntryImpl { key="x-envoy-upstream-service-time", value="5" }
      └── ... (custom/non-inline headers)
```

## Byte Size Tracking

```mermaid
sequenceDiagram
    participant Caller
    participant HM as HeaderMapImpl
    participant WM as Watermark Buffer

    Caller->>HM: addCopy("x-custom", "value")
    HM->>HM: cached_byte_size_ += key.size() + value.size() + 2 (CRLF)
    HM-->>WM: (downstream checks byteSize() for watermark)

    Caller->>HM: remove("x-custom")
    HM->>HM: cached_byte_size_ -= key.size() + value.size() + 2
```

## Common Operations

| Operation | Method | Complexity |
|-----------|--------|------------|
| Get inline header | `headers.path()` | O(1) |
| Set inline header | `headers.setPath("/foo")` | O(1) |
| Remove inline header | `headers.removePath()` | O(1) |
| Get custom header | `headers.get(LowerCaseString("x-custom"))` | O(n) |
| Add custom header | `headers.addCopy(key, value)` | O(1) amortized |
| Remove custom header | `headers.remove(LowerCaseString("x-custom"))` | O(n) |
| Iterate all headers | `headers.iterate(callback)` | O(n) inline + O(m) list |
| Byte size | `headers.byteSize()` | O(1) |

## `HeaderEntry` and `HeaderString`

```mermaid
classDiagram
    class HeaderEntry {
        <<interface>>
        +key(): HeaderString
        +value(): HeaderString
        +setValue(value)
    }

    class HeaderEntryImpl {
        -key_: HeaderString
        -value_: HeaderString
    }

    class HeaderString {
        +getStringView(): absl::string_view
        +empty(): bool
        +size(): size_t
        -buffer_: union { inline_buffer[128], heap_ptr }
    }

    HeaderEntry <|-- HeaderEntryImpl
    HeaderEntryImpl *-- HeaderString : key_
    HeaderEntryImpl *-- HeaderString : value_
```

`HeaderString` uses a small-string optimization: strings up to 128 bytes are stored in an inline buffer on the stack; longer strings are heap-allocated.

## Iteration Protocol

```mermaid
sequenceDiagram
    participant Consumer
    participant HM as HeaderMapImpl

    Consumer->>HM: iterate(callback)
    loop for each inline slot (not null)
        HM->>Consumer: callback(key, value)
        Consumer-->>HM: HeaderMap::Iterate::Continue
    end
    loop for each entry in headers_ list
        HM->>Consumer: callback(key, value)
        Consumer-->>HM: HeaderMap::Iterate::Continue or Break
    end
```

## Immutable vs. Mutable Views

| Type | Interface | Use Case |
|------|-----------|---------|
| `RequestHeaderMap` | Mutable | Downstream request headers in filter chain |
| `ResponseHeaderMap` | Mutable | Upstream response headers in filter chain |
| `RequestTrailerMap` | Mutable | Downstream request trailers |
| `ResponseTrailerMap` | Mutable | Upstream response trailers |

All four have `Impl` variants (`RequestHeaderMapImpl` etc.) via `TypedHeaderMapImpl<T>`.

## Performance Characteristics

- **No hashing** for the ~50+ well-known headers (HTTP/2 pseudo-headers, standard HTTP/1.1 headers, Envoy-specific `x-envoy-*` headers)
- **Cache-friendly** inline slot array (contiguous memory, avoids pointer chasing)
- **O(1) byte-size** avoids re-scanning headers on every watermark check
- **Copy-on-write semantics** for `setReference()` (zero-copy when the caller owns the lifetime)

## Related Files

| File | Relationship |
|------|-------------|
| `headers.h` | Defines `CustomHeaderValues` — the singleton registry of all inline header names |
| `header_utility.h` | Higher-level header manipulation (matchers, add/remove, case normalization) |
| `header_mutation.h` | Programmatic mutation rules (add, set, remove, append) |
| `character_set_validation.h` | Character set validation for header values |
