# gRPC Async Client Implementation

**File**: `async_client_impl.h/cc`
**Purpose**: Async gRPC client built on top of HTTP/2 AsyncClient

## Overview

`AsyncClientImpl` provides:
- **Unary RPCs** - Single request, single response
- **Streaming RPCs** - Client streaming, server streaming, bidirectional
- **Protocol handling** - gRPC frame encoding/decoding
- **Status management** - gRPC status to HTTP status mapping
- **Metadata support** - Initial metadata and trailers

## Architecture

```
┌──────────────────────────────────────────────────────┐
│             AsyncClientImpl                           │
│  ┌────────────────────────────────────────────────┐  │
│  │  sendRaw() - Unary RPC                         │  │
│  │    → Creates AsyncRequestImpl                  │  │
│  │    → Single request/response                   │  │
│  └────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────┐  │
│  │  startRaw() - Streaming RPC                    │  │
│  │    → Creates AsyncStreamImpl                   │  │
│  │    → Multiple requests/responses               │  │
│  └────────────────────────────────────────────────┘  │
│                         ↓                              │
│  ┌────────────────────────────────────────────────┐  │
│  │      Http::AsyncClient (HTTP/2)                │  │
│  │  - Stream management                           │  │
│  │  - Header/data encoding                        │  │
│  └────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────┘
```

## Key Classes

### AsyncClientImpl

Main client interface:

```cpp
class AsyncClientImpl : public RawAsyncClient {
public:
    // Create client
    static absl::StatusOr<std::unique_ptr<AsyncClientImpl>> create(
        const envoy::config::core::v3::GrpcService& config,
        Server::Configuration::CommonFactoryContext& context
    );

    // Unary RPC
    AsyncRequest* sendRaw(
        absl::string_view service_full_name,    // e.g., "myapp.UserService"
        absl::string_view method_name,          // e.g., "GetUser"
        Buffer::InstancePtr&& request,          // Serialized protobuf
        RawAsyncRequestCallbacks& callbacks,    // Result callbacks
        Tracing::Span& parent_span,            // Distributed tracing
        const Http::AsyncClient::RequestOptions& options  // Timeout, retry, etc.
    ) override;

    // Streaming RPC
    RawAsyncStream* startRaw(
        absl::string_view service_full_name,
        absl::string_view method_name,
        RawAsyncStreamCallbacks& callbacks,
        const Http::AsyncClient::StreamOptions& options
    ) override;

    // Get destination cluster
    absl::string_view destination() override { return remote_cluster_name_; }

private:
    const uint32_t max_recv_message_length_;
    const bool skip_envoy_headers_;
    Upstream::ClusterManager& cm_;
    const std::string remote_cluster_name_;
    const std::string host_name_;
    std::list<AsyncStreamImplPtr> active_streams_;
    Router::RetryPolicyConstSharedPtr retry_policy_;
};
```

### AsyncStreamImpl

Manages a gRPC stream (wraps HTTP/2 stream):

```cpp
class AsyncStreamImpl : public RawAsyncStream,
                        public Http::AsyncClient::StreamCallbacks {
public:
    // Constructor
    AsyncStreamImpl(
        AsyncClientImpl& parent,
        absl::string_view service_full_name,
        absl::string_view method_name,
        RawAsyncStreamCallbacks& callbacks,
        const Http::AsyncClient::StreamOptions& options
    );

    // Send gRPC message
    void sendMessageRaw(Buffer::InstancePtr&& request, bool end_stream) override;

    // Close stream gracefully
    void closeStream() override;

    // Reset stream (abort)
    void resetStream() override;

    // HTTP/2 callbacks
    void onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void onData(Buffer::Instance& data, bool end_stream) override;
    void onTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
    void onComplete() override;
    void onReset() override;

    // Backpressure
    bool isAboveWriteBufferHighWatermark() const override;
    void setWatermarkCallbacks(Http::SidestreamWatermarkCallbacks& callbacks) override;

    // Stream info
    const StreamInfo::StreamInfo& streamInfo() const override;

protected:
    AsyncClientImpl& parent_;
    std::string service_full_name_;
    std::string method_name_;
    RawAsyncStreamCallbacks& callbacks_;
    Http::AsyncClient::Stream* stream_;  // Underlying HTTP/2 stream
    Decoder decoder_;                     // gRPC frame decoder
    std::vector<Frame> decoded_frames_;   // Decoded messages
    bool http_reset_{false};
};
```

### AsyncRequestImpl

Specialization for unary RPCs:

```cpp
class AsyncRequestImpl : public AsyncRequest,
                         public AsyncStreamImpl,
                         public RawAsyncStreamCallbacks {
public:
    AsyncRequestImpl(
        AsyncClientImpl& parent,
        absl::string_view service_full_name,
        absl::string_view method_name,
        Buffer::InstancePtr&& request,          // Single request
        RawAsyncRequestCallbacks& callbacks,    // Unary callbacks
        Tracing::Span& parent_span,
        const Http::AsyncClient::RequestOptions& options
    );

    // Cancel request
    void cancel() override;

    // Detach (continue in background)
    void detach() override;

private:
    Buffer::InstancePtr request_;               // Request message
    RawAsyncRequestCallbacks& callbacks_;       // Unary callbacks
    Buffer::InstancePtr response_;              // Accumulated response
};
```

## Request Lifecycle

### Unary RPC

```
1. Client calls sendRaw()
   ↓
2. AsyncRequestImpl created
   ↓
3. HTTP/2 stream created
   ↓
4. Request headers sent
   :method: POST
   :path: /package.Service/Method
   content-type: application/grpc
   ↓
5. Request body sent (gRPC framed)
   [5-byte frame header][protobuf payload]
   ↓
6. Response headers received
   :status: 200
   content-type: application/grpc
   ↓
7. Response body received (gRPC framed)
   [frame header][protobuf payload]
   ↓
8. Response trailers received
   grpc-status: 0
   grpc-message: OK
   ↓
9. Callback invoked
   callbacks.onSuccess(request, response)
```

### Streaming RPC

```
1. Client calls startRaw()
   ↓
2. AsyncStreamImpl created
   ↓
3. HTTP/2 stream created
   ↓
4. Request headers sent
   ↓
5. Client sends multiple messages
   stream->sendMessageRaw(msg1, false)
   stream->sendMessageRaw(msg2, false)
   stream->sendMessageRaw(msg3, true)  // end_stream
   ↓
6. Server sends multiple responses
   callbacks.onReceiveMessageRaw(response1)
   callbacks.onReceiveMessageRaw(response2)
   callbacks.onReceiveMessageRaw(response3)
   ↓
7. Trailers received
   grpc-status: 0
   ↓
8. Stream closed
   callbacks.onRemoteClose(status, message)
```

## Implementation Details

### Header Preparation

```cpp
void AsyncStreamImpl::initialize(bool buffer_body_for_retry) {
    // Create HTTP/2 request headers
    auto headers = Http::RequestMessageImpl::create();

    // Set pseudo-headers
    headers->headers().setMethod("POST");
    headers->headers().setPath(
        absl::StrCat("/", service_full_name_, "/", method_name_)
    );

    // Set gRPC headers
    headers->headers().setReferenceKey(
        Http::Headers::get().ContentType,
        Http::Headers::get().ContentTypeValues.Grpc
    );

    // Optional: Set timeout
    if (options_.timeout.has_value()) {
        Grpc::Common::toGrpcTimeout(
            options_.timeout.value(),
            headers->headers()
        );
    }

    // Optional: Set compression
    if (options_.compression_enabled) {
        headers->headers().setReference(
            Http::CustomHeaders::get().GrpcEncoding,
            "gzip"
        );
    }

    // Start HTTP/2 stream
    stream_ = parent_.http_async_client_.start(
        *this,  // StreamCallbacks
        options_
    );

    // Send headers
    stream_->sendHeaders(headers->headers(), !has_request_body);
}
```

### Message Encoding

```cpp
void AsyncStreamImpl::sendMessageRaw(
    Buffer::InstancePtr&& request,
    bool end_stream
) {
    // Prepend gRPC frame header (5 bytes)
    // [flags][length as 4-byte big-endian]
    Encoder encoder;
    encoder.prependFrameHeader(GRPC_FH_DEFAULT, *request);

    // Send via HTTP/2 stream
    stream_->sendData(*request, end_stream);

    // request is drained (moved into HTTP/2 buffer)
}
```

### Message Decoding

```cpp
void AsyncStreamImpl::onData(
    Buffer::Instance& data,
    bool end_stream
) {
    // Decode gRPC frames
    decoded_frames_.clear();
    auto status = decoder_.decode(data, decoded_frames_);

    if (!status.ok()) {
        // Frame parsing error
        streamError(Grpc::Status::WellKnownGrpcStatus::Internal,
                   "gRPC frame decode error");
        return;
    }

    // Process each decoded frame
    for (auto& frame : decoded_frames_) {
        // Check for oversized messages
        if (frame.length_ > max_recv_message_length_) {
            streamError(Grpc::Status::WellKnownGrpcStatus::ResourceExhausted,
                       "gRPC message exceeds maximum size");
            return;
        }

        // Invoke callback with message
        bool continue_reading = callbacks_.onReceiveMessageRaw(
            std::move(frame.data_)
        );

        if (!continue_reading) {
            // Backpressure - stop reading
            stream_->readDisable(true);
            break;
        }
    }
}
```

### Status Extraction

```cpp
void AsyncStreamImpl::onTrailers(Http::ResponseTrailerMapPtr&& trailers) {
    // Extract grpc-status
    auto grpc_status = Grpc::Common::getGrpcStatus(*trailers);

    // Extract grpc-message
    auto grpc_message = Grpc::Common::getGrpcMessage(*trailers);

    // Extract grpc-status-details-bin (rich status)
    auto status_details = Grpc::Common::getGrpcStatusDetailsBin(*trailers);

    // Notify callback
    if (grpc_status.has_value()) {
        callbacks_.onRemoteClose(grpc_status.value(), grpc_message);
    } else {
        // No grpc-status - map from HTTP status
        auto http_status = response_headers_->getStatusValue();
        auto mapped_status = Grpc::Utility::httpToGrpcStatus(http_status);
        callbacks_.onRemoteClose(mapped_status, "");
    }
}
```

## Usage Examples

### Example 1: Unary RPC

```cpp
class MyFilter : public Http::StreamDecoderFilter,
                 public Grpc::RawAsyncRequestCallbacks {
public:
    void makeGrpcCall() {
        // Create request
        MyRequest request;
        request.set_user_id(123);

        // Serialize to buffer
        auto request_buffer = Grpc::Common::serializeToGrpcFrame(request);

        // Send
        active_request_ = grpc_client_->sendRaw(
            "myapp.UserService",     // Service
            "GetUser",               // Method
            std::move(request_buffer),
            *this,                   // Callbacks
            parent_span_,
            options_
        );
    }

    // Success callback
    void onSuccess(
        const Grpc::AsyncRequest& request,
        Buffer::InstancePtr&& response
    ) override {
        // Deserialize response
        MyResponse response_proto;
        if (Grpc::Common::parseBufferInstance(std::move(response), response_proto)) {
            // Process response
            ENVOY_LOG(info, "User name: {}", response_proto.name());
        }
        active_request_ = nullptr;
    }

    // Failure callback
    void onFailure(
        const Grpc::AsyncRequest& request,
        Grpc::Status::GrpcStatus status,
        const std::string& message
    ) override {
        ENVOY_LOG(error, "gRPC failed: status={} message={}", status, message);
        active_request_ = nullptr;
    }

private:
    Grpc::AsyncClientPtr grpc_client_;
    Grpc::AsyncRequest* active_request_{nullptr};
};
```

### Example 2: Server Streaming RPC

```cpp
class StreamingHandler : public Grpc::RawAsyncStreamCallbacks {
public:
    void startStream() {
        stream_ = grpc_client_->startRaw(
            "myapp.EventService",
            "SubscribeEvents",
            *this,
            options_
        );

        // Send subscription request
        SubscribeRequest request;
        request.set_topic("notifications");

        stream_->sendMessageRaw(
            Grpc::Common::serializeToGrpcFrame(request),
            true  // end_stream (no more requests)
        );
    }

    // Callback for each response message
    bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
        Event event;
        if (!Grpc::Common::parseBufferInstance(std::move(response), event)) {
            return false;  // Stop receiving
        }

        // Process event
        handleEvent(event);

        return true;  // Continue receiving
    }

    // Stream closed
    void onRemoteClose(
        Grpc::Status::GrpcStatus status,
        const std::string& message
    ) override {
        if (status == Grpc::Status::WellKnownGrpcStatus::Ok) {
            ENVOY_LOG(info, "Stream completed successfully");
        } else {
            ENVOY_LOG(error, "Stream error: {}", message);
        }
        stream_ = nullptr;
    }

private:
    Grpc::RawAsyncStream* stream_{nullptr};
};
```

### Example 3: Bidirectional Streaming

```cpp
class ChatHandler : public Grpc::RawAsyncStreamCallbacks {
public:
    void startChat() {
        stream_ = grpc_client_->startRaw(
            "myapp.ChatService",
            "Chat",
            *this,
            options_
        );

        // Start sending messages
        sendNextMessage();
    }

    void sendMessage(const std::string& text) {
        ChatMessage msg;
        msg.set_text(text);
        msg.set_timestamp(getCurrentTime());

        stream_->sendMessageRaw(
            Grpc::Common::serializeToGrpcFrame(msg),
            false  // More messages coming
        );
    }

    void endChat() {
        // Send empty message with end_stream
        ChatMessage goodbye;
        goodbye.set_text("Goodbye");

        stream_->sendMessageRaw(
            Grpc::Common::serializeToGrpcFrame(goodbye),
            true  // End client stream
        );
    }

    // Receive messages from server
    bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
        ChatMessage msg;
        if (Grpc::Common::parseBufferInstance(std::move(response), msg)) {
            displayMessage(msg.text());
        }
        return true;
    }

    void onRemoteClose(Grpc::Status::GrpcStatus status, const std::string&) override {
        // Server ended chat
        stream_ = nullptr;
    }

private:
    Grpc::RawAsyncStream* stream_{nullptr};
};
```

## Error Handling

### Common Error Patterns

```cpp
void onFailure(
    const Grpc::AsyncRequest& request,
    Grpc::Status::GrpcStatus status,
    const std::string& message
) override {
    switch (status) {
    case Grpc::Status::WellKnownGrpcStatus::Cancelled:
        // Client cancelled - clean up
        cleanup();
        break;

    case Grpc::Status::WellKnownGrpcStatus::DeadlineExceeded:
        // Timeout - maybe retry with longer timeout
        if (should_retry_) {
            retryWithLongerTimeout();
        }
        break;

    case Grpc::Status::WellKnownGrpcStatus::Unavailable:
        // Service down - retry with backoff
        scheduleRetry();
        break;

    case Grpc::Status::WellKnownGrpcStatus::ResourceExhausted:
        // Rate limited - back off
        backoff();
        break;

    case Grpc::Status::WellKnownGrpcStatus::InvalidArgument:
    case Grpc::Status::WellKnownGrpcStatus::FailedPrecondition:
        // Client error - don't retry
        logClientError(message);
        break;

    case Grpc::Status::WellKnownGrpcStatus::Internal:
    case Grpc::Status::WellKnownGrpcStatus::Unknown:
        // Server error - log and alert
        logServerError(message);
        break;

    default:
        handleUnknownError(status, message);
        break;
    }
}
```

## Best Practices

### 1. Always Handle Cleanup

```cpp
class MyHandler : public Grpc::RawAsyncRequestCallbacks {
    ~MyHandler() override {
        // Cancel pending request
        if (active_request_) {
            active_request_->cancel();
            active_request_ = nullptr;
        }
    }
};
```

### 2. Set Appropriate Timeouts

```cpp
Http::AsyncClient::RequestOptions options;
options.setTimeout(std::chrono::seconds(10));  // 10 second timeout

// Send request with timeout
grpc_client_->sendRaw(..., options);
```

### 3. Implement Backpressure for Streaming

```cpp
bool onReceiveMessageRaw(Buffer::InstancePtr&& response) override {
    if (pending_messages_.size() > max_buffer_size_) {
        // Too many pending - stop receiving temporarily
        return false;
    }

    pending_messages_.push(std::move(response));
    return true;
}

void onMessagesProcessed() {
    // Re-enable reading
    if (stream_) {
        stream_->readDisable(false);
    }
}
```

### 4. Use Tracing

```cpp
// Pass parent span for distributed tracing
grpc_client_->sendRaw(
    service_name,
    method_name,
    std::move(request),
    *this,
    parent_span_,  // Important for tracing
    options
);
```

## Summary

`AsyncClientImpl` provides:
- **Full gRPC support** over HTTP/2
- **Unary and streaming** RPC patterns
- **Automatic framing** and protocol handling
- **Status mapping** between gRPC and HTTP
- **Integrated tracing** and statistics

**Key Point**: Built on Envoy's HTTP/2 stack, providing native integration with Envoy's connection pooling, stats, and observability.
