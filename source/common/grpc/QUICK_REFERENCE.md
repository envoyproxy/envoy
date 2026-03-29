# gRPC Quick Reference

**Quick guide for common gRPC patterns in Envoy**

## At a Glance

### Creating a Client

```cpp
// Create async gRPC client
auto grpc_client = Grpc::AsyncClientImpl::create(
    grpc_service_config,  // From envoy config
    context               // Factory context
).value();
```

### Unary RPC

```cpp
// Send single request, get single response
grpc_client->sendRaw(
    "package.Service",      // Service name
    "Method",               // Method name
    std::move(request),     // Serialized request
    *this,                  // Callbacks
    parent_span,            // Tracing span
    options                 // Timeout, retry, etc.
);
```

### Streaming RPC

```cpp
// Start bidirectional stream
auto* stream = grpc_client->startRaw(
    "package.Service",
    "StreamMethod",
    *this,                  // Callbacks
    options
);

// Send multiple messages
stream->sendMessageRaw(msg1, false);
stream->sendMessageRaw(msg2, false);
stream->sendMessageRaw(msg3, true);  // end_stream
```

---

## Common Patterns

### Pattern 1: Simple Unary Call

```cpp
class MyFilter : public Grpc::RawAsyncRequestCallbacks {
    void makeCall() {
        MyRequest req;
        req.set_id(123);

        auto buffer = Grpc::Common::serializeToGrpcFrame(req);

        grpc_client_->sendRaw(
            "myapp.UserService",
            "GetUser",
            std::move(buffer),
            *this,
            parent_span_,
            {}
        );
    }

    void onSuccess(const Grpc::AsyncRequest&, Buffer::InstancePtr&& response) override {
        MyResponse resp;
        Grpc::Common::parseBufferInstance(std::move(response), resp);
        // Use resp
    }

    void onFailure(const Grpc::AsyncRequest&, Grpc::Status::GrpcStatus status,
                   const std::string& msg) override {
        // Handle error
    }
};
```

### Pattern 2: With Timeout

```cpp
void makeCallWithTimeout() {
    Http::AsyncClient::RequestOptions options;
    options.setTimeout(std::chrono::milliseconds(5000));  // 5 seconds

    grpc_client_->sendRaw(
        service,
        method,
        std::move(request),
        *this,
        parent_span_,
        options  // Include timeout
    );
}
```

### Pattern 3: Server Streaming

```cpp
class Streamer : public Grpc::RawAsyncStreamCallbacks {
    void start() {
        stream_ = grpc_client_->startRaw(
            "myapp.EventService",
            "Subscribe",
            *this,
            {}
        );

        // Send subscription request
        SubscribeReq req;
        stream_->sendMessageRaw(
            Grpc::Common::serializeToGrpcFrame(req),
            true  // Only sending one message
        );
    }

    bool onReceiveMessageRaw(Buffer::InstancePtr&& msg) override {
        Event event;
        if (Grpc::Common::parseBufferInstance(std::move(msg), event)) {
            handleEvent(event);
            return true;  // Keep receiving
        }
        return false;  // Stop
    }

    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string&) override {
        stream_ = nullptr;
    }

private:
    Grpc::RawAsyncStream* stream_{nullptr};
};
```

### Pattern 4: With Retry

```cpp
void makeCallWithRetry() {
    if (retry_count_ >= max_retries_) {
        onGiveUp();
        return;
    }

    grpc_client_->sendRaw(...);
}

void onFailure(const Grpc::AsyncRequest&, Grpc::Status::GrpcStatus status,
               const std::string&) override {
    if (status == Grpc::Status::WellKnownGrpcStatus::Unavailable ||
        status == Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded) {
        // Retry with exponential backoff
        auto delay = std::chrono::milliseconds(100 * (1 << retry_count_++));
        timer_->enableTimer(delay, [this]() { makeCallWithRetry(); });
    } else {
        // Non-retryable
        onGiveUp();
    }
}
```

### Pattern 5: Bidirectional Streaming

```cpp
class Chat : public Grpc::RawAsyncStreamCallbacks {
    void start() {
        stream_ = grpc_client_->startRaw("myapp.Chat", "Chat", *this, {});
    }

    void sendMessage(const std::string& text) {
        ChatMsg msg;
        msg.set_text(text);
        stream_->sendMessageRaw(
            Grpc::Common::serializeToGrpcFrame(msg),
            false  // More to come
        );
    }

    void endChat() {
        ChatMsg goodbye;
        goodbye.set_text("bye");
        stream_->sendMessageRaw(
            Grpc::Common::serializeToGrpcFrame(goodbye),
            true  // End stream
        );
    }

    bool onReceiveMessageRaw(Buffer::InstancePtr&& msg) override {
        ChatMsg chat;
        if (Grpc::Common::parseBufferInstance(std::move(msg), chat)) {
            displayMessage(chat.text());
        }
        return true;
    }

    void onRemoteClose(Grpc::Status::GrpcStatus, const std::string&) override {
        stream_ = nullptr;
    }

private:
    Grpc::RawAsyncStream* stream_{nullptr};
};
```

---

## Frame Encoding/Decoding

### Encode Message

```cpp
// Serialize protobuf message to gRPC frame
Buffer::InstancePtr buffer = Grpc::Common::serializeToGrpcFrame(message);

// Manual encoding
Buffer::OwnedImpl buffer;
std::string serialized;
message.SerializeToString(&serialized);
buffer.add(serialized);

Grpc::Encoder encoder;
encoder.prependFrameHeader(Grpc::GRPC_FH_DEFAULT, buffer);
```

### Decode Messages

```cpp
void onData(Buffer::Instance& data, bool end_stream) {
    std::vector<Grpc::Frame> frames;
    auto status = decoder_.decode(data, frames);

    if (!status.ok()) {
        handleError();
        return;
    }

    for (auto& frame : frames) {
        MyMessage msg;
        msg.ParseFromArray(
            frame.data_->linearize(frame.length_),
            frame.length_
        );
        processMessage(msg);
    }
}
```

---

## Status Codes

### Common Status Codes

| Code | Meaning | Retry? | Use Case |
|------|---------|--------|----------|
| 0 (OK) | Success | N/A | Request succeeded |
| 1 (CANCELLED) | Cancelled | No | Client cancelled |
| 3 (INVALID_ARGUMENT) | Bad request | No | Fix client code |
| 4 (DEADLINE_EXCEEDED) | Timeout | Maybe | Increase timeout or retry |
| 5 (NOT_FOUND) | Not found | No | Resource doesn't exist |
| 7 (PERMISSION_DENIED) | Forbidden | No | Fix permissions |
| 8 (RESOURCE_EXHAUSTED) | Rate limited | Yes | Back off |
| 12 (UNIMPLEMENTED) | Not implemented | No | Method doesn't exist |
| 13 (INTERNAL) | Server error | Maybe | Log and alert |
| 14 (UNAVAILABLE) | Service down | Yes | Retry with backoff |
| 16 (UNAUTHENTICATED) | Unauthorized | No | Fix credentials |

### Handling Status

```cpp
void onFailure(const Grpc::AsyncRequest&,
               Grpc::Status::GrpcStatus status,
               const std::string& msg) override {
    using Status = Grpc::Status::WellKnownGrpcStatus;

    switch (status) {
    case Status::Ok:
        break;
    case Status::Cancelled:
        cleanup();
        break;
    case Status::InvalidArgument:
    case Status::FailedPrecondition:
        logClientBug(msg);
        break;
    case Status::DeadlineExceeded:
    case Status::Unavailable:
        retry();
        break;
    case Status::ResourceExhausted:
        backoff();
        break;
    case Status::Internal:
    case Status::Unknown:
        alertOncall(msg);
        break;
    default:
        handleUnknown(status, msg);
        break;
    }
}
```

---

## Headers

### Request Headers

```
:method: POST
:path: /package.Service/Method
:scheme: https
:authority: api.example.com
content-type: application/grpc
grpc-timeout: 5000m
grpc-encoding: gzip
te: trailers
```

### Response Headers

```
:status: 200
content-type: application/grpc
grpc-encoding: gzip
```

### Trailers

```
grpc-status: 0
grpc-message: Success
grpc-status-details-bin: <base64>
```

### Setting Timeout

```cpp
// Option 1: In RequestOptions
Http::AsyncClient::RequestOptions options;
options.setTimeout(std::chrono::milliseconds(5000));

// Option 2: In headers (server reads this)
Http::RequestHeaderMap& headers = ...;
Grpc::Common::toGrpcTimeout(std::chrono::milliseconds(5000), headers);
```

---

## Configuration

### Envoy gRPC

```yaml
grpc_services:
  - envoy_grpc:
      cluster_name: backend_service
      authority: api.example.com

    timeout: 5s

    retry_policy:
      retry_on: "5xx"
      num_retries: 3

    initial_metadata:
      - key: "x-api-key"
        value: "secret"
```

### Google gRPC

```yaml
grpc_services:
  - google_grpc:
      target_uri: api.example.com:443
      stat_prefix: ext_authz

      channel_credentials:
        ssl_credentials:
          root_certs:
            filename: /etc/ssl/ca.pem

      channel_args:
        args:
          grpc.max_receive_message_length:
            int_value: 4194304
```

---

## Statistics

### Per-Method Stats

```
cluster.<cluster>.grpc.<service>.<method>.success
cluster.<cluster>.grpc.<service>.<method>.failure
cluster.<cluster>.grpc.<service>.<method>.total
cluster.<cluster>.grpc.<service>.<method>.0         # OK count
cluster.<cluster>.grpc.<service>.<method>.14        # UNAVAILABLE count
cluster.<cluster>.grpc.<service>.<method>.upstream_rq_time
cluster.<cluster>.grpc.<service>.<method>.request_message_count
cluster.<cluster>.grpc.<service>.<method>.response_message_count
```

### Example

```
cluster.backend.grpc.myapp.UserService.GetUser.success: 1234
cluster.backend.grpc.myapp.UserService.GetUser.failure: 56
cluster.backend.grpc.myapp.UserService.GetUser.0: 1234
cluster.backend.grpc.myapp.UserService.GetUser.14: 45
cluster.backend.grpc.myapp.UserService.GetUser.4: 11
cluster.backend.grpc.myapp.UserService.GetUser.upstream_rq_time: P50=25ms, P95=100ms
```

---

## Utilities

### Serialize to gRPC Frame

```cpp
Buffer::InstancePtr buffer = Grpc::Common::serializeToGrpcFrame(message);
```

### Parse from Buffer

```cpp
MyMessage message;
bool success = Grpc::Common::parseBufferInstance(std::move(buffer), message);
```

### Prepare Headers

```cpp
Http::RequestMessagePtr headers = Grpc::Common::prepareHeaders(
    "backend_cluster",
    "package.Service",
    "Method",
    std::chrono::milliseconds(5000)  // timeout
);
```

### Extract Service/Method

```cpp
auto names = Grpc::Common::resolveServiceAndMethod(path_header);
if (names.has_value()) {
    absl::string_view service = names->service_;
    absl::string_view method = names->method_;
}
```

### Check if gRPC Request

```cpp
bool is_grpc = Grpc::Common::isGrpcRequestHeaders(headers);
```

---

## Debugging

### Enable Logging

```cpp
ENVOY_LOG(debug, "gRPC request: service={} method={}", service, method);
ENVOY_LOG(debug, "gRPC status: {} message={}", status, message);
```

### Query Stats

```bash
curl localhost:9901/stats | grep grpc

# Look for:
# - cluster.<name>.grpc.<service>.<method>.success
# - cluster.<name>.grpc.<service>.<method>.failure
# - cluster.<name>.grpc.<service>.<method>.upstream_rq_time
```

### Inspect Frames

```bash
# Enable detailed HTTP/2 logging
# Set log level to trace in config
```

---

## Common Pitfalls

### ❌ Not Handling All Status Codes

```cpp
// Bad
void onFailure(..., Grpc::Status::GrpcStatus status, const std::string&) {
    logError();  // That's it?
}

// Good
void onFailure(..., Grpc::Status::GrpcStatus status, const std::string&) {
    switch (status) {
        case Grpc::Status::WellKnownGrpcStatus::Unavailable:
            retry();
            break;
        // ... handle all relevant cases
    }
}
```

### ❌ Forgetting to Cancel Requests

```cpp
// Bad
~MyFilter() {}  // Active request leaks

// Good
~MyFilter() {
    if (active_request_) {
        active_request_->cancel();
    }
}
```

### ❌ No Timeout

```cpp
// Bad
grpc_client_->sendRaw(..., {});  // No timeout - can hang forever

// Good
Http::AsyncClient::RequestOptions options;
options.setTimeout(std::chrono::seconds(10));
grpc_client_->sendRaw(..., options);
```

### ❌ Ignoring Backpressure

```cpp
// Bad
bool onReceiveMessageRaw(Buffer::InstancePtr&& msg) override {
    queue_.push(std::move(msg));  // Unbounded queue
    return true;  // Always accept
}

// Good
bool onReceiveMessageRaw(Buffer::InstancePtr&& msg) override {
    if (queue_.size() >= max_size_) {
        return false;  // Stop receiving
    }
    queue_.push(std::move(msg));
    return true;
}
```

### ❌ Retrying Non-Retryable Errors

```cpp
// Bad
void onFailure(..., Grpc::Status::GrpcStatus status, ...) {
    retry();  // Always retry?
}

// Good
void onFailure(..., Grpc::Status::GrpcStatus status, ...) {
    if (status == Grpc::Status::WellKnownGrpcStatus::Unavailable) {
        retry();  // Only retry transient errors
    } else {
        giveUp();
    }
}
```

---

## Checklist

### Before Deploying gRPC Code

- [ ] Set appropriate timeout
- [ ] Handle all relevant status codes
- [ ] Cancel requests in destructor
- [ ] Implement retry logic for transient errors
- [ ] Use exponential backoff for retries
- [ ] Handle backpressure for streaming
- [ ] Add distributed tracing
- [ ] Monitor gRPC stats
- [ ] Test with network failures
- [ ] Test with slow/unresponsive server

---

## Quick Command Reference

```bash
# Check gRPC stats
curl localhost:9901/stats | grep "grpc\..*\.success"

# Check error rate
curl localhost:9901/stats | grep "grpc\..*\.failure"

# Check latency
curl localhost:9901/stats | grep "grpc\..*\.upstream_rq_time"

# Check specific method
curl localhost:9901/stats | grep "grpc\.myapp\.UserService\.GetUser"
```

---

## Related Documentation

- **Detailed Async Client**: [async_client_impl.md](./async_client_impl.md)
- **Frame Encoding/Decoding**: [codec.md](./codec.md)
- **Full Overview**: [README.md](./README.md)

---

## Examples by Use Case

| Use Case | Example | Link |
|----------|---------|------|
| Simple unary call | Pattern 1 | [↑](#pattern-1-simple-unary-call) |
| With timeout | Pattern 2 | [↑](#pattern-2-with-timeout) |
| Server streaming | Pattern 3 | [↑](#pattern-3-server-streaming) |
| With retry | Pattern 4 | [↑](#pattern-4-with-retry) |
| Bidirectional | Pattern 5 | [↑](#pattern-5-bidirectional-streaming) |

---

**Tip**: Start with Pattern 1 (simple unary call) and add features as needed.
