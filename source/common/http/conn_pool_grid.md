# HTTP Connection Pool Grid - HTTP/3 with Failover

**File**: `/source/common/http/conn_pool_grid.h`
**Purpose**: HTTP/3 (QUIC) connection pool with automatic failover to HTTP/2/TCP

## Overview

The ConnectivityGrid is an intelligent connection pool that:
- **Tries HTTP/3 first** - Uses QUIC for better performance
- **Falls back to HTTP/2** - If HTTP/3 fails or times out
- **Implements Happy Eyeballs** - Tries IPv4 and IPv6 in parallel for HTTP/3
- **Marks HTTP/3 broken** - Temporarily disables HTTP/3 after repeated failures
- **Transparent to caller** - Hides complexity of protocol negotiation

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                   newStream() Request                        │
│                           ↓                                   │
│         ┌─────────────────────────────────────┐             │
│         │   Should attempt HTTP/3?            │             │
│         │  - Alternate protocol configured?   │             │
│         │  - HTTP/3 not broken?              │             │
│         └─────────────────────────────────────┘             │
│                  YES ↓              ↓ NO                      │
│  ┌──────────────────────────┐   ┌────────────────────┐     │
│  │  Try HTTP/3 (QUIC)       │   │  Try HTTP/2 (TCP)  │     │
│  │  - Start timer           │   │                     │     │
│  └──────────────────────────┘   └────────────────────┘     │
│           ↓                                                   │
│  ┌──────────────────────────┐                               │
│  │ Wait for:                │                               │
│  │ 1. HTTP/3 success        │                               │
│  │ 2. HTTP/3 failure        │                               │
│  │ 3. Timeout               │                               │
│  └──────────────────────────┘                               │
│           ↓                                                   │
│  ┌──────────────────────────────────────┐                   │
│  │ On Timeout or HTTP/3 Failure         │                   │
│  │  → Fallback to HTTP/2 (TCP)         │                   │
│  │  → Try HTTP/3 Happy Eyeballs        │                   │
│  │     (alternate address family)       │                   │
│  └──────────────────────────────────────┘                   │
│           ↓                                                   │
│  ┌──────────────────────────────────────┐                   │
│  │ First Success Wins                   │                   │
│  │  → Cancel other attempts             │                   │
│  │  → Return encoder to caller          │                   │
│  └──────────────────────────────────────┘                   │
│           ↓                                                   │
│  ┌──────────────────────────────────────┐                   │
│  │ Track Protocol Performance           │                   │
│  │  - HTTP/3 succeeded → mark working   │                   │
│  │  - HTTP/3 failed + TCP worked →     │                   │
│  │    mark HTTP/3 broken               │                   │
│  └──────────────────────────────────────┘                   │
└─────────────────────────────────────────────────────────────┘
```

## Key Classes

### ConnectivityGrid

Main grid implementation managing multiple pools:

```cpp
class ConnectivityGrid : public ConnectionPool::Instance,
                         public Http3::PoolConnectResultCallback {
public:
    struct ConnectivityOptions {
        explicit ConnectivityOptions(const std::vector<Http::Protocol>& protocols)
            : protocols_(protocols) {}
        std::vector<Http::Protocol> protocols_;
    };

    ConnectivityGrid(
        Event::Dispatcher& dispatcher,
        Random::RandomGenerator& random_generator,
        Upstream::HostConstSharedPtr host,
        Upstream::ResourcePriority priority,
        const Network::ConnectionSocket::OptionsSharedPtr& options,
        const Network::TransportSocketOptionsConstSharedPtr& transport_socket_options,
        Upstream::ClusterConnectivityState& state,
        TimeSource& time_source,
        HttpServerPropertiesCacheSharedPtr alternate_protocols,
        ConnectivityOptions connectivity_options,
        Quic::QuicStatNames& quic_stat_names,
        Stats::Scope& scope,
        Http::PersistentQuicInfo& quic_info,
        OptRef<Quic::EnvoyQuicNetworkObserverRegistry> network_observer_registry,
        Server::OverloadManager& overload_manager
    );

    // Create new stream (tries HTTP/3 first, falls back to HTTP/2)
    ConnectionPool::Cancellable* newStream(
        Http::ResponseDecoder& response_decoder,
        ConnectionPool::Callbacks& callbacks,
        const Instance::StreamOptions& options
    ) override;

    // Check if HTTP/3 is currently broken
    bool isHttp3Broken() const;

    // Mark HTTP/3 as broken (exponential backoff)
    void markHttp3Broken();

    // Mark HTTP/3 as confirmed working (reset backoff)
    void markHttp3Confirmed();

    // Http3::PoolConnectResultCallback
    void onHandshakeComplete() override;
    void onZeroRttHandshakeFailed() override;

private:
    // Decide if HTTP/3 should be attempted
    bool shouldAttemptHttp3();

    // Get or create pools
    ConnectionPool::Instance* getOrCreateHttp3Pool();
    ConnectionPool::Instance* getOrCreateHttp2Pool();
    ConnectionPool::Instance* getOrCreateHttp3AlternativePool();

    // Create pools
    virtual ConnectionPool::InstancePtr createHttp3Pool(bool attempt_alternate_address);
    virtual ConnectionPool::InstancePtr createHttp2Pool();

    // The underlying connection pools
    ConnectionPool::InstancePtr http3_pool_;           // Primary HTTP/3
    ConnectionPool::InstancePtr http3_alternate_pool_; // Happy eyeballs HTTP/3
    ConnectionPool::InstancePtr http2_pool_;           // Fallback TCP pool

    // Wrapped callbacks (lifetime management)
    std::list<WrapperCallbacksPtr> wrapped_callbacks_;

    // Configuration
    std::chrono::milliseconds next_attempt_duration_;  // Timeout before failover
    HttpServerPropertiesCacheSharedPtr alternate_protocols_;

    // State
    bool draining_{false};
    bool destroying_{};
    bool deferred_deleting_{};

    // ... other members
};
```

### WrapperCallbacks

Wraps caller's callbacks to handle multiple connection attempts:

```cpp
class WrapperCallbacks : public ConnectionPool::Cancellable,
                         public LinkedObject<WrapperCallbacks> {
public:
    WrapperCallbacks(
        ConnectivityGrid& grid,
        Http::ResponseDecoder& decoder,
        ConnectionPool::Callbacks& callbacks,
        const Instance::StreamOptions& options
    );

    // Cancel all pending connection attempts
    void cancel(ConnectionPool::CancelPolicy cancel_policy) override;

    // Try to create stream on given pool
    StreamCreationResult newStream(ConnectionPool::Instance& pool);

    // Attempt another connection (failover)
    absl::optional<StreamCreationResult> tryAnotherConnection();

    // Timer callback for failover
    void onNextAttemptTimer();

    // Called when a connection attempt succeeds
    void onConnectionAttemptReady(
        ConnectionAttemptCallbacks* attempt,
        RequestEncoder& encoder,
        Upstream::HostDescriptionConstSharedPtr host,
        StreamInfo::StreamInfo& info,
        absl::optional<Http::Protocol> protocol
    );

    // Called when a connection attempt fails
    void onConnectionAttemptFailed(
        ConnectionAttemptCallbacks* attempt,
        ConnectionPool::PoolFailureReason reason,
        absl::string_view transport_failure_reason,
        Upstream::HostDescriptionConstSharedPtr host
    );

private:
    // Check if HTTP/3 happy eyeballs should be attempted
    bool shouldAttemptSecondHttp3Connection();

    // Attempt HTTP/3 happy eyeballs (alternate address family)
    StreamCreationResult attemptSecondHttp3Connection();

    // Mark HTTP/3 broken if TCP succeeded but HTTP/3 failed
    void maybeMarkHttp3Broken();

    // Cancel all pending attempts
    void cancelAllPendingAttempts(ConnectionPool::CancelPolicy cancel_policy);

    // Active connection attempts
    std::list<ConnectionAttemptCallbacksPtr> connection_attempts_;

    ConnectivityGrid& grid_;
    Http::ResponseDecoder& decoder_;
    Http::ResponseDecoderHandlePtr decoder_handle_;
    ConnectionPool::Callbacks* inner_callbacks_;

    Event::TimerPtr next_attempt_timer_;
    bool has_attempted_http2_ = false;
    bool has_tried_http3_alternate_address_ = false;
    bool http3_attempt_failed_{};
    bool tcp_attempt_succeeded_{};

    const Instance::StreamOptions stream_options_{};
};
```

### ConnectionAttemptCallbacks

Represents a single connection attempt to a specific pool:

```cpp
class ConnectionAttemptCallbacks : public ConnectionPool::Callbacks,
                                   public LinkedObject<ConnectionAttemptCallbacks> {
public:
    ConnectionAttemptCallbacks(WrapperCallbacks& parent, ConnectionPool::Instance& pool);

    // Initiate stream creation
    StreamCreationResult newStream();

    // ConnectionPool::Callbacks
    void onPoolReady(
        RequestEncoder& encoder,
        Upstream::HostDescriptionConstSharedPtr host,
        StreamInfo::StreamInfo& info,
        absl::optional<Http::Protocol> protocol
    ) override;

    void onPoolFailure(
        ConnectionPool::PoolFailureReason reason,
        absl::string_view transport_failure_reason,
        Upstream::HostDescriptionConstSharedPtr host
    ) override;

    // Cancel this connection attempt
    void cancel(ConnectionPool::CancelPolicy cancel_policy);

private:
    WrapperCallbacks& parent_;
    ConnectionPool::Instance& pool_;
    Cancellable* cancellable_{nullptr};  // Handle to underlying pool request
};
```

## Protocol Selection Flow

### Step 1: Initial Decision

```cpp
ConnectionPool::Cancellable* ConnectivityGrid::newStream(
    Http::ResponseDecoder& response_decoder,
    ConnectionPool::Callbacks& callbacks,
    const Instance::StreamOptions& options
) {
    // Create wrapper to manage multiple attempts
    auto wrapper = std::make_unique<WrapperCallbacks>(*this, response_decoder,
                                                       callbacks, options);

    // Decide which protocol to try
    ConnectionPool::Instance* pool = nullptr;
    if (shouldAttemptHttp3()) {
        pool = getOrCreateHttp3Pool();
    } else {
        pool = getOrCreateHttp2Pool();
    }

    // Attempt stream creation
    auto result = wrapper->newStream(*pool);

    if (result == StreamCreationResult::ImmediateResult) {
        // Immediate success or failure - wrapper already notified caller
        return nullptr;
    } else {
        // Async - return cancellable handle
        auto* raw_wrapper = wrapper.get();
        wrapped_callbacks_.push_back(std::move(wrapper));
        return raw_wrapper;
    }
}
```

### Step 2: Determining HTTP/3 Eligibility

```cpp
bool ConnectivityGrid::shouldAttemptHttp3() {
    // Check if HTTP/3 is configured
    if (!alternate_protocols_) {
        return false;
    }

    // Check if HTTP/3 is currently broken
    if (isHttp3Broken()) {
        return false;
    }

    // Check if draining (shutting down)
    if (draining_) {
        return false;
    }

    // Look up alternate protocol for this origin
    auto tracker = getHttp3StatusTracker();
    if (!tracker.isHttp3Confirmed() && !tracker.isHttp3Broken()) {
        // Unknown state - try HTTP/3
        return true;
    }

    return tracker.isHttp3Confirmed();
}
```

### Step 3: HTTP/3 Attempt with Timeout

```cpp
void WrapperCallbacks::onNextAttemptTimer() {
    // Timeout expired - time to try alternatives

    // 1. Attempt HTTP/3 happy eyeballs (alternate address family)
    if (shouldAttemptSecondHttp3Connection()) {
        attemptSecondHttp3Connection();
    }

    // 2. Attempt HTTP/2 fallback
    if (!has_attempted_http2_) {
        has_attempted_http2_ = true;
        auto* http2_pool = grid_.getOrCreateHttp2Pool();
        if (http2_pool) {
            newStream(*http2_pool);
        }
    }
}
```

### Step 4: Connection Success

```cpp
void WrapperCallbacks::onConnectionAttemptReady(
    ConnectionAttemptCallbacks* attempt,
    RequestEncoder& encoder,
    Upstream::HostDescriptionConstSharedPtr host,
    StreamInfo::StreamInfo& info,
    absl::optional<Http::Protocol> protocol
) {
    // Success! This protocol won the race

    // Track if TCP succeeded (for marking HTTP/3 broken)
    if (protocol == Http::Protocol::Http2) {
        tcp_attempt_succeeded_ = true;
    }

    // Mark HTTP/3 broken if:
    // - HTTP/3 failed but TCP succeeded
    maybeMarkHttp3Broken();

    // Notify caller of success
    if (inner_callbacks_) {
        inner_callbacks_->onPoolReady(encoder, host, info, protocol);
        inner_callbacks_ = nullptr;  // Only notify once
    }

    // Cancel all other pending attempts
    cancelAllPendingAttempts(ConnectionPool::CancelPolicy::CloseExcess);

    // Delete this wrapper
    deleteThis();
}
```

### Step 5: Connection Failure

```cpp
void WrapperCallbacks::onConnectionAttemptFailed(
    ConnectionAttemptCallbacks* attempt,
    ConnectionPool::PoolFailureReason reason,
    absl::string_view transport_failure_reason,
    Upstream::HostDescriptionConstSharedPtr host
) {
    // Remove this attempt from the list
    connection_attempts_.remove_if([attempt](const auto& a) {
        return a.get() == attempt;
    });

    // Track if HTTP/3 failed
    if (grid_.isPoolHttp3(attempt->pool())) {
        http3_attempt_failed_ = true;
    }

    // Try another connection (failover)
    auto result = tryAnotherConnection();

    if (!result.has_value()) {
        // No more options - all attempts failed
        if (inner_callbacks_) {
            inner_callbacks_->onPoolFailure(reason, transport_failure_reason, host);
            inner_callbacks_ = nullptr;
        }
        deleteThis();
    }
}
```

## HTTP/3 Happy Eyeballs

Happy Eyeballs tries IPv4 and IPv6 in parallel to find the fastest path.

### When to Use Happy Eyeballs

```cpp
bool WrapperCallbacks::shouldAttemptSecondHttp3Connection() {
    // Already tried? Don't try again
    if (has_tried_http3_alternate_address_) {
        return false;
    }

    // Grid shutting down?
    if (grid_.draining_) {
        return false;
    }

    // Runtime feature disabled?
    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.http3_happy_eyeballs")) {
        return false;
    }

    // Need both IPv4 and IPv6 addresses
    auto& address_list = host_->addressList();
    if (address_list.size() < 2) {
        return false;  // Only one address family
    }

    // Check we have different address families
    bool has_ipv4 = false;
    bool has_ipv6 = false;
    for (const auto& address : address_list) {
        if (address->ip()->version() == Network::Address::IpVersion::v4) {
            has_ipv4 = true;
        } else if (address->ip()->version() == Network::Address::IpVersion::v6) {
            has_ipv6 = true;
        }
    }

    return has_ipv4 && has_ipv6;
}
```

### Attempting Alternate Address

```cpp
ConnectivityGrid::StreamCreationResult WrapperCallbacks::attemptSecondHttp3Connection() {
    has_tried_http3_alternate_address_ = true;

    // Get or create alternate HTTP/3 pool
    // This pool will use the second address in the list
    auto* pool = grid_.getOrCreateHttp3AlternativePool();
    if (!pool) {
        return StreamCreationResult::ImmediateResult;
    }

    // Try to create stream
    return newStream(*pool);
}
```

## HTTP/3 Broken Tracking

The grid tracks HTTP/3 health and temporarily disables it after failures.

### Marking HTTP/3 Broken

```cpp
void ConnectivityGrid::markHttp3Broken() {
    auto& tracker = getHttp3StatusTracker();

    // Calculate backoff duration (exponential)
    // First failure: 5 minutes
    // Second failure: 10 minutes
    // Third failure: 20 minutes
    // ...
    auto backoff_duration = tracker.calculateBackoffDuration();

    tracker.markHttp3Broken(time_source_.monotonicTime() + backoff_duration);

    ENVOY_LOG(warn, "HTTP/3 marked broken for {}ms", backoff_duration.count());
}

void WrapperCallbacks::maybeMarkHttp3Broken() {
    // Mark HTTP/3 broken if:
    // 1. HTTP/3 attempt failed
    // 2. TCP attempt succeeded
    // This indicates HTTP/3 is specifically broken, not network issues
    if (http3_attempt_failed_ && tcp_attempt_succeeded_) {
        grid_.markHttp3Broken();
    }
}
```

### Checking if HTTP/3 is Broken

```cpp
bool ConnectivityGrid::isHttp3Broken() const {
    auto& tracker = getHttp3StatusTracker();
    auto now = time_source_.monotonicTime();

    // Check if still in broken period
    if (tracker.isBroken(now)) {
        return true;
    }

    return false;
}
```

### Confirming HTTP/3 Working

```cpp
void ConnectivityGrid::onHandshakeComplete() {
    // HTTP/3 handshake succeeded - mark as working
    markHttp3Confirmed();
}

void ConnectivityGrid::markHttp3Confirmed() {
    auto& tracker = getHttp3StatusTracker();

    // Reset exponential backoff counter
    tracker.markHttp3Confirmed();

    ENVOY_LOG(info, "HTTP/3 confirmed working");
}
```

## Timeline Example

### Scenario: HTTP/3 timeout with TCP fallback

```
Time 0ms:   newStream() called
            → Start HTTP/3 attempt
            → Start timer (300ms)

Time 300ms: Timer fires (HTTP/3 still pending)
            → Start HTTP/2 attempt (fallback)
            → Start HTTP/3 happy eyeballs (if available)

Time 350ms: HTTP/2 connection established
            → onPoolReady(HTTP/2)
            → Cancel HTTP/3 attempts
            → Mark HTTP/3 broken (TCP worked but HTTP/3 didn't)
            → Return HTTP/2 encoder to caller

Time 400ms: HTTP/3 would have completed
            → Already cancelled, no effect
```

### Scenario: HTTP/3 success

```
Time 0ms:   newStream() called
            → Start HTTP/3 attempt
            → Start timer (300ms)

Time 150ms: HTTP/3 handshake complete
            → onPoolReady(HTTP/3)
            → Cancel timer
            → Mark HTTP/3 confirmed
            → Return HTTP/3 encoder to caller

Timer never fires - HTTP/3 succeeded before timeout
```

### Scenario: Both HTTP/3 and TCP fail

```
Time 0ms:   newStream() called
            → Start HTTP/3 attempt
            → Start timer (300ms)

Time 200ms: HTTP/3 fails (connection refused)
            → Start HTTP/2 attempt (failover)

Time 300ms: Timer fires
            → HTTP/2 already started, no action

Time 400ms: HTTP/2 fails (connection refused)
            → No more options
            → onPoolFailure() to caller
```

## Configuration

### Enabling HTTP/3

```yaml
clusters:
  - name: my_service
    type: STRICT_DNS
    dns_lookup_family: AUTO  # Try both IPv4 and IPv6 (for happy eyeballs)

    # Enable HTTP/3
    typed_extension_protocol_options:
      envoy.extensions.upstreams.http.v3.HttpProtocolOptions:
        auto_config:
          http3_protocol_options: {}  # Enable HTTP/3
          http2_protocol_options: {}  # Fallback to HTTP/2

        # Alternative: explicit HTTP/3
        explicit_http_config:
          http3_protocol_options: {}
```

### HTTP/3 Alternate Protocol Cache

```yaml
http_connection_manager:
  http_protocol_options:
    # Store HTTP/3 alt-svc advertisements
    common_http_protocol_options:
      http_server_properties_cache_options:
        max_entries: 1000
```

### Timeout Configuration

```yaml
clusters:
  - name: my_service
    # Timeout before TCP fallback (default: 300ms)
    connect_timeout: 0.3s
```

## Statistics

### HTTP/3 Stats

```
cluster.<name>.http3.quic_version_<version>   # QUIC version used
cluster.<name>.http3.request_streams          # Active HTTP/3 streams
cluster.<name>.http3.0rtt_accepted            # 0-RTT requests accepted
cluster.<name>.http3.0rtt_rejected            # 0-RTT requests rejected
```

### Grid Stats

```
cluster.<name>.upstream_cx_http3              # HTTP/3 connections created
cluster.<name>.upstream_cx_http2              # HTTP/2 connections created
cluster.<name>.upstream_cx_connect_attempts   # Total connection attempts
```

### Failure Stats

```
cluster.<name>.upstream_cx_connect_fail       # Connection failures
cluster.<name>.upstream_cx_connect_timeout    # Connection timeouts
```

## Best Practices

### 1. Configure Reasonable Timeouts

```yaml
# Balance between:
# - Too short: Unnecessarily fall back to TCP
# - Too long: Slow response for broken HTTP/3

connect_timeout: 300ms  # Good default for most cases
```

### 2. Enable Happy Eyeballs

```yaml
dns_lookup_family: AUTO  # Enables IPv4 + IPv6 happy eyeballs
```

### 3. Monitor HTTP/3 Health

```
# Track HTTP/3 vs TCP usage
http3_ratio = cluster.<name>.upstream_cx_http3 / cluster.<name>.upstream_cx_total

# Alert if HTTP/3 ratio drops unexpectedly
# Normal: > 80%
# Problem: < 50%
```

### 4. Handle 0-RTT Appropriately

```cpp
// Only send idempotent requests with 0-RTT
StreamOptions options;
options.can_send_early_data_ = request_is_idempotent_;  // GET, HEAD, PUT (idempotent)
```

### 5. Test Failover Scenarios

```bash
# Test HTTP/3 failure
# Block UDP port 443
iptables -A OUTPUT -p udp --dport 443 -j DROP

# Verify:
# 1. Requests still succeed (fall back to TCP)
# 2. HTTP/3 marked broken
# 3. After timeout, HTTP/3 retried
```

## Debugging

### Enable Trace Logging

```yaml
static_resources:
  listeners:
    - name: listener_0
      filter_chains:
        - filters:
            - name: envoy.filters.network.http_connection_manager
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
                # Trace-level logging for connection pool
                access_log:
                  - name: envoy.access_loggers.file
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
                      path: "/dev/stdout"
```

### Check HTTP/3 Status

```bash
# Query admin interface
curl localhost:9901/stats | grep http3

# Look for:
# - cluster.<name>.http3.*
# - HTTP/3 connection counts
# - 0-RTT stats
```

### Verify Alternate Protocols

```bash
# Check alt-svc headers in responses
curl -v https://example.com

# Look for:
# alt-svc: h3=":443"; ma=86400
```

## Summary

The ConnectivityGrid provides intelligent HTTP/3 support with:

- **Automatic failover** from HTTP/3 to HTTP/2
- **Happy Eyeballs** for IPv4/IPv6
- **Broken protocol tracking** with exponential backoff
- **Transparent to callers** - single `newStream()` API
- **Performance optimization** - HTTP/3 when available, TCP when needed

**Key Benefits**:
- **0-RTT support** - Faster connection establishment
- **Better performance** - QUIC's advantages (multiplexing, no head-of-line blocking)
- **Graceful degradation** - Falls back to TCP when HTTP/3 unavailable
- **Resilient** - Automatically recovers from transient HTTP/3 issues

**Next**: See `conn_pool_base.h` for the underlying pool implementation.
