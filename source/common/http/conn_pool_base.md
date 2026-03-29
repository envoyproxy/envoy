# HTTP Connection Pool Base Implementation

**File**: `/source/common/http/conn_pool_base.h`
**Interface**: `/envoy/http/conn_pool.h`
**Purpose**: Shared connection pooling implementation for HTTP/1.1 and HTTP/2

## Overview

The connection pool base provides:
- **Connection reuse** - Efficient connection management to upstream hosts
- **Request queuing** - Queue requests when all connections are busy
- **Stream multiplexing** - HTTP/2 can handle multiple streams per connection
- **Connection lifecycle** - Manage connection creation, reuse, and draining
- **Backpressure handling** - Respect connection and stream limits

## Architecture

```
┌──────────────────────────────────────────────────────────────┐
│              Request needs upstream connection                │
│                           ↓                                    │
│  ┌────────────────────────────────────────────────────────┐  │
│  │  newStream(decoder, callbacks, options)                │  │
│  └────────────────────────────────────────────────────────┘  │
│                           ↓                                    │
│              ┌────────────────────────┐                        │
│              │ Connection available?  │                        │
│              └────────────────────────┘                        │
│                  ↓                  ↓                          │
│            YES                    NO                           │
│              ↓                      ↓                          │
│  ┌───────────────────┐  ┌──────────────────────────┐         │
│  │ Use existing conn │  │ Can create new connection?│         │
│  │ onPoolReady()     │  └──────────────────────────┘         │
│  └───────────────────┘             ↓                          │
│                              YES         NO                    │
│                               ↓           ↓                    │
│                    ┌─────────────────┐  ┌────────────┐        │
│                    │ Create new conn │  │ Queue      │        │
│                    │ When ready:     │  │ request    │        │
│                    │ onPoolReady()   │  └────────────┘        │
│                    └─────────────────┘                         │
└──────────────────────────────────────────────────────────────┘
```

## Key Classes

### HttpConnPoolImplBase

Base implementation for HTTP/1.1 and HTTP/2 connection pools:

```cpp
class HttpConnPoolImplBase : public ConnectionPool::ConnPoolImplBase,
                             public Http::ConnectionPool::Instance {
public:
    HttpConnPoolImplBase(
        Upstream::HostConstSharedPtr host,
        Upstream::ResourcePriority priority,
        Event::Dispatcher& dispatcher,
        const Network::ConnectionSocket::OptionsSharedPtr& options,
        const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
        Random::RandomGenerator& random_generator,
        Upstream::ClusterConnectivityState& state,
        std::vector<Http::Protocol> protocols,
        Server::OverloadManager& overload_manager
    );

    // Create a new stream request
    ConnectionPool::Cancellable* newStream(
        Http::ResponseDecoder& response_decoder,
        Http::ConnectionPool::Callbacks& callbacks,
        const Instance::StreamOptions& options
    ) override;

    // Check if pool has active connections
    bool hasActiveConnections() const override;

    // Attempt preconnect (warm up connections)
    bool maybePreconnect(float ratio) override;

    // Create protocol-specific codec client (implemented by subclasses)
    virtual CodecClientPtr createCodecClient(
        Upstream::Host::CreateConnectionData& data
    ) PURE;

protected:
    Random::RandomGenerator& random_generator_;
    absl::optional<HttpServerPropertiesCache::Origin> origin_;
};
```

### HttpPendingStream

Represents a stream request waiting for a connection:

```cpp
class HttpPendingStream : public ConnectionPool::PendingStream {
public:
    HttpPendingStream(
        ConnectionPool::ConnPoolImplBase& parent,
        Http::ResponseDecoder& decoder,
        Http::ConnectionPool::Callbacks& callbacks,
        bool can_send_early_data
    );

    // Context holds decoder and callbacks
    HttpAttachContext context_;
};
```

### HttpAttachContext

Context passed when attaching a stream to a connection:

```cpp
struct HttpAttachContext : public ConnectionPool::AttachContext {
    HttpAttachContext(
        Http::ResponseDecoder* decoder,
        Http::ConnectionPool::Callbacks* callbacks
    );

    Http::ResponseDecoder* decoder_;
    Http::ConnectionPool::Callbacks* callbacks_;
    ResponseDecoderHandlePtr decoder_handle_;  // For lifetime tracking
};
```

### ActiveClient

Base class for an active upstream connection:

```cpp
class ActiveClient : public ConnectionPool::ActiveClient {
public:
    ActiveClient(
        HttpConnPoolImplBase& parent,
        uint32_t lifetime_stream_limit,           // Max streams over lifetime
        uint32_t effective_concurrent_stream_limit, // Current max concurrent
        uint32_t configured_concurrent_stream_limit, // Config max concurrent
        OptRef<Upstream::Host::CreateConnectionData> opt_data
    );

    void initialize(
        Upstream::Host::CreateConnectionData& data,
        HttpConnPoolImplBase& parent
    );

    // Initialize read filters on the connection
    void initializeReadFilters() override;

    // Get connection protocol (HTTP/1.1, HTTP/2, HTTP/3)
    absl::optional<Http::Protocol> protocol() const override;

    // Close the connection
    void close() override;

    // Create new stream encoder (implemented by subclasses)
    virtual Http::RequestEncoder& newStreamEncoder(
        Http::ResponseDecoder& response_decoder
    ) PURE;

    virtual Http::RequestEncoder& newStreamEncoder(
        Http::ResponseDecoderHandlePtr response_decoder_handle
    ) PURE;

    // Handle connection events (connected, closed, etc.)
    void onEvent(Network::ConnectionEvent event) override;

    // Number of active streams on this connection
    uint32_t numActiveStreams() const override;

    // Connection ID
    uint64_t id() const override;

    Http::CodecClientPtr codec_client_;
    uint32_t request_count_{0};  // Total requests on this connection
};
```

### FixedHttpConnPoolImpl

Concrete pool implementation for a fixed protocol (HTTP/1 or HTTP/2):

```cpp
class FixedHttpConnPoolImpl : public HttpConnPoolImplBase {
public:
    using CreateClientFn = std::function<ActiveClientPtr(HttpConnPoolImplBase*)>;
    using CreateCodecFn = std::function<CodecClientPtr(
        Upstream::Host::CreateConnectionData&,
        HttpConnPoolImplBase*
    )>;

    FixedHttpConnPoolImpl(
        Upstream::HostConstSharedPtr host,
        Upstream::ResourcePriority priority,
        Event::Dispatcher& dispatcher,
        const Network::ConnectionSocket::OptionsSharedPtr& options,
        const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
        Random::RandomGenerator& random_generator,
        Upstream::ClusterConnectivityState& state,
        CreateClientFn client_fn,
        CreateCodecFn codec_fn,
        std::vector<Http::Protocol> protocols,
        Server::OverloadManager& overload_manager,
        absl::optional<HttpServerPropertiesCache::Origin> origin = absl::nullopt,
        Http::HttpServerPropertiesCacheSharedPtr cache = nullptr
    );

    // Create codec client using provided factory function
    CodecClientPtr createCodecClient(
        Upstream::Host::CreateConnectionData& data
    ) override;

    // Instantiate active client using provided factory function
    ActiveClientPtr instantiateActiveClient() override;

    // Protocol description for logging
    absl::string_view protocolDescription() const override;

private:
    const CreateCodecFn codec_fn_;
    const CreateClientFn client_fn_;
    const Http::Protocol protocol_;
    Http::HttpServerPropertiesCacheSharedPtr cache_;
};
```

### MultiplexedActiveClientBase

Base class for HTTP/2 and HTTP/3 active clients (support multiple concurrent streams):

```cpp
class MultiplexedActiveClientBase : public CodecClientCallbacks,
                                    public Http::ConnectionCallbacks,
                                    public Http::ActiveClient {
public:
    MultiplexedActiveClientBase(
        HttpConnPoolImplBase& parent,
        uint32_t effective_concurrent_streams,
        uint32_t max_configured_concurrent_streams,
        Stats::Counter& cx_total,
        OptRef<Upstream::Host::CreateConnectionData> data
    );

    // Caps max streams to prevent overflow
    static uint32_t maxStreamsPerConnection(uint32_t max_streams_config);

    // Check if connection is closing with incomplete streams
    bool closingWithIncompleteStream() const override;

    // Create new stream encoder
    RequestEncoder& newStreamEncoder(ResponseDecoder& response_decoder) override;
    RequestEncoder& newStreamEncoder(
        ResponseDecoderHandlePtr response_decoder_handle
    ) override;

    // CodecClientCallbacks
    void onStreamDestroy() override;
    void onStreamReset(Http::StreamResetReason reason) override;

    // Http::ConnectionCallbacks
    void onGoAway(Http::GoAwayErrorCode error_code) override;
    void onSettings(ReceivedSettings& settings) override;

private:
    bool closed_with_active_rq_{};
};
```

## Connection Pool Lifecycle

### 1. Pool Creation

```cpp
// HTTP/2 pool creation
auto http2_pool = std::make_unique<FixedHttpConnPoolImpl>(
    host,
    priority,
    dispatcher,
    options,
    transport_socket_options,
    random_generator,
    state,
    // Client factory
    [](HttpConnPoolImplBase* pool) -> ActiveClientPtr {
        return std::make_unique<Http2ActiveClient>(*pool);
    },
    // Codec factory
    [](Upstream::Host::CreateConnectionData& data, HttpConnPoolImplBase* pool) {
        return std::make_unique<CodecClientProd>(
            CodecType::HTTP2, data.connection_, data.host_description_,
            pool->dispatcher(), pool->randomGenerator()
        );
    },
    {Http::Protocol::Http2},  // Protocols
    overload_manager
);
```

### 2. New Stream Request

When a request needs an upstream connection:

```cpp
// Router filter requests a stream
auto cancellable = conn_pool_->newStream(
    response_decoder_,
    *this,  // ConnectionPool::Callbacks
    Http::ConnectionPool::Instance::StreamOptions{
        .can_send_early_data_ = false,
        .can_use_http3_ = true
    }
);

if (cancellable == nullptr) {
    // Immediate result (success or failure)
    // Callbacks already invoked
} else {
    // Request queued, save handle for cancellation
    pending_request_ = cancellable;
}
```

### 3. Connection Selection

The pool decides how to handle the request:

**Case A: Existing connection available**
```cpp
// Connection with available capacity found
onPoolReady(active_client, context);

// Callback invoked synchronously
callbacks->onPoolReady(
    encoder,
    host_description,
    stream_info,
    protocol
);
```

**Case B: Need new connection**
```cpp
// Create new connection
auto data = host_->createConnection(dispatcher_, socket_options_);
auto client = createCodecClient(data);

// Connection established asynchronously
// When ready, onPoolReady() called
```

**Case C: Queue request**
```cpp
// Max connections reached, queue the request
auto pending = std::make_unique<HttpPendingStream>(
    *this, decoder, callbacks, can_send_early_data
);
pending_streams_.push_back(std::move(pending));

// Return cancellable handle
return pending_streams_.back().get();
```

### 4. Stream Attachment

When a connection is ready:

```cpp
void HttpConnPoolImplBase::onPoolReady(
    ConnectionPool::ActiveClient& client,
    ConnectionPool::AttachContext& context
) {
    auto& http_context = typedContext<HttpAttachContext>(context);
    auto* callbacks = http_context.callbacks_;

    // Create stream encoder
    Http::RequestEncoder& encoder = static_cast<ActiveClient&>(client)
        .newStreamEncoder(*http_context.decoder_);

    // Invoke success callback
    callbacks->onPoolReady(
        encoder,
        host_,
        client.streamInfo(),
        client.protocol()
    );
}
```

### 5. Stream Completion

When the request/response completes:

```cpp
void ActiveClient::onStreamDestroy() {
    request_count_++;

    // Check if connection should be closed
    if (shouldCloseConnection()) {
        parent_.scheduleOnUpstreamReady();
        close();
    } else {
        // Connection available for reuse
        parent_.onStreamClosed(*this, false);
    }
}
```

### 6. Connection Draining

When the pool is drained:

```cpp
void HttpConnPoolImplBase::drainConnections(DrainBehavior drain_behavior) {
    // Stop creating new connections
    draining_ = true;

    // Gracefully close idle connections
    for (auto& client : ready_clients_) {
        client->close();
    }

    // Let active connections finish
    // They will close after completing current streams
}
```

## HTTP/1.1 vs HTTP/2 Differences

### HTTP/1.1 Connection Pool

**Characteristics**:
- One stream per connection
- Connection reused after stream completes
- Pipelining not supported (safety reasons)

**Stream Limit**:
```cpp
// HTTP/1.1: Always 1 concurrent stream
effective_concurrent_stream_limit = 1
```

**Connection Selection**:
```cpp
// Need a new connection for each concurrent request
if (num_active_connections < max_connections) {
    createNewConnection();
} else {
    queueRequest();
}
```

### HTTP/2 Connection Pool

**Characteristics**:
- Multiple streams per connection (multiplexing)
- Configurable concurrent stream limit
- SETTINGS frame negotiates max streams

**Stream Limit**:
```cpp
// HTTP/2: Negotiated with server
effective_concurrent_stream_limit = min(
    configured_max_streams,
    server_max_concurrent_streams  // From SETTINGS frame
);

// Default: 100 concurrent streams per connection
// Cap at 2^31 - 1 to prevent overflow
```

**Connection Selection**:
```cpp
// Prefer existing connection with capacity
for (auto& client : ready_clients_) {
    if (client->numActiveStreams() < client->effectiveConcurrentStreamLimit()) {
        return client;  // Reuse connection
    }
}

// All connections at capacity
if (num_connections < max_connections) {
    createNewConnection();
} else {
    queueRequest();
}
```

### HTTP/3 Connection Pool

**Characteristics**:
- QUIC-based transport
- Built-in stream multiplexing
- 0-RTT support (early data)
- Happy eyeballs (IPv4/IPv6)

Implemented in `/source/common/http/http3/conn_pool.h`.

## Callbacks and Failure Handling

### ConnectionPool::Callbacks

```cpp
class Callbacks {
public:
    // Connection ready - stream can be created
    virtual void onPoolReady(
        RequestEncoder& encoder,
        Upstream::HostDescriptionConstSharedPtr host,
        StreamInfo::StreamInfo& info,
        absl::optional<Http::Protocol> protocol
    ) PURE;

    // Pool failure - no connection available
    virtual void onPoolFailure(
        PoolFailureReason reason,
        absl::string_view transport_failure_reason,
        Upstream::HostDescriptionConstSharedPtr host
    ) PURE;
};
```

### Failure Reasons

```cpp
enum class PoolFailureReason {
    // Connection pool is at max connections
    Overflow,

    // Connection to upstream failed
    ConnectionFailure,

    // Remote connection closed
    RemoteConnectionFailure,

    // Connection timeout
    Timeout,

    // Local connection failed (before reaching upstream)
    LocalConnectionFailure
};
```

### Failure Handling Example

```cpp
void RouterFilter::onPoolFailure(
    PoolFailureReason reason,
    absl::string_view transport_failure_reason,
    Upstream::HostDescriptionConstSharedPtr host
) {
    // Log failure
    ENVOY_STREAM_LOG(debug, "Pool failure: {} - {}",
                     *callbacks_, reason, transport_failure_reason);

    // Record stats
    cluster_->stats().upstream_cx_connect_fail_.inc();

    // Decide retry or error
    if (reason == PoolFailureReason::Overflow) {
        // Pool saturated - return 503
        sendLocalReply(Http::Code::ServiceUnavailable,
                      "upstream connect overflow");
    } else if (reason == PoolFailureReason::Timeout) {
        // Connection timeout - maybe retry
        if (retry_state_->shouldRetry()) {
            doRetry();
        } else {
            sendLocalReply(Http::Code::GatewayTimeout,
                          "upstream connect timeout");
        }
    } else {
        // Connection failure - retry or 503
        if (retry_state_->shouldRetry()) {
            doRetry();
        } else {
            sendLocalReply(Http::Code::ServiceUnavailable,
                          "upstream connect error");
        }
    }
}
```

## Preconnecting

Warm up connections before requests arrive:

```cpp
// Preconnect to establish connections in advance
bool preconnected = conn_pool_->maybePreconnect(0.5);  // 50% of max connections

// Internal implementation
bool HttpConnPoolImplBase::maybePreconnect(float ratio) {
    uint32_t target = std::ceil(max_connections_ * ratio);
    uint32_t current = num_active_connections_;

    for (uint32_t i = current; i < target; i++) {
        auto data = host_->createConnection(dispatcher_, socket_options_);
        auto client = createCodecClient(data);
        ready_clients_.push_back(std::move(client));
    }

    return target > current;
}
```

**Use Cases**:
- Before expected traffic spike
- During service warmup
- After auto-scaling events

## Configuration

### Cluster Connection Pool Settings

```yaml
clusters:
  - name: my_service
    connect_timeout: 1s

    # Circuit breaker limits
    circuit_breakers:
      thresholds:
        - priority: DEFAULT
          max_connections: 1024          # Max total connections
          max_pending_requests: 1024     # Max queued requests
          max_requests: 1024             # Max active requests
          max_retries: 3

    # HTTP/2 settings
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        explicit_http_config:
          http2_protocol_options:
            max_concurrent_streams: 100  # Streams per connection
```

### Per-Route Override

```yaml
routes:
  - match:
      prefix: "/api"
    route:
      cluster: my_service
      # Override connection pool behavior
      max_stream_duration:
        max_stream_duration: 300s
```

## Best Practices

### 1. Set Appropriate Connection Limits

**For HTTP/1.1**:
```yaml
max_connections: 512  # 512 concurrent connections
max_requests: 512     # 512 concurrent requests (1:1 with connections)
```

**For HTTP/2**:
```yaml
max_connections: 16   # Far fewer connections needed
max_requests: 1600    # 100 streams/conn * 16 connections
```

### 2. Configure Timeouts

```yaml
connect_timeout: 1s    # Connection establishment timeout
idle_timeout: 300s     # Keep connection open when idle
```

### 3. Monitor Pool Metrics

Key stats to watch:

```
# Connection creation rate
cluster.my_service.upstream_cx_total

# Active connections
cluster.my_service.upstream_cx_active

# Connection pool overflow (queue saturated)
cluster.my_service.upstream_cx_overflow

# Connection timeouts
cluster.my_service.upstream_cx_connect_timeout

# Connection failures
cluster.my_service.upstream_cx_connect_fail
```

### 4. Handle Overload Gracefully

```cpp
void onPoolFailure(PoolFailureReason reason, ...) {
    if (reason == PoolFailureReason::Overflow) {
        // Pool saturated - consider:
        // 1. Shedding load (reject requests)
        // 2. Increasing limits (if host can handle it)
        // 3. Adding more upstream hosts
        sendLocalReply(Http::Code::ServiceUnavailable, "overloaded");
    }
}
```

### 5. Use Preconnecting Wisely

```cpp
// Good: Preconnect before known traffic spike
void beforeTrafficSpike() {
    conn_pool_->maybePreconnect(0.5);
}

// Bad: Aggressive preconnecting wastes resources
void onCreate() {
    conn_pool_->maybePreconnect(1.0);  // Too much!
}
```

## Summary

The HTTP connection pool base provides:

- **Efficient connection reuse** across requests
- **Protocol-specific optimizations** (HTTP/1.1 vs HTTP/2 vs HTTP/3)
- **Request queuing** when connections are saturated
- **Configurable limits** for connections, requests, and streams
- **Graceful failure handling** with retry support

**Key Design Points**:
- HTTP/1.1: One stream per connection
- HTTP/2: Multiple streams per connection (multiplexing)
- HTTP/3: QUIC-based with 0-RTT support
- Flexible factory pattern for protocol-specific implementations

**Next**: See `conn_pool_grid.h` for HTTP/3 failover to HTTP/2.
