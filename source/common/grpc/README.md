# Envoy gRPC Implementation

**Location**: `/source/common/grpc/`
**Purpose**: Native gRPC support over HTTP/2, including client, codec, and protocol utilities

## Overview

Envoy provides **two gRPC client implementations**:

1. **Envoy gRPC** (This directory) - Native implementation using Envoy's HTTP/2 stack
2. **Google gRPC** - Wrapper around Google's C++ gRPC library

### Key Files

| File | Purpose |
|------|---------|
| `async_client_impl.h/cc` | Async gRPC client built on HTTP/2 |
| `codec.h/cc` | gRPC frame encoding/decoding |
| `common.h/cc` | Protocol utilities and helpers |
| `context_impl.h/cc` | gRPC context and statistics |
| `google_async_client_impl.h/cc` | Google gRPC C++ wrapper |
| `status.h/cc` | gRPC status code handling |
| `typed_async_client.h/cc` | Type-safe protobuf client |

---

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    gRPC Request                           │
│                         ↓                                  │
│  ┌──────────────────────────────────────────────────┐   │
│  │         AsyncClientImpl (gRPC layer)             │   │
│  │  - Service/method resolution                     │   │
│  │  - gRPC frame encoding                           │   │
│  │  - Status/trailer handling                       │   │
│  └──────────────────────────────────────────────────┘   │
│                         ↓                                  │
│  ┌──────────────────────────────────────────────────┐   │
│  │      HTTP/2 AsyncClient (transport)              │   │
│  │  - HTTP/2 multiplexing                           │   │
│  │  - Stream management                             │   │
│  │  - Header/data encoding                          │   │
│  └──────────────────────────────────────────────────┘   │
│                         ↓                                  │
│  ┌──────────────────────────────────────────────────┐   │
│  │         Connection Pool                           │   │
│  └──────────────────────────────────────────────────┘   │
│                         ↓                                  │
│                   Upstream Server                          │
└──────────────────────────────────────────────────────────┘
```

### gRPC over HTTP/2 Mapping

| gRPC Concept | HTTP/2 Mapping |
|--------------|----------------|
| Service call | HTTP/2 POST request |
| Method | `:path` header (`/package.Service/Method`) |
| Request message | HTTP/2 DATA frames with gRPC framing |
| Response message | HTTP/2 DATA frames with gRPC framing |
| Status | `grpc-status` trailer |
| Metadata | HTTP/2 headers |
| Timeout | `grpc-timeout` header |

---

## Documentation Files

### Core Components

1. **[async_client_impl.md](./async_client_impl.md)** - Async gRPC client implementation
   - Unary and streaming RPC patterns
   - Request/response lifecycle
   - Integration with HTTP/2 async client

2. **[codec.md](./codec.md)** - gRPC frame encoding/decoding
   - Frame format (5-byte header + payload)
   - Encoder/Decoder implementation
   - Compression handling

3. **[common.md](./common.md)** - Protocol utilities
   - Header validation (content-type, grpc-status)
   - Service/method resolution
   - Timeout parsing/encoding
   - Protobuf serialization

4. **[context_impl.md](./context_impl.md)** - gRPC context and stats
   - Per-service/method statistics
   - Success/failure tracking
   - Message count tracking

### Implementation Guides

5. **[google_grpc_client.md](./google_grpc_client.md)** - Google C++ gRPC wrapper
   - When to use Google gRPC vs Envoy gRPC
   - Credentials handling
   - Thread model differences

6. **[status_handling.md](./status_handling.md)** - gRPC status codes
   - Status code mapping (gRPC ↔ HTTP)
   - Error handling patterns
   - `grpc-status-details-bin` rich status

---

## Quick Start Examples

### Unary RPC (Request-Response)

```cpp
// 1. Create async client
auto grpc_client = std::make_unique<Grpc::AsyncClientImpl>(
    config,
    cluster_manager,
    time_source
);

// 2. Prepare request
MyRequest request;
request.set_field("value");
auto request_buffer = Grpc::Common::serializeToGrpcFrame(request);

// 3. Send request
grpc_client->sendRaw(
    "package.ServiceName",           // Service
    "MethodName",                    // Method
    std::move(request_buffer),       // Request
    *this,                           // Callbacks
    parent_span,                     // Tracing
    options                          // Options (timeout, retry, etc.)
);

// 4. Receive response
void onSuccess(const Request&, Buffer::InstancePtr&& response) {
    MyResponse response_proto;
    Grpc::Common::parseBufferInstance(std::move(response), response_proto);
    // Process response
}

void onFailure(const Request&, Grpc::Status::GrpcStatus status, const std::string& message) {
    // Handle error
}
```

### Streaming RPC

```cpp
// 1. Start stream
auto* stream = grpc_client->startRaw(
    "package.ServiceName",
    "StreamingMethod",
    *this,  // Callbacks
    options
);

// 2. Send messages
MyRequest request1;
request1.set_data("chunk1");
stream->sendMessageRaw(
    Grpc::Common::serializeToGrpcFrame(request1),
    false  // Not end_stream
);

MyRequest request2;
request2.set_data("chunk2");
stream->sendMessageRaw(
    Grpc::Common::serializeToGrpcFrame(request2),
    true  // End stream
);

// 3. Receive messages
bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
    MyResponse response_proto;
    Grpc::Common::parseBufferInstance(std::move(response), response_proto);
    // Process response
    return true;  // Continue receiving
}

void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string& message) override {
    // Stream completed
}
```

---

## gRPC Frame Format

### Frame Structure

```
┌─────────────────────────────────────────────────────┐
│  Flags (1 byte)  │  Length (4 bytes, big-endian)   │
├─────────────────────────────────────────────────────┤
│                                                      │
│              Message Payload (Length bytes)          │
│                                                      │
└─────────────────────────────────────────────────────┘
```

### Flags

```cpp
const uint8_t GRPC_FH_DEFAULT = 0b0;      // Uncompressed
const uint8_t GRPC_FH_COMPRESSED = 0b1;   // Compressed (gzip)
const uint8_t CONNECT_FH_EOS = 0b10;      // End-of-stream (Connect protocol)
```

### Example Frame

```
Request: { "name": "Alice", "age": 30 }

Protobuf serialized: [10 bytes]
Frame:
  [0x00]              // Flags: uncompressed
  [0x00, 0x00, 0x00, 0x0A]  // Length: 10 bytes (big-endian)
  [... 10 bytes ...]  // Protobuf payload
```

---

## gRPC Headers

### Request Headers

```
:method: POST
:path: /package.ServiceName/MethodName
:scheme: https
:authority: api.example.com
content-type: application/grpc
grpc-timeout: 1000m          # 1000 milliseconds
grpc-encoding: gzip          # Optional compression
te: trailers                 # Required for HTTP/2
```

### Response Headers

```
:status: 200
content-type: application/grpc
grpc-encoding: gzip          # If response is compressed
```

### Response Trailers

```
grpc-status: 0               # 0 = OK
grpc-message: Success        # Optional message
grpc-status-details-bin: ... # Optional rich status (base64 encoded)
```

---

## Status Codes

### gRPC Status → HTTP Status Mapping

| gRPC Status | Code | HTTP Status | Meaning |
|-------------|------|-------------|---------|
| OK | 0 | 200 | Success |
| CANCELLED | 1 | 499 | Client cancelled |
| UNKNOWN | 2 | 500 | Unknown error |
| INVALID_ARGUMENT | 3 | 400 | Invalid request |
| DEADLINE_EXCEEDED | 4 | 504 | Timeout |
| NOT_FOUND | 5 | 404 | Not found |
| ALREADY_EXISTS | 6 | 409 | Conflict |
| PERMISSION_DENIED | 7 | 403 | Forbidden |
| RESOURCE_EXHAUSTED | 8 | 429 | Too many requests |
| FAILED_PRECONDITION | 9 | 400 | Precondition failed |
| ABORTED | 10 | 409 | Aborted |
| OUT_OF_RANGE | 11 | 400 | Out of range |
| UNIMPLEMENTED | 12 | 501 | Not implemented |
| INTERNAL | 13 | 500 | Internal error |
| UNAVAILABLE | 14 | 503 | Service unavailable |
| DATA_LOSS | 15 | 500 | Data loss |
| UNAUTHENTICATED | 16 | 401 | Unauthorized |

### Status Handling Example

```cpp
void onFailure(Grpc::Status::GrpcStatus status, const std::string& message) {
    switch (status) {
    case Grpc::Status::WellKnownGrpcStatus::Ok:
        // Success
        break;

    case Grpc::Status::WellKnownGrpcStatus::Cancelled:
        // Client cancelled - don't retry
        break;

    case Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded:
        // Timeout - maybe retry
        if (should_retry_) {
            retryRequest();
        }
        break;

    case Grpc::Status::WellKnownGrpcStatus::Unavailable:
        // Service down - retry with backoff
        scheduleRetry();
        break;

    case Grpc::Status::WellKnownGrpcStatus::InvalidArgument:
        // Bad request - don't retry
        logError(message);
        break;

    default:
        // Unknown error
        handleError(status, message);
        break;
    }
}
```

---

## Statistics

### Per-Cluster Stats

```
cluster.<cluster_name>.grpc.<service>.<method>.success       # Successful calls
cluster.<cluster_name>.grpc.<service>.<method>.failure       # Failed calls
cluster.<cluster_name>.grpc.<service>.<method>.total         # Total calls
cluster.<cluster_name>.grpc.<service>.<method>.request_message_count   # Messages sent
cluster.<cluster_name>.grpc.<service>.<method>.response_message_count  # Messages received
cluster.<cluster_name>.grpc.<service>.<method>.upstream_rq_time        # Latency histogram
```

### Status Code Stats

```
cluster.<cluster_name>.grpc.<service>.<method>.<status_code>  # Per-status counts
```

**Example**:
```
cluster.backend.grpc.myapp.UserService.GetUser.success: 1234
cluster.backend.grpc.myapp.UserService.GetUser.failure: 45
cluster.backend.grpc.myapp.UserService.GetUser.0: 1234       # OK
cluster.backend.grpc.myapp.UserService.GetUser.14: 45        # UNAVAILABLE
cluster.backend.grpc.myapp.UserService.GetUser.upstream_rq_time: P50=25ms, P95=100ms
```

---

## Configuration

### Envoy gRPC Client

```yaml
grpc_services:
  - envoy_grpc:
      cluster_name: backend_service
      authority: api.example.com  # Optional :authority override

    timeout: 1s                   # Request timeout
    retry_policy:                 # Optional retry policy
      retry_on: "5xx"
      num_retries: 3

    # Optional metadata to add to all requests
    initial_metadata:
      - key: "x-api-key"
        value: "secret123"
```

### Google gRPC Client

```yaml
grpc_services:
  - google_grpc:
      target_uri: api.example.com:443
      stat_prefix: google_grpc

      # Credentials
      channel_credentials:
        ssl_credentials:
          root_certs:
            filename: /etc/ssl/ca.pem

      # gRPC channel args
      channel_args:
        args:
          grpc.max_receive_message_length:
            int_value: 4194304  # 4MB
```

---

## Envoy gRPC vs Google gRPC

### Envoy gRPC (Recommended)

**Pros**:
- ✅ Native integration with Envoy's HTTP/2 stack
- ✅ Uses Envoy's connection pooling
- ✅ Consistent stats and tracing
- ✅ Better resource sharing with Envoy
- ✅ No additional threads

**Cons**:
- ❌ Only supports HTTP/2 transport
- ❌ Limited to features Envoy HTTP/2 provides

**Use when**:
- Making gRPC calls from Envoy filters/extensions
- Need consistent observability with Envoy
- Want to leverage Envoy's connection management

### Google gRPC

**Pros**:
- ✅ Full gRPC feature set
- ✅ Multiple transport options
- ✅ Advanced features (channelz, etc.)
- ✅ C++ gRPC library features

**Cons**:
- ❌ Separate thread pool (resource overhead)
- ❌ Less integrated with Envoy stats/tracing
- ❌ Additional dependencies

**Use when**:
- Need gRPC features not in Envoy gRPC
- Migrating existing Google gRPC code
- Need advanced C++ gRPC features

---

## Common Patterns

### Pattern 1: Unary RPC with Timeout

```cpp
// Set timeout
Http::AsyncClient::RequestOptions options;
options.setTimeout(std::chrono::milliseconds(1000));

// Send with timeout
grpc_client->sendRaw(
    service_name,
    method_name,
    std::move(request),
    *this,
    parent_span,
    options
);
```

### Pattern 2: Streaming RPC with Backpressure

```cpp
class MyStreamHandler : public Grpc::RawAsyncStreamCallbacks {
    void onReceiveInitialMetadata(Http::ResponseHeaderMapPtr&&) override {}

    bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
        if (buffer_full_) {
            // Backpressure: Tell stream to stop sending
            stream_->readDisable(true);
            return false;
        }

        // Process response
        processResponse(std::move(response));
        return true;  // Continue receiving
    }

    void onBufferDrained() {
        // Re-enable reading
        stream_->readDisable(false);
    }

private:
    Grpc::RawAsyncStream* stream_;
    bool buffer_full_{false};
};
```

### Pattern 3: Retry with Exponential Backoff

```cpp
void makeRequestWithRetry() {
    if (retry_count_ > max_retries_) {
        onFinalFailure();
        return;
    }

    grpc_client->sendRaw(
        service_name,
        method_name,
        createRequest(),
        *this,
        parent_span,
        options
    );
}

void onFailure(const Request&, Grpc::Status::GrpcStatus status, const std::string&) {
    if (status == Grpc::Status::WellKnownGrpcStatus::Unavailable ||
        status == Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded) {

        // Exponential backoff: 100ms, 200ms, 400ms, ...
        auto delay = std::chrono::milliseconds(100 * (1 << retry_count_));
        retry_count_++;

        retry_timer_->enableTimer(delay, [this]() {
            makeRequestWithRetry();
        });
    } else {
        // Non-retryable error
        onFinalFailure();
    }
}
```

### Pattern 4: Bidirectional Streaming

```cpp
// Client sends and receives simultaneously
class BidiStreamHandler : public Grpc::RawAsyncStreamCallbacks {
    void start() {
        stream_ = grpc_client->startRaw(
            "myapp.ChatService",
            "Chat",
            *this,
            options
        );

        // Start sending messages
        sendNextMessage();
    }

    void sendNextMessage() {
        if (pending_messages_.empty()) {
            stream_->closeStream();  // No more to send
            return;
        }

        auto msg = pending_messages_.front();
        pending_messages_.pop();

        stream_->sendMessageRaw(
            Grpc::Common::serializeToGrpcFrame(msg),
            false  // More messages coming
        );
    }

    bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
        // Process incoming message while sending
        ChatMessage msg;
        Grpc::Common::parseBufferInstance(std::move(response), msg);
        handleIncomingMessage(msg);
        return true;
    }

    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string&) override {
        // Server closed stream
    }

private:
    Grpc::RawAsyncStream* stream_;
    std::queue<ChatMessage> pending_messages_;
};
```

---

## Debugging

### Enable gRPC Trace Logging

```cpp
// In your code
ENVOY_LOG(trace, "gRPC request: service={} method={}", service, method);
ENVOY_LOG(trace, "gRPC status: {} message={}", status, message);
```

### Check gRPC Stats

```bash
# Query admin interface
curl localhost:9901/stats | grep grpc

# Look for:
# - cluster.<name>.grpc.<service>.<method>.success
# - cluster.<name>.grpc.<service>.<method>.failure
# - cluster.<name>.grpc.<service>.<method>.upstream_rq_time
```

### Inspect gRPC Frames

```bash
# Enable HTTP/2 frame logging
# In envoy config:
admin:
  access_log:
    - name: envoy.access_loggers.file
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: "/dev/stdout"
        log_format:
          text_format: "[%START_TIME%] %PROTOCOL% %GRPC_STATUS%\n"
```

---

## Best Practices

### 1. Set Appropriate Timeouts

```cpp
// Set per-request timeout
options.setTimeout(std::chrono::milliseconds(5000));  // 5 seconds

// Or use grpc-timeout header (parsed by server)
Grpc::Common::toGrpcTimeout(
    std::chrono::milliseconds(5000),
    request_headers
);
```

### 2. Handle All Status Codes

```cpp
void onFailure(Grpc::Status::GrpcStatus status, const std::string& message) {
    // Don't just log - take appropriate action
    switch (status) {
    case Grpc::Status::WellKnownGrpcStatus::Unavailable:
        // Retry with backoff
        break;
    case Grpc::Status::WellKnownGrpcStatus::InvalidArgument:
        // Log and alert - client bug
        break;
    case Grpc::Status::WellKnownGrpcStatus::Unauthenticated:
        // Refresh credentials
        break;
    // ... handle all cases
    }
}
```

### 3. Use Type-Safe Clients

```cpp
// Instead of raw API:
grpc_client->sendRaw(service, method, buffer, callbacks, ...);

// Use typed client (generates protobuf serialization):
using MyServiceClient = Grpc::AsyncClient<MyRequest, MyResponse>;
auto client = MyServiceClient::create(grpc_client);

client->send(request, *this, options);
// Automatic protobuf serialization/deserialization
```

### 4. Monitor gRPC Health

```
# Alert on high failure rates
grpc_failure_rate =
    cluster.<name>.grpc.<service>.<method>.failure /
    cluster.<name>.grpc.<service>.<method>.total

# Alert if > 5%
```

### 5. Use Streaming for Large Data

```cpp
// Bad: Unary RPC with large message (can cause memory spike)
void downloadFile() {
    auto request = createRequest();
    grpc_client->sendRaw(...);  // Response could be 100MB
}

// Good: Streaming RPC (incremental processing)
void downloadFileStreaming() {
    auto* stream = grpc_client->startRaw(...);

    bool onReceiveMessageRaw(Buffer::InstancePtr&& chunk) {
        processChunk(std::move(chunk));  // Process incrementally
        return true;
    }
}
```

---

## Related Documentation

- **HTTP/2 Implementation**: `/source/common/http/http2/`
- **Connection Pooling**: `/source/common/http/conn_pool_base.md`
- **Async Client**: `/source/common/http/async_client_impl.md`
- **Protobuf Utilities**: `/source/common/protobuf/`

---

## Further Reading

- [gRPC Protocol Specification](https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-HTTP2.md)
- [Envoy gRPC Services](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/core/v3/grpc_service.proto)
- [gRPC Status Codes](https://grpc.github.io/grpc/core/md_doc_statuscodes.html)

---

**Summary**: Envoy's gRPC implementation provides a native, efficient way to make gRPC calls using Envoy's HTTP/2 stack, with full observability and integration with Envoy's connection management.
