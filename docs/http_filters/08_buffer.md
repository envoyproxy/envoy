# Buffer Filter

## Overview

The Buffer filter buffers the entire request body before forwarding it to the upstream service. This is useful when the upstream service requires the full request body to be present before processing (e.g., for signature verification, content validation, or when using HTTP/1.0 which doesn't support chunked encoding).

## Key Responsibilities

- Buffer complete request body
- Enforce maximum buffer size
- Handle streaming vs buffered requests
- Integrate with route configuration
- Support runtime configuration
- Provide observability for buffer operations

## Architecture

```mermaid
classDiagram
    class BufferFilter {
        +BufferConfig config_
        +Buffer::Instance buffer_
        +bool request_buffered_
        +decodeHeaders() FilterHeadersStatus
        +decodeData() FilterDataStatus
        +decodeTrailers() FilterTrailersStatus
    }

    class BufferConfig {
        +uint64_t max_request_bytes_
        +bool enabled_
    }

    class BufferInstance {
        +add() void
        +drain() void
        +length() uint64_t
        +move() void
    }

    BufferFilter --> BufferConfig
    BufferFilter --> BufferInstance
```

## Request Flow - Buffering

```mermaid
sequenceDiagram
    participant Client
    participant FM as Filter Manager
    participant Buffer as Buffer Filter
    participant Router
    participant Upstream

    Client->>FM: POST /api (start)
    FM->>Buffer: decodeHeaders(headers, end_stream=false)

    alt Buffer Enabled for Route
        Buffer->>Buffer: Check max_request_bytes
        Buffer->>FM: StopIterationAndBuffer

        Note over Buffer: Waiting for complete body

        Client->>FM: Body chunk 1
        FM->>Buffer: decodeData(chunk1, end_stream=false)
        Buffer->>Buffer: Add to buffer
        Buffer->>FM: StopIterationAndBuffer

        Client->>FM: Body chunk 2
        FM->>Buffer: decodeData(chunk2, end_stream=false)
        Buffer->>Buffer: Add to buffer
        Buffer->>Buffer: Check buffer size
        alt Buffer size <= max_request_bytes
            Buffer->>FM: StopIterationAndBuffer
        else Buffer size > max_request_bytes
            Buffer->>FM: sendLocalReply(413, "Payload Too Large")
            FM->>Client: 413 Payload Too Large
            Note over Buffer,Upstream: Request terminated
        end

        Client->>FM: Body chunk 3 (final)
        FM->>Buffer: decodeData(chunk3, end_stream=true)
        Buffer->>Buffer: Add to buffer
        Buffer->>Buffer: Complete body received

        Buffer->>FM: Continue with buffered body
        FM->>Router: decodeHeaders(headers, end_stream=false)
        FM->>Router: decodeData(full_body, end_stream=true)
        Router->>Upstream: Send complete request
        Upstream-->>Router: Response
        Router-->>Client: Response

    else Buffer Disabled
        Buffer->>FM: Continue
        FM->>Router: Stream request normally
        Router->>Upstream: Chunked request
    end
```

## Buffer Size Check

```mermaid
flowchart TD
    A[Receive Data Chunk] --> B[Add to Buffer]
    B --> C{Buffer Size Check}
    C --> D[Current Size]
    D --> E{Size <= max_request_bytes?}

    E -->|Yes| F{end_stream?}
    F -->|Yes| G[Forward Complete Request]
    F -->|No| H[Continue Buffering]

    E -->|No| I[Buffer Limit Exceeded]
    I --> J[Drain Buffer]
    J --> K[Send 413 Response]
    K --> L[Close Connection]

    H --> M[StopIterationAndBuffer]
    G --> N[Continue with Full Body]
```

## State Machine

```mermaid
stateDiagram-v2
    [*] --> Initial
    Initial --> Buffering: decodeHeaders (not end_stream)
    Initial --> Passthrough: Buffer disabled or end_stream
    Buffering --> Buffering: decodeData (not end_stream)
    Buffering --> Complete: decodeData (end_stream)
    Buffering --> Error: Buffer limit exceeded
    Complete --> Forwarding: Continue with buffered body
    Forwarding --> [*]
    Error --> [*]: 413 Response
    Passthrough --> [*]: Stream through

    note right of Buffering
        Accumulating request body
        Checking size limits
    end note

    note right of Complete
        Full body received
        Ready to forward
    end note

    note right of Error
        max_request_bytes exceeded
    end note
```

## Configuration Example - Filter Level

```yaml
name: envoy.filters.http.buffer
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
  max_request_bytes: 1048576  # 1 MB
```

## Configuration Example - Per-Route

```yaml
routes:
  - match:
      prefix: "/api/upload"
    route:
      cluster: upload_service
    typed_per_filter_config:
      envoy.filters.http.buffer:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
        buffer:
          max_request_bytes: 10485760  # 10 MB for uploads

  - match:
      prefix: "/api/webhook"
    route:
      cluster: webhook_service
    typed_per_filter_config:
      envoy.filters.http.buffer:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
        buffer:
          max_request_bytes: 262144  # 256 KB for webhooks

  - match:
      prefix: "/api/streaming"
    route:
      cluster: streaming_service
    typed_per_filter_config:
      envoy.filters.http.buffer:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
        disabled: true  # Disable buffering for streaming endpoint
```

## Buffer Limit Handling

```mermaid
sequenceDiagram
    participant Client
    participant Buffer as Buffer Filter
    participant FM as Filter Manager

    Client->>Buffer: Large request (streaming)
    Note over Buffer: max_request_bytes = 1MB

    loop Receiving chunks
        Client->>Buffer: Chunk (200KB)
        Buffer->>Buffer: Buffer += 200KB
        Note over Buffer: Total: 200KB
        Buffer-->>Client: Continue

        Client->>Buffer: Chunk (300KB)
        Buffer->>Buffer: Buffer += 300KB
        Note over Buffer: Total: 500KB
        Buffer-->>Client: Continue

        Client->>Buffer: Chunk (400KB)
        Buffer->>Buffer: Buffer += 400KB
        Note over Buffer: Total: 900KB
        Buffer-->>Client: Continue

        Client->>Buffer: Chunk (300KB)
        Buffer->>Buffer: Buffer would be 1.2MB
        Note over Buffer: Exceeds 1MB limit!
        Buffer->>FM: sendLocalReply(413)
        FM->>Client: 413 Payload Too Large
        Buffer->>Buffer: Drain buffer
    end
```

## Use Case Diagram

```mermaid
flowchart TD
    A[Buffer Filter Use Cases] --> B[Signature Verification]
    A --> C[Content Validation]
    A --> D[Legacy HTTP/1.0]
    A --> E[Transformation]
    A --> F[Logging/Auditing]

    B --> B1[Calculate hash of<br/>complete request body]
    C --> C1[Validate JSON/XML<br/>schema before upstream]
    D --> D1[Convert chunked to<br/>Content-Length]
    E --> E1[Body transformation<br/>requires full content]
    F --> F1[Log complete request<br/>for compliance]
```

## Memory Management

```mermaid
flowchart TD
    A[Request Arrives] --> B[Allocate Buffer]
    B --> C[Accumulate Data]
    C --> D{Buffer Full?}
    D -->|No| E{More Data?}
    E -->|Yes| C
    E -->|No| F[Forward Complete Request]
    D -->|Yes| G{Size Check}
    G -->|Within Limit| F
    G -->|Exceeds Limit| H[Reject Request]
    F --> I[Free Buffer]
    H --> I
    I --> J[Buffer Deallocated]
```

## Configuration with Runtime Override

```yaml
name: envoy.filters.http.buffer
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer
  max_request_bytes: 1048576  # Default 1 MB

# Runtime configuration (can override without restart)
runtime:
  layers:
    - name: admin
      admin_layer: {}
    - name: static
      static_layer:
        # Override max buffer size to 2MB
        envoy.http.buffer.max_request_bytes: 2097152
```

## Integration with Other Filters

```mermaid
sequenceDiagram
    participant Client
    participant ExtAuthz as ext_authz Filter
    participant Buffer as Buffer Filter
    participant CustomFilter
    participant Router

    Client->>ExtAuthz: Request
    Note over ExtAuthz: Auth check on headers only

    alt Auth Failed
        ExtAuthz->>Client: 401 Unauthorized
    else Auth Success
        ExtAuthz->>Buffer: Continue
        Buffer->>Buffer: Buffer complete body
        Buffer->>CustomFilter: Full request body
        Note over CustomFilter: Transform/validate body
        CustomFilter->>Router: Modified request
        Router->>Client: Response
    end
```

## Performance Considerations

```mermaid
mindmap
  root((Buffer Filter<br/>Performance))
    Memory Usage
      "Per-request allocation"
      "max_request_bytes limit"
      "Multiple concurrent requests"
      "Memory pressure"
    Latency Impact
      "Buffering delay"
      "Complete body wait"
      "Not suitable for streaming"
      "Increased TTFB"
    Throughput
      "Reduced for large requests"
      "Connection held longer"
      "Upstream waits for full body"
    Resource Limits
      "Set appropriate max_request_bytes"
      "Monitor buffer pool"
      "Consider memory limits"
```

## Statistics

| Stat | Type | Description |
|------|------|-------------|
| buffer.rq_too_large | Counter | Requests exceeding max_request_bytes |
| buffer.rq_buffered | Counter | Requests successfully buffered |

## Common Use Cases

### 1. Request Signature Verification
Buffer entire request to compute HMAC/signature

```yaml
# AWS SigV4, similar patterns
routes:
  - match:
      prefix: "/api"
    route:
      cluster: api_cluster
    typed_per_filter_config:
      envoy.filters.http.buffer:
        "@type": type.googleapis.com/envoy.extensions.filters.http.buffer.v3.BufferPerRoute
        buffer:
          max_request_bytes: 1048576
```

### 2. Request Body Validation
Validate complete JSON/XML before forwarding

### 3. Logging/Auditing
Log complete request body for compliance

### 4. Body Transformation
Transform request body (requires full content)

### 5. Legacy Protocol Support
Convert chunked encoding to Content-Length

### 6. External Processing
Send complete request to ext_proc for processing

## Best Practices

1. **Set appropriate max_request_bytes** - Based on expected payload sizes
2. **Don't buffer large files** - Use streaming for uploads/downloads
3. **Monitor memory usage** - Buffer filter can consume significant memory
4. **Use per-route config** - Only buffer where necessary
5. **Consider upstream requirements** - Buffer only if upstream needs it
6. **Handle 413 gracefully** - Provide clear error messages
7. **Set client_max_body_size** - In conjunction with nginx/similar
8. **Test with production data sizes** - Avoid surprises
9. **Monitor rq_too_large stat** - Adjust limits if needed
10. **Disable for streaming APIs** - WebSocket, SSE, etc.

## When NOT to Use Buffer Filter

```mermaid
flowchart TD
    A[Request Type] --> B{Large File<br/>Upload?}
    B -->|Yes| C[Don't Buffer]
    C --> C1[Use streaming]

    A --> D{WebSocket or<br/>SSE?}
    D -->|Yes| E[Don't Buffer]
    E --> E1[Requires streaming]

    A --> F{Video/Audio<br/>Streaming?}
    F -->|Yes| G[Don't Buffer]
    G --> G1[Progressive download]

    A --> H{High Throughput<br/>API?}
    H -->|Yes| I[Consider Not Buffering]
    I --> I1[Latency sensitive]

    A --> J{Small Payloads<br/>+ Need Full Body?}
    J -->|Yes| K[Use Buffer]
    K --> K1[Validation, signatures]
```

## Troubleshooting

```mermaid
flowchart TD
    A[Buffer Issue] --> B{Issue Type?}

    B -->|413 Errors| C[Check max_request_bytes]
    C --> C1[Increase limit or<br/>disable for route]

    B -->|High Memory| D[Too many concurrent<br/>buffered requests]
    D --> D1[Reduce max_request_bytes<br/>or limit concurrency]

    B -->|Timeout| E[Buffering takes too long]
    E --> E1[Increase timeout or<br/>disable buffering]

    B -->|Not Buffering| F[Check per-route config]
    F --> F1[Ensure not disabled<br/>for route]
```

## Comparison: Buffer Filter vs Other Approaches

| Approach | When to Use | Pros | Cons |
|----------|-------------|------|------|
| Buffer Filter | Small payloads, need full body | Simple, built-in | Memory intensive |
| Streaming | Large payloads | Memory efficient | More complex |
| ext_proc | Complex processing | Flexible | Additional service |
| Lua Filter | Custom logic | Highly customizable | Requires scripting |

## Related Filters

- **ext_proc**: External processing of buffered requests
- **ext_authz**: Can buffer via with_request_body
- **lua**: Custom buffering logic
- **grpc_json_transcoder**: Requires full body

## References

- [Envoy Buffer Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/buffer_filter)
- [HTTP Buffering Best Practices](https://www.envoyproxy.io/docs/envoy/latest/faq/performance/how_fast_is_envoy)
