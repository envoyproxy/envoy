# HTTP/1 Legacy Parser — `legacy_parser_impl.h`

**File:** `source/common/http/http1/legacy_parser_impl.h`

`LegacyHttpParserImpl` is the **original HTTP/1.1 parser** in Envoy, wrapping the
[node.js `http_parser`](https://github.com/nodejs/http-parser) C library. It implements
the `Parser` interface and is the fallback when the `use_balsa_parser` runtime flag is disabled.

---

## Class Overview

```mermaid
classDiagram
    class Parser {
        <<interface>>
        +execute(slice, len) size_t
        +resume()
        +pause() CallbackResult
        +getStatus() ParserStatus
        +statusCode() Http::Code
        +isHttp11() bool
        +contentLength() optional~uint64_t~
        +isChunked() bool
        +methodName() string_view
        +errorMessage() string_view
        +hasTransferEncoding() int
    }

    class LegacyHttpParserImpl {
        +execute(slice, len) size_t
        +resume()
        +pause() CallbackResult
        +getStatus() ParserStatus
        -impl_ : Impl (pImpl)
    }

    class Impl {
        <<private pImpl>>
        wraps http_parser struct
        wraps http_parser_settings
    }

    Parser <|-- LegacyHttpParserImpl
    LegacyHttpParserImpl o-- Impl : pImpl pattern
```

---

## Design Notes

### pImpl Pattern
`LegacyHttpParserImpl` uses the **pImpl (pointer to implementation) idiom** via the private
`Impl` class. This isolates the `http_parser.h` C headers from the rest of the Envoy codebase,
avoiding macro and symbol pollution from the C library.

```cpp
class LegacyHttpParserImpl : public Parser {
  private:
    class Impl;                    // Forward-declared
    std::unique_ptr<Impl> impl_;   // Actual http_parser struct lives here
};
```

### Callback Translation
The underlying `http_parser` fires C function callbacks (`on_url`, `on_header_field`, etc.).
The `Impl` class registers these as `http_parser_settings` and translates them into
`ParserCallbacks` method calls on `ConnectionImpl`.

```mermaid
sequenceDiagram
    participant CI as ConnectionImpl
    participant LP as LegacyHttpParserImpl
    participant HP as http_parser (C library)
    participant CB as ParserCallbacks (ConnectionImpl)

    CI->>LP: execute(slice, len)
    LP->>HP: http_parser_execute(&parser, &settings, slice, len)
    HP->>LP: on_message_begin callback
    LP->>CB: onMessageBegin()
    HP->>LP: on_url callback
    LP->>CB: onUrl(data, len)
    HP->>LP: on_header_field callback
    LP->>CB: onHeaderField(data, len)
    HP->>LP: on_header_value callback
    LP->>CB: onHeaderValue(data, len)
    HP->>LP: on_headers_complete callback
    LP->>CB: onHeadersComplete()
    HP->>LP: on_body callback
    LP->>CB: bufferBody(data, len)
    HP->>LP: on_message_complete callback
    LP->>CB: onMessageComplete()
```

---

## Comparison with `BalsaParser`

| Aspect | `LegacyHttpParserImpl` | `BalsaParser` |
|---|---|---|
| Underlying library | node.js `http_parser` (C) | QUICHE `BalsaFrame` (C++) |
| Isolation | pImpl pattern | Direct member |
| Custom methods | Limited | `allow_custom_methods_` flag |
| Interim headers | Not natively supported | `OnInterimHeaders()` |
| Status | **Deprecated / fallback** | **Preferred** |
| Selection | `use_balsa_parser = false` | `use_balsa_parser = true` (default) |

---

## When Is It Used?

```mermaid
flowchart TD
    A[HTTP/1 connection created] --> B{Runtime flag\nuse_balsa_parser?}
    B -->|true - default| C[BalsaParser selected]
    B -->|false - legacy| D[LegacyHttpParserImpl selected]
    D --> E[Uses http_parser C library\nvia pImpl]
```

> The `LegacyHttpParserImpl` is retained for compatibility and rollback purposes.
> New development should target `BalsaParser`.
