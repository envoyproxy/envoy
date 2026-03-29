# Envoy HTTP Interfaces Documentation

**Location**: `/envoy/http/`
**Purpose**: Core HTTP processing interfaces for Envoy proxy

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Core Interfaces](#core-interfaces)
  - [HTTP Filters](#http-filters)
  - [HTTP Codecs](#http-codecs)
  - [Header Maps](#header-maps)
  - [Async Client](#async-client)
  - [Connection Pool](#connection-pool)
  - [HTTP Messages](#http-messages)
  - [Status Codes](#status-codes)
- [Request/Response Flow](#requestresponse-flow)
- [Key Patterns](#key-patterns)
- [Usage Examples](#usage-examples)

---

## Overview

Envoy's HTTP subsystem is built on a flexible, extensible architecture that supports:

- **Multiple HTTP versions**: HTTP/1.0, HTTP/1.1, HTTP/2, HTTP/3
- **Filter chain processing**: Request and response filters for transformation and routing
- **Connection pooling**: Efficient connection management to upstream services
- **Async operations**: Non-blocking HTTP client for internal requests
- **Stream-based processing**: Full-duplex streaming with flow control

### Key Design Principles

1. **Protocol Agnostic**: Filters work uniformly across HTTP/1, HTTP/2, and HTTP/3
2. **Zero-copy**: Headers and data are passed by reference to avoid copying
3. **Streaming**: Support for request/response streaming with backpressure
4. **Extensibility**: Custom filters can be added without modifying core code

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Downstream Request                       │
│                            ↓                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              HTTP Codec (Decoder)                     │   │
│  │  (HTTP/1, HTTP/2, HTTP/3 → Internal representation)  │   │
│  └──────────────────────────────────────────────────────┘   │
│                            ↓                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │                 Filter Chain                          │   │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────┐     │   │
│  │  │  Filter 1  │→ │  Filter 2  │→ │  Router    │     │   │
│  │  │(decode/    │  │(decode/    │  │  Filter    │     │   │
│  │  │ encode)    │  │ encode)    │  │            │     │   │
│  │  └────────────┘  └────────────┘  └────────────┘     │   │
│  └──────────────────────────────────────────────────────┘   │
│                            ↓                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │             Connection Pool                           │   │
│  │  (Manages connections to upstream clusters)          │   │
│  └──────────────────────────────────────────────────────┘   │
│                            ↓                                  │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              HTTP Codec (Encoder)                     │   │
│  │  (Internal representation → HTTP/1, HTTP/2, HTTP/3)  │   │
│  └──────────────────────────────────────────────────────┘   │
│                            ↓                                  │
│                      Upstream Request                         │
└─────────────────────────────────────────────────────────────┘
```

---

## Core Interfaces

### HTTP Filters

**File**: `filter.h` (68KB, 2000+ lines)

HTTP filters are the core extension mechanism in Envoy. They process requests and responses as they flow through the proxy.

#### Filter Return Codes

Filters control the filter chain iteration using return codes:

```cpp
enum class FilterHeadersStatus {
    Continue,        // Continue to next filter
    StopIteration,   // Stop iteration, must call continueDecoding/Encoding later
    StopAllIterationAndBuffer,  // Buffer data until continue is called
    StopAllIterationAndWatermark, // Stop and apply watermark limits
    ContinueAndEndStream  // Mark stream as ended
};

enum class FilterDataStatus {
    Continue,               // Continue to next filter
    StopIterationAndBuffer, // Buffer this data chunk
    StopIterationAndWatermark,
    StopIterationNoBuffer   // Don't buffer, just stop
};

enum class FilterTrailersStatus {
    Continue,
    StopIteration
};
```

**Key Pattern**: `StopIteration` allows filters to pause processing (e.g., to make an async call) and resume later with `continueDecoding()` or `continueEncoding()`.

#### StreamDecoderFilter Interface

Processes **downstream → upstream** (request) flow:

```cpp
class StreamDecoderFilter {
public:
    virtual ~StreamDecoderFilter() = default;

    // Called when request headers are received
    virtual FilterHeadersStatus decodeHeaders(
        RequestHeaderMap& headers,
        bool end_stream
    ) PURE;

    // Called when request data is received (may be called multiple times)
    virtual FilterDataStatus decodeData(
        Buffer::Instance& data,
        bool end_stream
    ) PURE;

    // Called when request trailers are received
    virtual FilterTrailersStatus decodeTrailers(
        RequestTrailerMap& trailers
    ) PURE;

    // Called when the downstream is ready to receive more data
    virtual void decodeComplete() PURE;
};
```

#### StreamEncoderFilter Interface

Processes **upstream → downstream** (response) flow:

```cpp
class StreamEncoderFilter {
public:
    virtual ~StreamEncoderFilter() = default;

    // Called when 1xx informational headers are received
    virtual Filter1xxHeadersStatus encode1xxHeaders(
        ResponseHeaderMap& headers
    ) PURE;

    // Called when response headers are received
    virtual FilterHeadersStatus encodeHeaders(
        ResponseHeaderMap& headers,
        bool end_stream
    ) PURE;

    // Called when response data is received (may be called multiple times)
    virtual FilterDataStatus encodeData(
        Buffer::Instance& data,
        bool end_stream
    ) PURE;

    // Called when response trailers are received
    virtual FilterTrailersStatus encodeTrailers(
        ResponseTrailerMap& trailers
    ) PURE;

    // Called when the upstream is ready to receive more data
    virtual void encodeComplete() PURE;
};
```

#### StreamFilter Interface

Most filters implement both encoder and decoder interfaces:

```cpp
class StreamFilter : public virtual StreamDecoderFilter,
                     public virtual StreamEncoderFilter {};
```

#### StreamDecoderFilterCallbacks

Filters use callbacks to interact with the filter chain and stream:

```cpp
class StreamDecoderFilterCallbacks {
public:
    // Continue iteration after returning StopIteration
    virtual void continueDecoding() PURE;

    // Access to the request encoder (for modifying upstream request)
    virtual const RequestEncoder& requestEncoder() const PURE;

    // Send local reply (error response without going upstream)
    virtual void sendLocalReply(
        Code code,
        absl::string_view body,
        std::function<void(ResponseHeaderMap&)> modify_headers,
        const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
        absl::string_view details
    ) PURE;

    // Access to stream info for logging/stats
    virtual StreamInfo::StreamInfo& streamInfo() PURE;

    // Access to active span for distributed tracing
    virtual Tracing::Span& activeSpan() PURE;

    // Access to route for routing decisions
    virtual const Router::RouteConstSharedPtr& route() PURE;

    // Disable/enable reading from downstream
    virtual void setDownstreamReadDisable(bool disable) PURE;
};
```

#### StreamEncoderFilterCallbacks

```cpp
class StreamEncoderFilterCallbacks {
public:
    // Continue iteration after returning StopIteration
    virtual void continueEncoding() PURE;

    // Access to the response encoder (for modifying downstream response)
    virtual const ResponseEncoder& responseEncoder() const PURE;

    // Access to stream info
    virtual StreamInfo::StreamInfo& streamInfo() PURE;

    // Disable/enable reading from upstream
    virtual void setUpstreamReadDisable(bool disable) PURE;
};
```

#### Common Filter Patterns

**1. Modifying Headers**

```cpp
FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
    // Add a header
    headers.addCopy(LowerCaseString("x-custom-header"), "value");

    // Modify existing header
    if (auto host = headers.Host(); host != nullptr) {
        host->value("new-host.example.com");
    }

    // Remove a header
    headers.remove(LowerCaseString("x-remove-me"));

    return FilterHeadersStatus::Continue;
}
```

**2. Async Processing with StopIteration**

```cpp
FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
    // Start async operation (e.g., external auth call)
    startAsyncAuth(headers, [this]() {
        // Resume when async operation completes
        decoder_callbacks_->continueDecoding();
    });

    // Stop filter chain until async operation completes
    return FilterHeadersStatus::StopIteration;
}
```

**3. Sending Local Reply**

```cpp
FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
    if (!isAuthorized(headers)) {
        decoder_callbacks_->sendLocalReply(
            Http::Code::Forbidden,
            "Access denied",
            nullptr,  // no header modifications
            absl::nullopt,  // no gRPC status
            "authorization_failed"
        );
        return FilterHeadersStatus::StopIteration;
    }
    return FilterHeadersStatus::Continue;
}
```

**4. Buffering Body Data**

```cpp
FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
    if (needsBodyInspection(headers)) {
        // Buffer all body data before processing
        return FilterHeadersStatus::StopAllIterationAndBuffer;
    }
    return FilterHeadersStatus::Continue;
}

FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    if (end_stream) {
        // All data buffered, now inspect it
        inspectBody(data);
        return FilterDataStatus::Continue;
    }
    return FilterDataStatus::StopIterationAndBuffer;
}
```

---

### HTTP Codecs

**File**: `codec.h` (684 lines)

Codecs handle protocol-specific encoding/decoding between wire format and Envoy's internal representation.

#### Supported Protocols

```cpp
enum class CodecType {
    HTTP1,  // HTTP/1.0, HTTP/1.1
    HTTP2,  // HTTP/2
    HTTP3   // HTTP/3 (QUIC)
};
```

#### Stream Encoder

Encodes HTTP messages for transmission:

```cpp
class StreamEncoder {
public:
    virtual ~StreamEncoder() = default;

    // Encode a data frame
    virtual void encodeData(Buffer::Instance& data, bool end_stream) PURE;

    // Encode metadata (HTTP/2 and HTTP/3)
    virtual void encodeMetadata(const MetadataMapVector& metadata) PURE;

    // Access to the underlying stream
    virtual Stream& getStream() PURE;

    // HTTP/1-specific options (returns nullopt for HTTP/2/3)
    virtual Http1StreamEncoderOptionsOptRef http1StreamEncoderOptions() PURE;
};
```

#### Request Encoder

Client-side request encoding:

```cpp
class RequestEncoder : public virtual StreamEncoder {
public:
    // Encode request headers (required: method, path, host)
    virtual Status encodeHeaders(
        const RequestHeaderMap& headers,
        bool end_stream
    ) PURE;

    // Encode request trailers
    virtual void encodeTrailers(const RequestTrailerMap& trailers) PURE;

    // Enable TCP tunneling (for CONNECT method)
    virtual void enableTcpTunneling() PURE;
};
```

#### Response Encoder

Server-side response encoding:

```cpp
class ResponseEncoder : public virtual StreamEncoder {
public:
    // Encode 1xx informational headers
    virtual void encode1xxHeaders(const ResponseHeaderMap& headers) PURE;

    // Encode response headers (required: status)
    virtual void encodeHeaders(
        const ResponseHeaderMap& headers,
        bool end_stream
    ) PURE;

    // Encode response trailers
    virtual void encodeTrailers(const ResponseTrailerMap& trailers) PURE;

    // Set new request decoder (for internal redirects)
    virtual void setRequestDecoder(RequestDecoder& decoder) PURE;

    // Error handling mode
    virtual bool streamErrorOnInvalidHttpMessage() const PURE;
};
```

#### Stream Decoder

Decodes incoming HTTP messages:

```cpp
class StreamDecoder {
public:
    virtual ~StreamDecoder() = default;

    // Called when data is decoded
    virtual void decodeData(Buffer::Instance& data, bool end_stream) PURE;

    // Called when metadata is decoded
    virtual void decodeMetadata(MetadataMapPtr&& metadata) PURE;
};
```

#### Request Decoder

Server-side request decoding:

```cpp
class RequestDecoder : public virtual StreamDecoder {
public:
    // Called when request headers are decoded
    virtual void decodeHeaders(
        RequestHeaderMapSharedPtr&& headers,
        bool end_stream
    ) PURE;

    // Called when request trailers are decoded
    virtual void decodeTrailers(RequestTrailerMapPtr&& trailers) PURE;

    // Send local reply on protocol errors
    virtual void sendLocalReply(
        Code code,
        absl::string_view body,
        const std::function<void(ResponseHeaderMap&)>& modify_headers,
        const absl::optional<Grpc::Status::GrpcStatus> grpc_status,
        absl::string_view details
    ) PURE;

    // Access to stream info
    virtual StreamInfo::StreamInfo& streamInfo() PURE;

    // Access to access log handlers
    virtual AccessLog::InstanceSharedPtrVector accessLogHandlers() PURE;
};
```

#### Response Decoder

Client-side response decoding:

```cpp
class ResponseDecoder : public virtual StreamDecoder {
public:
    // Called when 1xx headers are decoded
    virtual void decode1xxHeaders(ResponseHeaderMapPtr&& headers) PURE;

    // Called when response headers are decoded
    virtual void decodeHeaders(
        ResponseHeaderMapPtr&& headers,
        bool end_stream
    ) PURE;

    // Called when response trailers are decoded
    virtual void decodeTrailers(ResponseTrailerMapPtr&& trailers) PURE;

    // Dump decoder state for debugging
    virtual void dumpState(std::ostream& os, int indent_level = 0) const PURE;
};
```

#### Stream Interface

Represents an HTTP stream (request/response pair):

```cpp
class Stream : public StreamResetHandler {
public:
    // Add/remove stream callbacks
    virtual void addCallbacks(StreamCallbacks& callbacks) PURE;
    virtual void removeCallbacks(StreamCallbacks& callbacks) PURE;

    // Flow control
    virtual void readDisable(bool disable) PURE;

    // Buffer limits
    virtual uint32_t bufferLimit() const PURE;

    // Flush timeout
    virtual void setFlushTimeout(std::chrono::milliseconds timeout) PURE;

    // Connection info
    virtual const Network::ConnectionInfoProvider& connectionInfoProvider() PURE;

    // Memory accounting
    virtual Buffer::BufferMemoryAccountSharedPtr account() const PURE;
    virtual void setAccount(Buffer::BufferMemoryAccountSharedPtr account) PURE;

    // Bytes meter for tracking
    virtual const StreamInfo::BytesMeterSharedPtr& bytesMeter() PURE;

    // Protocol-specific stream ID
    virtual absl::optional<uint32_t> codecStreamId() const PURE;
};
```

#### Stream Callbacks

```cpp
class StreamCallbacks {
public:
    virtual ~StreamCallbacks() = default;

    // Stream has been reset
    virtual void onResetStream(
        StreamResetReason reason,
        absl::string_view transport_failure_reason
    ) PURE;

    // Buffer watermark events
    virtual void onAboveWriteBufferHighWatermark() PURE;
    virtual void onBelowWriteBufferLowWatermark() PURE;
};
```

#### Connection Interface

Manages multiple streams on a single connection:

```cpp
class Connection {
public:
    virtual ~Connection() = default;

    // Dispatch incoming data
    virtual Status dispatch(Buffer::Instance& data) PURE;

    // Send GOAWAY frame
    virtual void goAway() PURE;

    // Get current protocol
    virtual Protocol protocol() PURE;

    // Shutdown notice
    virtual void shutdownNotice() PURE;

    // Check if codec wants to write
    virtual bool wantsToWrite() PURE;

    // Watermark events
    virtual void onUnderlyingConnectionAboveWriteBufferHighWatermark() PURE;
    virtual void onUnderlyingConnectionBelowWriteBufferLowWatermark() PURE;
};
```

#### Server Connection

```cpp
class ServerConnection : public virtual Connection {};

class ServerConnectionCallbacks : public virtual ConnectionCallbacks {
public:
    // Called when a new request stream arrives
    virtual RequestDecoder& newStream(
        ResponseEncoder& response_encoder,
        bool is_internally_created = false
    ) PURE;
};
```

#### Client Connection

```cpp
class ClientConnection : public virtual Connection {
public:
    // Create a new outgoing request stream
    virtual RequestEncoder& newStream(ResponseDecoder& response_decoder) PURE;
};
```

#### HTTP/1 Settings

```cpp
struct Http1Settings {
    bool allow_absolute_url_{false};
    bool accept_http_10_{false};
    std::string default_host_for_http_10_;
    bool enable_trailers_{false};
    bool allow_chunked_length_{false};

    enum class HeaderKeyFormat {
        Default,           // All lowercase
        ProperCase,        // Proper-Case
        StatefulFormatter  // Custom formatter
    };

    HeaderKeyFormat header_key_format_{HeaderKeyFormat::Default};
    bool stream_error_on_invalid_http_message_{false};
    bool validate_scheme_{false};
    bool send_fully_qualified_url_{false};
    bool allow_custom_methods_{false};
};
```

---

### Header Maps

**File**: `header_map.h` (55KB, 1600+ lines)

Efficient header storage and manipulation with copy-on-write semantics.

#### LowerCaseString

Header keys are stored as lowercase for efficient case-insensitive comparison:

```cpp
class LowerCaseString {
public:
    explicit LowerCaseString(absl::string_view data);

    absl::string_view get() const;
    const char* c_str() const;

    bool operator==(const LowerCaseString& rhs) const;
    // Hash-based comparison for O(1) lookups
};
```

#### HeaderString

Represents a header value with optimized storage:

```cpp
class HeaderString {
public:
    enum class Type {
        Inline,      // Small strings stored inline
        Reference,   // References external string (zero-copy)
        Dynamic      // Heap-allocated string
    };

    void setCopy(absl::string_view value);
    void setReference(absl::string_view value);
    void setInteger(uint64_t value);

    absl::string_view getStringView() const;
    uint64_t getInteger() const;
};
```

#### HeaderEntry

A single header key-value pair:

```cpp
class HeaderEntry {
public:
    virtual ~HeaderEntry() = default;

    virtual const HeaderString& key() const PURE;
    virtual HeaderString& value() PURE;
    virtual const HeaderString& value() const PURE;
};
```

#### HeaderMap

Main interface for header manipulation:

```cpp
class HeaderMap {
public:
    virtual ~HeaderMap() = default;

    // Add a header (allows duplicates)
    virtual void addCopy(const LowerCaseString& key, absl::string_view value) PURE;
    virtual void addReference(const LowerCaseString& key, absl::string_view value) PURE;
    virtual void addReferenceKey(const LowerCaseString& key, absl::string_view value) PURE;

    // Add via string (will be lowercased)
    virtual void addCopy(absl::string_view key, absl::string_view value) PURE;

    // Append to existing header value (comma-separated)
    virtual void appendCopy(const LowerCaseString& key, absl::string_view value) PURE;

    // Set header (replaces existing)
    virtual void setCopy(const LowerCaseString& key, absl::string_view value) PURE;
    virtual void setReference(const LowerCaseString& key, absl::string_view value) PURE;

    // Get header value
    virtual const HeaderEntry* get(const LowerCaseString& key) const PURE;

    // Remove header
    virtual size_t remove(const LowerCaseString& key) PURE;

    // Remove specific prefix (e.g., "x-envoy-")
    virtual void removePrefix(const LowerCaseString& prefix) PURE;

    // Iterate over headers
    virtual void iterate(HeaderMap::ConstIterateCb cb) const PURE;

    // Get header count
    virtual size_t size() const PURE;

    // Check if empty
    virtual bool empty() const PURE;

    // Get byte size (for buffer accounting)
    virtual size_t byteSize() const PURE;
};
```

#### Typed Header Accessors

For common headers, typed accessors provide type-safe access:

```cpp
class RequestHeaderMap : public HeaderMap {
public:
    // Pseudo-headers for HTTP/2 and HTTP/3
    virtual const HeaderEntry* Method() const PURE;
    virtual void setMethod(absl::string_view value) PURE;

    virtual const HeaderEntry* Path() const PURE;
    virtual void setPath(absl::string_view value) PURE;

    virtual const HeaderEntry* Scheme() const PURE;
    virtual void setScheme(absl::string_view value) PURE;

    virtual const HeaderEntry* Host() const PURE;
    virtual void setHost(absl::string_view value) PURE;

    // Common request headers
    virtual const HeaderEntry* ContentType() const PURE;
    virtual const HeaderEntry* ContentLength() const PURE;
    virtual const HeaderEntry* UserAgent() const PURE;
    virtual const HeaderEntry* ForwardedFor() const PURE;
    // ... many more
};

class ResponseHeaderMap : public HeaderMap {
public:
    // Pseudo-header
    virtual const HeaderEntry* Status() const PURE;
    virtual void setStatus(uint64_t status) PURE;

    // Common response headers
    virtual const HeaderEntry* ContentType() const PURE;
    virtual const HeaderEntry* ContentLength() const PURE;
    virtual const HeaderEntry* Server() const PURE;
    virtual const HeaderEntry* Date() const PURE;
    // ... many more
};
```

#### Header Map Creation

```cpp
using RequestHeaderMapPtr = std::unique_ptr<RequestHeaderMap>;
using ResponseHeaderMapPtr = std::unique_ptr<ResponseHeaderMap>;
using RequestTrailerMapPtr = std::unique_ptr<RequestTrailerMap>;
using ResponseTrailerMapPtr = std::unique_ptr<ResponseTrailerMap>;

// Create header maps via factory
RequestHeaderMapPtr createRequestHeaderMap();
ResponseHeaderMapPtr createResponseHeaderMap();
```

---

### Async Client

**File**: `async_client.h` (518 lines)

Non-blocking HTTP client for making internal requests (e.g., from filters to external services).

#### AsyncClient Interface

```cpp
class AsyncClient {
public:
    virtual ~AsyncClient() = default;

    // One-shot request with callback
    virtual Request* send(
        RequestMessagePtr&& request,
        Callbacks& callbacks,
        const RequestOptions& options
    ) PURE;

    // Start ongoing request (can send body/trailers incrementally)
    virtual OngoingRequest* startRequest(
        RequestHeaderMapPtr&& request_headers,
        Callbacks& callbacks,
        const RequestOptions& options
    ) PURE;

    // Start streaming (full-duplex)
    virtual Stream* start(
        StreamCallbacks& callbacks,
        const StreamOptions& options
    ) PURE;

    // Access to dispatcher
    virtual Event::Dispatcher& dispatcher() PURE;
};
```

#### Request Handle

```cpp
class Request {
public:
    virtual ~Request() = default;

    // Cancel the request
    virtual void cancel() PURE;
};
```

#### Callbacks for One-Shot Requests

```cpp
class Callbacks {
public:
    virtual ~Callbacks() = default;

    // Request succeeded
    virtual void onSuccess(
        const Request& request,
        ResponseMessagePtr&& response
    ) PURE;

    // Request failed
    virtual void onFailure(
        const Request& request,
        FailureReason reason
    ) PURE;

    // Before finalizing span (for tracing)
    virtual void onBeforeFinalizeUpstreamSpan(
        Tracing::Span& span,
        const Http::ResponseHeaderMap* response_headers
    ) PURE;
};

enum class FailureReason {
    Reset,                      // Stream reset
    ExceedResponseBufferLimit   // Response too large
};
```

#### StreamCallbacks for Streaming

```cpp
class StreamCallbacks {
public:
    virtual ~StreamCallbacks() = default;

    // Headers received
    virtual void onHeaders(ResponseHeaderMapPtr&& headers, bool end_stream) PURE;

    // Data received (may be called multiple times)
    virtual void onData(Buffer::Instance& data, bool end_stream) PURE;

    // Trailers received
    virtual void onTrailers(ResponseTrailerMapPtr&& trailers) PURE;

    // Stream completed gracefully
    virtual void onComplete() PURE;

    // Stream was reset
    virtual void onReset() PURE;
};
```

#### Stream Interface

```cpp
class Stream {
public:
    virtual ~Stream() = default;

    // Send headers
    virtual void sendHeaders(RequestHeaderMap& headers, bool end_stream) PURE;

    // Send data (can be called multiple times)
    virtual void sendData(Buffer::Instance& data, bool end_stream) PURE;

    // Send trailers (implicitly ends stream)
    virtual void sendTrailers(RequestTrailerMap& trailers) PURE;

    // Reset the stream
    virtual void reset() PURE;

    // Destructor callback (for cleanup)
    virtual void setDestructorCallback(StreamDestructorCallbacks callback) PURE;
    virtual void removeDestructorCallback() PURE;

    // Watermark callbacks (for flow control)
    virtual void setWatermarkCallbacks(SidestreamWatermarkCallbacks& callbacks) PURE;
    virtual void removeWatermarkCallbacks() PURE;

    // Check if over high watermark
    virtual bool isAboveWriteBufferHighWatermark() const PURE;

    // Access stream info
    virtual StreamInfo::StreamInfo& streamInfo() PURE;
};
```

#### RequestOptions / StreamOptions

```cpp
struct StreamOptions {
    // Timeout (from end_stream sent to first response received)
    absl::optional<std::chrono::milliseconds> timeout;

    // Buffer body for retry
    bool buffer_body_for_retry{false};

    // Add x-forwarded-for header
    bool send_xff{true};

    // Add x-envoy-internal header
    bool send_internal{true};

    // Hash policy for load balancing
    Protobuf::RepeatedPtrField<envoy::config::route::v3::RouteAction::HashPolicy> hash_policy;

    // Parent context (for correlation)
    ParentContext parent_context;

    // Metadata for subset load balancing
    envoy::config::core::v3::Metadata metadata;

    // Filter state
    StreamInfo::FilterStateSharedPtr filter_state;

    // Buffer accounting
    Buffer::BufferMemoryAccountSharedPtr account_{nullptr};
    absl::optional<uint64_t> buffer_limit_;

    // Retry policy
    absl::optional<envoy::config::route::v3::RetryPolicy> retry_policy;
    Router::RetryPolicyConstSharedPtr parsed_retry_policy;

    // Shadow request
    bool is_shadow{false};
    bool discard_response_body{false};

    // Tracing
    Tracing::Span* parent_span_{nullptr};
    std::string child_span_name_{""};
    absl::optional<bool> sampled_{true};

    // Watermark callbacks
    SidestreamWatermarkCallbacks* sidestream_watermark_callbacks = nullptr;

    // Remote close timeout
    std::chrono::milliseconds remote_close_timeout{1000};

    // Upstream override
    absl::optional<Upstream::LoadBalancerContext::OverrideHost> upstream_override_host_;
};
```

---

### Connection Pool

**File**: `conn_pool.h` (123 lines)

Manages connection pooling to upstream clusters for efficient connection reuse.

#### Instance Interface

```cpp
class Instance : public ConnectionPool::Instance {
public:
    virtual ~Instance() = default;

    struct StreamOptions {
        bool can_send_early_data_;  // For 0-RTT
        bool can_use_http3_;        // Allow HTTP/3
    };

    // Check if pool has active connections
    virtual bool hasActiveConnections() const PURE;

    // Create a new stream on the pool
    virtual Cancellable* newStream(
        ResponseDecoder& response_decoder,
        Callbacks& callbacks,
        const StreamOptions& options
    ) PURE;

    // Get protocol description (for logging)
    virtual absl::string_view protocolDescription() const PURE;
};
```

#### Callbacks

```cpp
class Callbacks {
public:
    virtual ~Callbacks() = default;

    // Pool error (no connection available)
    virtual void onPoolFailure(
        PoolFailureReason reason,
        absl::string_view transport_failure_reason,
        Upstream::HostDescriptionConstSharedPtr host
    ) PURE;

    // Connection ready
    virtual void onPoolReady(
        RequestEncoder& encoder,
        Upstream::HostDescriptionConstSharedPtr host,
        StreamInfo::StreamInfo& info,
        absl::optional<Http::Protocol> protocol
    ) PURE;
};

enum class PoolFailureReason {
    Overflow,            // Pool at max connections
    ConnectionFailure,   // Connection failed
    RemoteConnectionFailure,
    Timeout,            // Connection timeout
    LocalConnectionFailure
};
```

#### Connection Lifetime Callbacks

Track connection lifecycle:

```cpp
class ConnectionLifetimeCallbacks {
public:
    virtual ~ConnectionLifetimeCallbacks() = default;

    // Connection opened
    virtual void onConnectionOpen(
        Instance& pool,
        std::vector<uint8_t>& hash_key,
        const Network::Connection& connection
    ) PURE;

    // Connection draining
    virtual void onConnectionDraining(
        Instance& pool,
        std::vector<uint8_t>& hash_key,
        const Network::Connection& connection
    ) PURE;
};
```

#### Cancellable Handle

```cpp
class Cancellable {
public:
    virtual ~Cancellable() = default;

    // Cancel the pending stream request
    virtual void cancel(CancelPolicy cancel_policy) PURE;
};
```

---

### HTTP Messages

**File**: `message.h` (53 lines)

Simple wrapper for complete HTTP messages (headers + body + trailers).

#### Message Interface

```cpp
template <class HeaderType, class TrailerType>
class Message {
public:
    virtual ~Message() = default;

    // Access headers
    virtual HeaderType& headers() PURE;

    // Access body (modifiable)
    virtual Buffer::Instance& body() PURE;

    // Access trailers
    virtual TrailerType* trailers() PURE;

    // Set trailers
    virtual void trailers(std::unique_ptr<TrailerType>&& trailers) PURE;

    // Get body as string
    virtual std::string bodyAsString() const PURE;
};

using RequestMessage = Message<RequestHeaderMap, RequestTrailerMap>;
using RequestMessagePtr = std::unique_ptr<RequestMessage>;

using ResponseMessage = Message<ResponseHeaderMap, ResponseTrailerMap>;
using ResponseMessagePtr = std::unique_ptr<ResponseMessage>;
```

**Use Case**: Primarily used by AsyncClient for simple request/response patterns.

---

### Status Codes

**File**: `codes.h` (117 lines)

HTTP status code definitions and statistics.

#### Code Enum

```cpp
enum class Code : uint16_t {
    // 1xx Informational
    Continue                      = 100,
    SwitchingProtocols            = 101,

    // 2xx Success
    OK                            = 200,
    Created                       = 201,
    Accepted                      = 202,
    NoContent                     = 204,
    PartialContent                = 206,

    // 3xx Redirection
    MovedPermanently              = 301,
    Found                         = 302,
    SeeOther                      = 303,
    NotModified                   = 304,
    TemporaryRedirect             = 307,
    PermanentRedirect             = 308,

    // 4xx Client Error
    BadRequest                    = 400,
    Unauthorized                  = 401,
    Forbidden                     = 403,
    NotFound                      = 404,
    MethodNotAllowed              = 405,
    RequestTimeout                = 408,
    TooManyRequests               = 429,

    // 5xx Server Error
    InternalServerError           = 500,
    NotImplemented                = 501,
    BadGateway                    = 502,
    ServiceUnavailable            = 503,
    GatewayTimeout                = 504,
};
```

#### CodeStats

Tracks HTTP response code statistics:

```cpp
class CodeStats {
public:
    virtual ~CodeStats() = default;

    // Charge basic response stat
    virtual void chargeBasicResponseStat(
        Stats::Scope& scope,
        Stats::StatName prefix,
        Code response_code,
        bool exclude_http_code_stats
    ) const PURE;

    // Charge full response stat (including canary)
    virtual void chargeResponseStat(
        const ResponseStatInfo& info,
        bool exclude_http_code_stats
    ) const PURE;

    // Charge response timing
    virtual void chargeResponseTiming(const ResponseTimingInfo& info) const PURE;
};
```

---

## Request/Response Flow

### Downstream Request Flow

1. **Connection established**
   - `ServerConnection` accepts new connection
   - Codec created based on protocol negotiation (HTTP/1, HTTP/2, HTTP/3)

2. **Request arrives**
   - Codec decodes wire format into `RequestHeaderMap`
   - `ServerConnectionCallbacks::newStream()` called
   - `ResponseEncoder` created for sending response

3. **Filter chain (decode)**
   - Each `StreamDecoderFilter::decodeHeaders()` called in order
   - Filters can:
     - Modify headers
     - Return `Continue` to proceed
     - Return `StopIteration` to pause (e.g., for async operations)
     - Send local reply and stop
   - Similar for `decodeData()` and `decodeTrailers()`

4. **Router filter**
   - Final filter in chain
   - Selects upstream cluster
   - Gets connection from connection pool
   - Forwards request to upstream

5. **Upstream response arrives**
   - Response decoder callbacks fired
   - Filters process in reverse order (encode)

6. **Filter chain (encode)**
   - Each `StreamEncoderFilter::encodeHeaders()` called in reverse order
   - Similar processing: modify, continue, stop

7. **Response sent**
   - Codec encodes internal format to wire format
   - Response transmitted to downstream

### Async Client Flow

1. **Create request**
   ```cpp
   auto request = std::make_unique<RequestMessageImpl>();
   request->headers().setMethod("GET");
   request->headers().setPath("/api/resource");
   request->headers().setHost("service.internal");
   ```

2. **Send request**
   ```cpp
   async_client.send(
       std::move(request),
       callbacks,
       AsyncClient::RequestOptions()
           .setTimeout(std::chrono::milliseconds(1000))
   );
   ```

3. **Callbacks invoked**
   ```cpp
   void onSuccess(const Request& req, ResponseMessagePtr&& response) {
       auto status = response->headers().Status()->value();
       auto body = response->bodyAsString();
       // Process response
   }

   void onFailure(const Request& req, FailureReason reason) {
       // Handle failure
   }
   ```

---

## Key Patterns

### 1. Zero-Copy Header Manipulation

Headers use reference semantics to avoid copying:

```cpp
// Reference external string (zero-copy)
headers.setReference(LowerCaseString("x-custom"), external_string);

// Copy string value (creates internal copy)
headers.setCopy(LowerCaseString("x-custom"), "value");
```

### 2. Streaming with Backpressure

Use watermark callbacks to implement flow control:

```cpp
void onAboveWriteBufferHighWatermark() override {
    // Upstream buffer full, stop reading from downstream
    decoder_callbacks_->setDownstreamReadDisable(true);
}

void onBelowWriteBufferLowWatermark() override {
    // Upstream buffer drained, resume reading
    decoder_callbacks_->setDownstreamReadDisable(false);
}
```

### 3. Async Processing Pattern

```cpp
class MyFilter : public StreamDecoderFilter {
    FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
        // Start async operation
        async_client_->send(
            createAuthRequest(headers),
            *this,  // Callbacks
            options
        );

        state_ = State::WaitingForAuth;
        return FilterHeadersStatus::StopIteration;
    }

    void onSuccess(const Request&, ResponseMessagePtr&& response) override {
        if (response->headers().Status()->value() == "200") {
            // Auth succeeded, continue
            decoder_callbacks_->continueDecoding();
        } else {
            // Auth failed, send error
            decoder_callbacks_->sendLocalReply(
                Http::Code::Forbidden,
                "Unauthorized",
                nullptr, absl::nullopt, "auth_failed"
            );
        }
    }
};
```

### 4. Connection Pool Usage

```cpp
void makeUpstreamRequest() {
    conn_pool_->newStream(
        response_decoder_,
        *this,  // Callbacks
        Http::ConnectionPool::Instance::StreamOptions{
            .can_send_early_data_ = false,
            .can_use_http3_ = true
        }
    );
}

void onPoolReady(RequestEncoder& encoder, ...) override {
    // Got connection, send request
    encoder.encodeHeaders(request_headers_, true);
}

void onPoolFailure(PoolFailureReason reason, ...) override {
    // Connection failed
    sendLocalReply(Http::Code::ServiceUnavailable, "Upstream unavailable");
}
```

### 5. Header Validation

```cpp
bool isValidRequest(const RequestHeaderMap& headers) {
    // Check required headers
    if (!headers.Method() || !headers.Path()) {
        return false;
    }

    // Validate method
    auto method = headers.getMethodValue();
    if (method != "GET" && method != "POST" && method != "PUT") {
        return false;
    }

    // Check host header
    if (!headers.Host()) {
        return false;
    }

    return true;
}
```

---

## Usage Examples

### Example 1: Simple Header Manipulation Filter

```cpp
class AddHeaderFilter : public Http::StreamFilter {
public:
    FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
        // Add request ID
        headers.addCopy(LowerCaseString("x-request-id"), generateUuid());
        return FilterHeadersStatus::Continue;
    }

    FilterHeadersStatus encodeHeaders(ResponseHeaderMap& headers, bool end_stream) override {
        // Add server header
        headers.addCopy(LowerCaseString("x-powered-by"), "Envoy");
        return FilterHeadersStatus::Continue;
    }

    // Other required methods...
};
```

### Example 2: Body Buffering Filter

```cpp
class BodyInspectionFilter : public Http::StreamDecoderFilter {
public:
    FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
        if (headers.getMethodValue() == "POST") {
            // Buffer body for inspection
            need_inspection_ = true;
            return end_stream ? FilterHeadersStatus::Continue
                             : FilterHeadersStatus::StopAllIterationAndBuffer;
        }
        return FilterHeadersStatus::Continue;
    }

    FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
        if (!need_inspection_) {
            return FilterDataStatus::Continue;
        }

        if (!end_stream) {
            // Keep buffering
            return FilterDataStatus::StopIterationAndBuffer;
        }

        // All data buffered, inspect it
        if (containsMaliciousContent(data)) {
            decoder_callbacks_->sendLocalReply(
                Http::Code::BadRequest,
                "Invalid request content",
                nullptr, absl::nullopt, "content_validation_failed"
            );
            return FilterDataStatus::StopIterationNoBuffer;
        }

        return FilterDataStatus::Continue;
    }

private:
    bool need_inspection_{false};

    bool containsMaliciousContent(Buffer::Instance& data) {
        auto content = data.toString();
        return content.find("<script>") != std::string::npos;
    }
};
```

### Example 3: External Authorization Filter

```cpp
class ExternalAuthFilter : public Http::StreamDecoderFilter,
                           public Http::AsyncClient::Callbacks {
public:
    FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
        // Create auth request
        auto auth_request = createAuthRequest(headers);

        // Send to auth service
        active_request_ = async_client_.send(
            std::move(auth_request),
            *this,
            AsyncClient::RequestOptions().setTimeout(std::chrono::milliseconds(500))
        );

        if (active_request_ == nullptr) {
            // Failed immediately
            return FilterHeadersStatus::Continue;
        }

        // Wait for auth response
        return FilterHeadersStatus::StopIteration;
    }

    void onSuccess(const Request&, ResponseMessagePtr&& response) override {
        active_request_ = nullptr;

        if (response->headers().Status()->value() == "200") {
            // Authorized
            decoder_callbacks_->continueDecoding();
        } else {
            // Not authorized
            decoder_callbacks_->sendLocalReply(
                Http::Code::Forbidden,
                "Access denied",
                nullptr, absl::nullopt, "external_auth_denied"
            );
        }
    }

    void onFailure(const Request&, FailureReason reason) override {
        active_request_ = nullptr;

        // Auth service failure - allow or deny based on policy
        if (fail_open_) {
            decoder_callbacks_->continueDecoding();
        } else {
            decoder_callbacks_->sendLocalReply(
                Http::Code::ServiceUnavailable,
                "Authorization service unavailable",
                nullptr, absl::nullopt, "external_auth_error"
            );
        }
    }

    void onDestroy() override {
        if (active_request_) {
            active_request_->cancel();
            active_request_ = nullptr;
        }
    }

private:
    Http::AsyncClient& async_client_;
    Http::AsyncClient::Request* active_request_{nullptr};
    bool fail_open_{true};

    RequestMessagePtr createAuthRequest(const RequestHeaderMap& headers) {
        auto request = std::make_unique<RequestMessageImpl>();
        request->headers().setMethod("POST");
        request->headers().setPath("/auth/verify");
        request->headers().setHost("auth-service.internal");

        // Copy relevant headers for auth
        if (auto auth_header = headers.get(LowerCaseString("authorization"))) {
            request->headers().addCopy(
                LowerCaseString("authorization"),
                auth_header->value().getStringView()
            );
        }

        return request;
    }
};
```

### Example 4: Rate Limiting Filter

```cpp
class RateLimitFilter : public Http::StreamDecoderFilter {
public:
    FilterHeadersStatus decodeHeaders(RequestHeaderMap& headers, bool end_stream) override {
        // Extract rate limit key (e.g., client IP)
        auto key = extractRateLimitKey(headers);

        // Check rate limit
        if (isRateLimited(key)) {
            decoder_callbacks_->sendLocalReply(
                Http::Code::TooManyRequests,
                "Rate limit exceeded",
                [](ResponseHeaderMap& headers) {
                    headers.addCopy(
                        LowerCaseString("retry-after"),
                        "60"
                    );
                },
                absl::nullopt,
                "rate_limited"
            );
            return FilterHeadersStatus::StopIteration;
        }

        // Record request
        recordRequest(key);

        return FilterHeadersStatus::Continue;
    }

private:
    std::string extractRateLimitKey(const RequestHeaderMap& headers) {
        // Use X-Forwarded-For or remote address
        if (auto xff = headers.ForwardedFor()) {
            return std::string(xff->value().getStringView());
        }
        return decoder_callbacks_->streamInfo()
                   .downstreamAddressProvider()
                   .remoteAddress()
                   ->asString();
    }

    bool isRateLimited(const std::string& key) {
        auto now = std::chrono::steady_clock::now();
        auto& bucket = rate_limit_buckets_[key];

        // Token bucket algorithm
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - bucket.last_refill
        ).count();

        bucket.tokens = std::min(
            bucket.tokens + elapsed * tokens_per_second_,
            max_tokens_
        );
        bucket.last_refill = now;

        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            return false;
        }

        return true;
    }

    void recordRequest(const std::string& key) {
        // Already handled in isRateLimited
    }

    struct TokenBucket {
        double tokens{10.0};
        std::chrono::steady_clock::time_point last_refill{
            std::chrono::steady_clock::now()
        };
    };

    std::unordered_map<std::string, TokenBucket> rate_limit_buckets_;
    double tokens_per_second_{1.0};
    double max_tokens_{10.0};
};
```

---

## Summary

The Envoy HTTP interfaces provide a powerful, extensible architecture for:

- **Protocol-agnostic processing**: Same filter API works across HTTP/1, HTTP/2, HTTP/3
- **Efficient header handling**: Zero-copy semantics with LowerCaseString and HeaderString
- **Streaming support**: Full-duplex streaming with backpressure control
- **Async operations**: Non-blocking filter processing with AsyncClient
- **Connection pooling**: Efficient connection reuse to upstream services
- **Extensibility**: Custom filters without modifying core code

**Key Files**:
- `filter.h` - Filter interfaces and filter chain processing
- `codec.h` - Protocol encoding/decoding
- `header_map.h` - Efficient header storage and manipulation
- `async_client.h` - Non-blocking HTTP client
- `conn_pool.h` - Connection pooling
- `message.h` - Complete HTTP message wrapper
- `codes.h` - HTTP status codes

**Next Steps**:
- Read specific filter implementations in `/source/extensions/filters/http/`
- Study codec implementations in `/source/common/http/`
- Review connection pool implementation in `/source/common/conn_pool/`
