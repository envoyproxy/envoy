# Part 2: Access Logs — Formatters, Filters, and gRPC Logs

## Formatter / Substitution System

### How Formatting Works

```mermaid
graph LR
    subgraph "Input"
        FMT["Format String\n'%REQ(:METHOD)% %REQ(:PATH)% %RESPONSE_CODE%'"]
        CTX["Formatter::Context\n(headers, trailers, type, span)"]
        SI["StreamInfo\n(timing, bytes, flags)"]
    end
    
    subgraph "Processing"
        Parser["SubstitutionFormatParser::parse()"]
        Providers["FormatterProvider list"]
    end
    
    subgraph "Output"
        Result["'GET /api/users 200'"]
    end
    
    FMT --> Parser
    Parser --> Providers
    Providers --> |"format(context, info)"| Result
    CTX --> Providers
    SI --> Providers
```

### Formatter Class Hierarchy

```mermaid
classDiagram
    class FormatterProvider {
        <<interface>>
        +formatWithContext(context, info) string
    }
    class FormatterImpl {
        -providers_ : vector~FormatterProviderPtr~
        +format(context, info) string
    }
    class JsonFormatterImpl {
        -json_output_format_ : StructFormatter
        +format(context, info) string
    }
    class SubstitutionFormatParser {
        +parse(format, parsers) vector~FormatterProviderPtr~$
    }
    class StreamInfoFormatterProvider {
        +formatWithContext(context, info) string
    }
    class RequestHeaderFormatterProvider {
        -header_name_ : string
        +formatWithContext(context, info) string
    }
    class ResponseHeaderFormatterProvider {
        -header_name_ : string
    }

    FormatterImpl --> FormatterProvider : "list of"
    JsonFormatterImpl --> FormatterProvider : "list of"
    StreamInfoFormatterProvider ..|> FormatterProvider
    RequestHeaderFormatterProvider ..|> FormatterProvider
    ResponseHeaderFormatterProvider ..|> FormatterProvider
```

### Format String Parsing

```mermaid
flowchart TD
    A["Format: '%REQ(:METHOD)% %REQ(:PATH)% %RESPONSE_CODE% %DURATION%'"]
    A --> B["SubstitutionFormatParser::parse()"]
    B --> C["Split into tokens"]
    
    C --> T1["PlainStringFormatter\n'(space)'"]
    C --> T2["RequestHeaderFormatter\n(:METHOD)"]
    C --> T3["RequestHeaderFormatter\n(:PATH)"]
    C --> T4["StreamInfoFormatter\n(RESPONSE_CODE)"]
    C --> T5["StreamInfoFormatter\n(DURATION)"]
    
    subgraph "Runtime"
        T2 -->|"format(ctx, info)"| R2["GET"]
        T3 -->|"format(ctx, info)"| R3["/api/users"]
        T4 -->|"format(ctx, info)"| R4["200"]
        T5 -->|"format(ctx, info)"| R5["15"]
    end
```

### Common Format Commands

| Command | Provider | Output |
|---------|----------|--------|
| `%REQ(header)%` | `RequestHeaderFormatterProvider` | Request header value |
| `%RESP(header)%` | `ResponseHeaderFormatterProvider` | Response header value |
| `%RESPONSE_CODE%` | `StreamInfoFormatterProvider` | HTTP response code |
| `%RESPONSE_FLAGS%` | `StreamInfoFormatterProvider` | Response flags (NR, UF, etc.) |
| `%DURATION%` | `StreamInfoFormatterProvider` | Request duration (ms) |
| `%BYTES_RECEIVED%` | `StreamInfoFormatterProvider` | Bytes received from downstream |
| `%BYTES_SENT%` | `StreamInfoFormatterProvider` | Bytes sent to downstream |
| `%UPSTREAM_HOST%` | `StreamInfoFormatterProvider` | Upstream host address |
| `%UPSTREAM_CLUSTER%` | `StreamInfoFormatterProvider` | Upstream cluster name |
| `%DOWNSTREAM_REMOTE_ADDRESS%` | `StreamInfoFormatterProvider` | Client IP:port |
| `%START_TIME%` | `StreamInfoFormatterProvider` | Request start time |
| `%PROTOCOL%` | `StreamInfoFormatterProvider` | HTTP protocol version |
| `%DYNAMIC_METADATA(ns:key)%` | `StreamInfoFormatterProvider` | Dynamic metadata value |
| `%FILTER_STATE(key)%` | `StreamInfoFormatterProvider` | Filter state value |

### JSON Formatter

```mermaid
graph TD
    subgraph "JSON Format Config"
        JF["typed_json_format:\n  status: '%RESPONSE_CODE%'\n  method: '%REQ(:METHOD)%'\n  path: '%REQ(:PATH)%'\n  duration_ms: '%DURATION%'"]
    end
    
    subgraph "Output"
        JSON["{\n  'status': '200',\n  'method': 'GET',\n  'path': '/api/users',\n  'duration_ms': '15'\n}"]
    end
    
    JF --> JSON
```

## Access Log Filters

### Filter Hierarchy

```mermaid
classDiagram
    class Filter {
        <<interface>>
        +evaluate(context, stream_info) bool
    }
    class StatusCodeFilter {
        -comparison_ : ComparisonFilter
        +evaluate(context, info) bool
    }
    class DurationFilter {
        -comparison_ : ComparisonFilter
        +evaluate(context, info) bool
    }
    class NotHealthCheckFilter {
        +evaluate(context, info) bool
    }
    class HeaderFilter {
        -header_data_ : HeaderUtility::HeaderData
        +evaluate(context, info) bool
    }
    class ResponseFlagFilter {
        -flags_ : set~ResponseFlag~
        +evaluate(context, info) bool
    }
    class GrpcStatusFilter {
        -statuses_ : set~GrpcStatus~
        +evaluate(context, info) bool
    }
    class AndFilter {
        -filters_ : vector~FilterPtr~
        +evaluate(context, info) bool
    }
    class OrFilter {
        -filters_ : vector~FilterPtr~
        +evaluate(context, info) bool
    }
    class RuntimeFilter {
        -runtime_key_ : string
        -percent_ : FractionalPercent
        +evaluate(context, info) bool
    }
    class LogTypeFilter {
        -types_ : set~AccessLogType~
        +evaluate(context, info) bool
    }

    StatusCodeFilter ..|> Filter
    DurationFilter ..|> Filter
    NotHealthCheckFilter ..|> Filter
    HeaderFilter ..|> Filter
    ResponseFlagFilter ..|> Filter
    GrpcStatusFilter ..|> Filter
    AndFilter ..|> Filter
    OrFilter ..|> Filter
    RuntimeFilter ..|> Filter
    LogTypeFilter ..|> Filter
```

### Filter Evaluation Flow

```mermaid
flowchart TD
    A["ImplBase::log(context, info)"] --> B{filter_ != nullptr?}
    B -->|No| C["emitLog() — always log"]
    B -->|Yes| D["filter_->evaluate(context, info)"]
    D -->|true| C
    D -->|false| E["Skip — don't log"]
```

### Composite Filter Example

```mermaid
graph TD
    subgraph "AND Filter"
        AND["AndFilter"]
        AND --> SF["StatusCodeFilter\n(>= 400)"]
        AND --> NHC["NotHealthCheckFilter"]
    end
    
    subgraph "Evaluation"
        SF -->|"400 >= 400 → true"| R1["true"]
        NHC -->|"not health check → true"| R2["true"]
        R1 --> ANDRESULT["AND → true → LOG"]
    end
    
    Note["Only log 4xx/5xx requests\nthat aren't health checks"]
```

## gRPC Access Logs

### Architecture

```mermaid
graph TD
    subgraph "Per-Worker"
        HGrpc["HttpGrpcAccessLog"]
        TLS["ThreadLocal Logger"]
        Logger["GrpcAccessLoggerImpl"]
        Buffer["Log Entry Buffer"]
    end
    
    subgraph "gRPC Transport"
        Client["AsyncClient"]
        Stream["StreamAccessLogs RPC\n(bidirectional gRPC stream)"]
    end
    
    subgraph "Server"
        ALS["AccessLogService\n(management server)"]
    end
    
    HGrpc --> TLS
    TLS --> Logger
    Logger --> Buffer
    Buffer -->|"flush on timer\nor size limit"| Client
    Client --> Stream
    Stream --> ALS
```

### gRPC Access Log Flow

```mermaid
sequenceDiagram
    participant AS as ActiveStream
    participant HGAL as HttpGrpcAccessLog
    participant Logger as GrpcAccessLoggerImpl
    participant Buffer as Entry Buffer
    participant Timer as Flush Timer
    participant gRPC as gRPC Stream

    AS->>HGAL: log(context, stream_info)
    HGAL->>HGAL: evaluate filter
    HGAL->>HGAL: Build HTTPAccessLogEntry proto
    Note over HGAL: Populate: common_properties,\nrequest, response, headers
    
    HGAL->>Logger: log(entry)
    Logger->>Buffer: Add entry to buffer
    
    alt Buffer size >= buffer_size_bytes
        Logger->>gRPC: Flush entries
        Logger->>Buffer: Clear buffer
    end
    
    alt Flush timer fires
        Timer->>Logger: Flush
        Logger->>gRPC: StreamAccessLogsMessage
        Logger->>Buffer: Clear buffer
    end
```

### gRPC Access Log Entry Structure

```mermaid
graph TD
    subgraph "HTTPAccessLogEntry"
        CP["common_properties"]
        REQ["request"]
        RESP["response"]
    end
    
    subgraph "CommonProperties"
        CP --> SampleRate["sample_rate"]
        CP --> DownstreamRemote["downstream_remote_address"]
        CP --> DownstreamLocal["downstream_local_address"]
        CP --> TLSProps["tls_properties"]
        CP --> StartTime["start_time"]
        CP --> Duration["duration"]
        CP --> UpstreamHost["upstream_remote_address"]
        CP --> UpstreamCluster["upstream_cluster"]
        CP --> ResponseFlags["response_flags"]
        CP --> Metadata["metadata"]
        CP --> FilterState["filter_state_objects"]
    end
    
    subgraph "Request"
        REQ --> Method["request_method"]
        REQ --> Scheme["scheme"]
        REQ --> Authority["authority"]
        REQ --> Path["path"]
        REQ --> UserAgent["user_agent"]
        REQ --> RequestHeaders["request_headers"]
        REQ --> RequestBodyBytes["request_body_bytes"]
    end
    
    subgraph "Response"
        RESP --> ResponseCode["response_code"]
        RESP --> ResponseHeaders["response_headers"]
        RESP --> ResponseBodyBytes["response_body_bytes"]
        RESP --> ResponseCodeDetails["response_code_details"]
    end
```

### gRPC Batching Configuration

```mermaid
graph LR
    subgraph "Batching Config"
        BFI["buffer_flush_interval\n(default: 1000ms)"]
        BSB["buffer_size_bytes\n(default: 16384)"]
    end
    
    subgraph "Flush Triggers"
        T["Timer fires\n(every buffer_flush_interval)"]
        S["Buffer exceeds\nbuffer_size_bytes"]
    end
    
    T --> Flush["Send StreamAccessLogsMessage"]
    S --> Flush
```

## Access Logger Types

```mermaid
graph TD
    subgraph "Built-in Access Loggers"
        File["FileAccessLog\n→ disk file"]
        Stream_["StreamAccessLog\n→ stdout/stderr"]
        HttpGrpc["HttpGrpcAccessLog\n→ gRPC ALS"]
        TcpGrpc["TcpGrpcAccessLog\n→ gRPC ALS"]
        OTLP["OpenTelemetry\n→ OTLP collector"]
        Fluentd_["FluentdAccessLog\n→ Fluentd"]
        Stats_["StatsAccessLog\n→ stats counters"]
        Wasm_["WasmAccessLog\n→ Wasm module"]
    end
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `source/common/formatter/substitution_formatter.h` | `FormatterImpl`, `SubstitutionFormatParser` | Format parsing and execution |
| `source/common/formatter/stream_info_formatter.h` | `StreamInfoFormatterProvider` | StreamInfo-based formatters |
| `source/common/formatter/http_specific_formatter.cc` | HTTP-specific formatters | Request/response header formatters |
| `source/common/access_log/access_log_impl.cc:55` | `FilterFactory::fromProto()` | Access log filter creation |
| `source/extensions/access_loggers/grpc/grpc_access_log_impl.h` | `GrpcAccessLoggerImpl`, `GrpcAccessLoggerCacheImpl` | gRPC logger |
| `source/extensions/access_loggers/grpc/http_grpc_access_log_impl.cc` | `HttpGrpcAccessLog` | HTTP gRPC access log |
| `source/extensions/access_loggers/common/grpc_access_logger.h` | Base gRPC logger | Batching, streaming |

---

**Previous:** [Part 1 — Architecture & Lifecycle](01-architecture-lifecycle.md)  
**Next:** [Part 3 — File I/O, Flushing, and Periodic Logs](03-file-io-flushing.md)
