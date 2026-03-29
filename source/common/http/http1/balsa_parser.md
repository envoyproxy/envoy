# HTTP/1 Balsa Parser — `balsa_parser.h`

**File:** `source/common/http/http1/balsa_parser.h`

`BalsaParser` is the **preferred HTTP/1.1 parser** in Envoy, wrapping the QUICHE project's
`BalsaFrame` framer. It implements the `Parser` interface and receives raw frame events from
`BalsaFrame` via `quiche::BalsaVisitorInterface`, translating them into `ParserCallbacks`
calls consumed by `ConnectionImpl`.

---

## Class Overview

```mermaid
classDiagram
    class BalsaParser {
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
        -framer_ : BalsaFrame
        -headers_ : BalsaHeaders
        -message_type_
        -status_
        -first_byte_processed_
        -headers_done_
        -first_message_
        -enable_trailers_
        -allow_custom_methods_
    }

    class BalsaVisitorInterface {
        <<quiche interface>>
        +OnRawBodyInput()
        +OnBodyChunkInput()
        +OnHeaderInput()
        +OnTrailerInput()
        +OnTrailers()
        +ProcessHeaders()
        +OnRequestFirstLineInput()
        +OnResponseFirstLineInput()
        +OnChunkLength()
        +HeaderDone()
        +MessageDone()
        +HandleError()
        +HandleWarning()
    }

    class Parser {
        <<interface>>
    }

    class ParserCallbacks {
        <<interface>>
    }

    Parser <|-- BalsaParser
    BalsaVisitorInterface <|.. BalsaParser
    BalsaParser --> ParserCallbacks : translates Balsa events to callbacks
    BalsaParser o-- BalsaFrame : wraps
```

---

## Data Flow

```mermaid
sequenceDiagram
    participant CI as ConnectionImpl
    participant BP as BalsaParser
    participant BF as quiche::BalsaFrame
    participant CB as ParserCallbacks (ConnectionImpl)

    CI->>BP: execute(slice, len)
    BP->>BF: ProcessInput(slice, len)
    BF->>BP: OnRequestFirstLineInput() / OnResponseFirstLineInput()
    BF->>BP: ProcessHeaders(BalsaHeaders)
    BP->>CB: onHeaderField() + onHeaderValue() [per header]
    BP->>CB: onHeadersComplete()
    BF->>BP: OnBodyChunkInput(data)
    BP->>CB: bufferBody(data, len)
    BF->>BP: MessageDone()
    BP->>CB: onMessageComplete()
```

---

## Key Design Points

### Balsa vs Legacy Parser

| Feature | `BalsaParser` | `LegacyHttpParserImpl` |
|---|---|---|
| Underlying library | QUICHE `BalsaFrame` | node.js `http_parser` |
| Custom HTTP methods | Configurable via `allow_custom_methods_` | Limited |
| Trailer support | Yes (`enable_trailers_`) | Yes |
| First-line parsing | `OnRequestFirstLineInput` / `OnResponseFirstLineInput` | via `onUrl` / `onStatus` |
| Error reporting | `HandleError()` + `HandleWarning()` | `errorMessage()` |
| Selection | Runtime flag `use_balsa_parser` | Default/fallback |

### `first_message_` Flag
Set to `true` until the first byte of the **second** message arrives. Used to distinguish
first-message parsing from pipelined request parsing — important for connection reuse semantics.

### `convertResult()` Helper
Internal helper that maps a `CallbackResult` from `ParserCallbacks` back to `ParserStatus`.
Marked `ABSL_MUST_USE_RESULT` — callers must not silently discard the returned status.

```cpp
// Typical usage pattern inside BalsaVisitorInterface callbacks:
status_ = convertResult(connection_->onHeadersComplete());
```

### Header/Trailer Processing
Both request headers and trailers share `validateAndProcessHeadersOrTrailersImpl()`.
This function iterates `BalsaHeaders`, fires `onHeaderField()` + `onHeaderValue()` callbacks
for each entry, then calls `onHeadersComplete()` or signals trailer end via `onMessageComplete()`.

### Interim Headers (`OnInterimHeaders`)
Handles `100 Continue` and other informational responses by firing callbacks
directly without going through the normal header processing pipeline.

---

## Error Handling

```mermaid
flowchart TD
    BF[BalsaFrame detects error] --> HE[HandleError(error_code)]
    HE --> SetMsg[error_message_ = mapped error string]
    HE --> SetStatus[status_ = ParserStatus::Error]
    SetStatus --> CI[ConnectionImpl::dispatch() returns error Status]
    CI --> Close[Connection closed with protocol error]

    BF2[BalsaFrame warning] --> HW[HandleWarning(error_code)]
    HW --> Log[Log warning, continue processing]
```
