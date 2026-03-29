# HTTP Context Implementation

**File**: `/source/common/http/context_impl.h`
**Interface**: `/envoy/http/context.h`
**Purpose**: Server-wide HTTP context holding shared HTTP infrastructure

## Overview

The `ContextImpl` provides:
- **Singleton HTTP infrastructure** - One instance per server
- **Code statistics** - Shared HTTP status code tracking
- **User agent detection** - Identify and categorize user agents
- **Tracing configuration** - Default distributed tracing settings
- **Async client stats** - Stat prefix for internal HTTP clients

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    Envoy Server                           │
│                         │                                  │
│          ┌──────────────┴──────────────┐                 │
│          │                               │                 │
│    ┌─────▼──────┐              ┌───────▼──────┐          │
│    │  Listener  │              │   Cluster    │          │
│    │    (HCM)   │              │   Manager    │          │
│    └─────┬──────┘              └───────┬──────┘          │
│          │                               │                 │
│          └──────────────┬──────────────┘                 │
│                         │                                  │
│              ┌──────────▼───────────┐                     │
│              │   HTTP ContextImpl   │                     │
│              │  (Server Singleton)  │                     │
│              └──────────────────────┘                     │
│                         │                                  │
│        ┌────────────────┼────────────────┐               │
│        │                 │                 │               │
│   ┌────▼────┐    ┌──────▼──────┐   ┌─────▼──────┐       │
│   │  Code   │    │ User Agent  │   │  Tracing   │       │
│   │  Stats  │    │   Context   │   │   Config   │       │
│   └─────────┘    └─────────────┘   └────────────┘       │
└──────────────────────────────────────────────────────────┘
```

## Class Definition

### ContextImpl

```cpp
class ContextImpl : public Context {
public:
    // Constructor - initializes all shared HTTP infrastructure
    explicit ContextImpl(Stats::SymbolTable& symbol_table);

    ~ContextImpl() override = default;

    // Get default tracing configuration
    const envoy::config::trace::v3::Tracing& defaultTracingConfig() override {
        return default_tracing_config_;
    }

    // Get code stats implementation
    CodeStats& codeStats() override {
        return code_stats_;
    }

    // Set default tracing config (called during server initialization)
    void setDefaultTracingConfig(
        const envoy::config::trace::v3::Tracing& tracing_config
    ) {
        default_tracing_config_ = tracing_config;
    }

    // Get user agent context for detection/categorization
    const UserAgentContext& userAgentContext() const override {
        return user_agent_context_;
    }

    // Get stat prefix for async HTTP clients
    const Stats::StatName& asyncClientStatPrefix() const override {
        return async_client_stat_prefix_;
    }

private:
    // Shared code statistics implementation
    CodeStatsImpl code_stats_;

    // User agent detection and categorization
    UserAgentContext user_agent_context_;

    // Stat prefix for async clients ("http.async_client")
    const Stats::StatName async_client_stat_prefix_;

    // Default tracing configuration
    envoy::config::trace::v3::Tracing default_tracing_config_;
};
```

## Components

### 1. Code Statistics

Shared HTTP status code tracking for all connections.

```cpp
// Initialized with symbol table
CodeStatsImpl code_stats_;

// Usage in connection manager
void ConnectionManagerImpl::chargeResponseStats() {
    // Access shared code stats via context
    config_->http_context_.codeStats().chargeResponseStat(
        stat_info,
        exclude_http_code_stats
    );
}
```

**Benefits of Shared Instance**:
- Single symbol table allocation for all codes
- Consistent stat naming across all listeners
- Efficient memory usage (no per-listener duplication)

### 2. User Agent Context

Detects and categorizes user agents for routing/stats.

```cpp
// User agent detection
UserAgentContext user_agent_context_;

// Usage in connection manager
void ConnectionManagerImpl::onRequestHeaders(RequestHeaderMap& headers) {
    auto user_agent = headers.UserAgent();
    if (user_agent) {
        auto type = config_->http_context_.userAgentContext()
                        .getUserAgentType(user_agent->value().getStringView());

        // Use type for routing or stats
        if (type == UserAgentType::Mobile) {
            // Mobile-specific handling
        }
    }
}
```

**User Agent Types**:
```cpp
enum class UserAgentType {
    Desktop,
    Mobile,
    Bot,
    Unknown
};
```

**Detection Patterns**:
- **Mobile**: `iPhone`, `Android`, `Mobile Safari`
- **Bot**: `Googlebot`, `bingbot`, `crawler`
- **Desktop**: Default for non-mobile, non-bot

### 3. Tracing Configuration

Default distributed tracing settings.

```cpp
// Default tracing config
envoy::config::trace::v3::Tracing default_tracing_config_;

// Set during server initialization
void ServerImpl::initialize() {
    http_context_->setDefaultTracingConfig(bootstrap_.tracing());
}

// Used by connection managers
void ConnectionManagerImpl::initializeTracing() {
    // Use default config if not overridden
    if (!config_->tracingConfig()) {
        config_->setTracingConfig(
            config_->http_context_.defaultTracingConfig()
        );
    }
}
```

**Tracing Config Example**:
```yaml
tracing:
  http:
    name: envoy.tracers.zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: zipkin
      collector_endpoint: "/api/v2/spans"
```

### 4. Async Client Stat Prefix

Consistent stat naming for internal HTTP clients.

```cpp
// Pre-allocated stat name "http.async_client"
const Stats::StatName async_client_stat_prefix_;

// Usage in async client
Http::AsyncClientImpl::AsyncClientImpl(Cluster& cluster, Context& http_context) {
    // Use shared prefix for stats
    stats_prefix_ = http_context.asyncClientStatPrefix();

    // Stats will be:
    // http.async_client.<cluster>.upstream_rq_total
    // http.async_client.<cluster>.upstream_rq_2xx
    // etc.
}
```

## Lifecycle

### Server Initialization

```cpp
// 1. Create HTTP context (early in server init)
auto http_context = std::make_unique<Http::ContextImpl>(symbol_table_);

// 2. Set default tracing config
http_context->setDefaultTracingConfig(bootstrap_.tracing());

// 3. Store in server instance
server.http_context_ = std::move(http_context);

// 4. Pass to all HTTP components
auto hcm = std::make_unique<Http::ConnectionManagerImpl>(
    config,
    server.http_context_,  // Shared context
    // ... other args
);
```

### Connection Manager Usage

```cpp
class ConnectionManagerImpl {
public:
    ConnectionManagerImpl(
        Config& config,
        Http::Context& http_context,  // Receives shared context
        // ... other args
    ) : config_(config), http_context_(http_context) {}

    void onResponseComplete() {
        // Use shared code stats
        http_context_.codeStats().chargeResponseStat(stat_info);
    }

    void onRequestHeaders(RequestHeaderMap& headers) {
        // Use shared user agent detection
        auto type = http_context_.userAgentContext()
                        .getUserAgentType(user_agent_string);
    }

private:
    Http::Context& http_context_;  // Reference to shared context
};
```

### Async Client Usage

```cpp
class AsyncClientImpl {
public:
    AsyncClientImpl(Upstream::Cluster& cluster, Http::Context& http_context)
        : cluster_(cluster), http_context_(http_context) {

        // Use shared stat prefix
        auto prefix = http_context_.asyncClientStatPrefix();

        // Create stats: http.async_client.<cluster>.*
        stats_ = Http::AsyncClientStats{
            {prefix, cluster_.statsScope()},
            // ... stat definitions
        };
    }

private:
    Http::Context& http_context_;
};
```

## Usage Examples

### Example 1: Charging Response Stats

```cpp
void RouterFilter::onUpstreamComplete() {
    // Build stat info
    CodeStats::ResponseStatInfo stat_info{
        .global_scope_ = config_->scope(),
        .cluster_scope_ = cluster_->statsScope(),
        .prefix_ = config_->statPrefix(),
        .response_status_code_ = response_code_,
        .internal_request_ = is_internal_,
        .request_vhost_name_ = vhost_stat_name_,
        .request_vcluster_name_ = vcluster_stat_name_,
        .request_route_name_ = route_stat_name_,
        .from_zone_ = local_zone_,
        .to_zone_ = upstream_zone_,
        .upstream_canary_ = upstream_->canary()
    };

    // Charge stats via shared context
    callbacks_->streamInfo().getFilterState()
        .getDataReadOnly<Http::Context>(Http::ContextFilterStateName)
        ->codeStats()
        .chargeResponseStat(stat_info, exclude_http_code_stats_);
}
```

### Example 2: User Agent Detection

```cpp
void RateLimitFilter::decodeHeaders(RequestHeaderMap& headers, bool) {
    // Get user agent from request
    auto user_agent_header = headers.UserAgent();
    if (!user_agent_header) {
        return FilterHeadersStatus::Continue;
    }

    // Detect user agent type via shared context
    auto user_agent_type = config_->http_context()
                               .userAgentContext()
                               .getUserAgentType(
                                   user_agent_header->value().getStringView()
                               );

    // Apply different rate limits
    uint32_t rate_limit;
    switch (user_agent_type) {
    case UserAgentType::Bot:
        rate_limit = 10;  // Stricter for bots
        break;
    case UserAgentType::Mobile:
        rate_limit = 100;
        break;
    default:
        rate_limit = 500;  // Generous for regular users
        break;
    }

    if (isRateLimited(rate_limit)) {
        decoder_callbacks_->sendLocalReply(
            Http::Code::TooManyRequests,
            "Rate limit exceeded",
            nullptr, absl::nullopt, "rate_limited"
        );
        return FilterHeadersStatus::StopIteration;
    }

    return FilterHeadersStatus::Continue;
}
```

### Example 3: Default Tracing

```cpp
void ConnectionManagerConfig::initTracing() {
    // Check if tracing configured for this listener
    if (config_proto_.has_tracing()) {
        tracing_config_ = config_proto_.tracing();
    } else {
        // Use default tracing from HTTP context
        tracing_config_ = http_context_.defaultTracingConfig();
    }

    // Initialize tracer
    if (tracing_config_.has_http()) {
        tracer_ = tracerFactory()->createHttpTracer(
            tracing_config_.http(),
            server_,
            cluster_manager_
        );
    }
}
```

### Example 4: Async Client Stats

```cpp
void FilterImpl::makeAsyncRequest() {
    // Create async client (uses shared context)
    auto async_client = cluster_manager_.getThreadLocalCluster(cluster_name_)
                            ->httpAsyncClient();

    // Stats automatically use shared prefix:
    // http.async_client.<cluster>.upstream_rq_total
    // http.async_client.<cluster>.upstream_rq_2xx
    // http.async_client.<cluster>.upstream_rq_time

    auto request = Http::RequestMessageImpl::create();
    request->headers().setMethod("GET");
    request->headers().setPath("/api/resource");

    async_client.send(
        std::move(request),
        *this,  // Callbacks
        Http::AsyncClient::RequestOptions()
    );
}
```

## Benefits of Shared Context

### 1. Memory Efficiency

**Without Shared Context**:
```
Each listener: 10KB for code stats
100 listeners: 1MB total
```

**With Shared Context**:
```
Single instance: 10KB
All listeners: 10KB total
Savings: 990KB
```

### 2. Consistent Stat Naming

All components use the same stat names:
```
# All listeners report to same counters
cluster.my_cluster.upstream_rq_2xx
cluster.my_cluster.upstream_rq_200

# All async clients use same prefix
http.async_client.auth_cluster.upstream_rq_total
http.async_client.rate_limit_cluster.upstream_rq_total
```

### 3. Centralized Configuration

Update tracing once, affects all listeners:
```cpp
// Update default tracing
server.http_context_->setDefaultTracingConfig(new_config);

// All listeners without explicit tracing config now use new default
```

### 4. Simplified Testing

Mock once, test all components:
```cpp
class MockHttpContext : public Http::Context {
public:
    MOCK_METHOD(CodeStats&, codeStats, ());
    MOCK_METHOD(const UserAgentContext&, userAgentContext, (), (const));
    // ...
};

// Use in all component tests
TEST_F(ConnectionManagerTest, ResponseStats) {
    MockHttpContext http_context;
    EXPECT_CALL(http_context, codeStats()).WillOnce(Return(mock_code_stats_));

    auto cm = std::make_unique<ConnectionManagerImpl>(config_, http_context);
    // ...
}
```

## Configuration

### Server-Wide Tracing

```yaml
# bootstrap.yaml
static_resources:
  # ... clusters, listeners

# Default tracing for all listeners
tracing:
  http:
    name: envoy.tracers.zipkin
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.ZipkinConfig
      collector_cluster: zipkin
      collector_endpoint: "/api/v2/spans"
      trace_id_128bit: true
      shared_span_context: false
```

### Per-Listener Override

```yaml
listeners:
  - name: listener_0
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 10000
    filter_chains:
      - filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              # Override default tracing for this listener
              tracing:
                provider:
                  name: envoy.tracers.lightstep
                  typed_config:
                    "@type": type.googleapis.com/envoy.config.trace.v3.LightstepConfig
                    collector_cluster: lightstep
```

## Best Practices

### 1. Initialize Context Early

```cpp
// During server construction (before any HTTP components)
auto http_context = std::make_unique<Http::ContextImpl>(symbol_table_);
server.setHttpContext(std::move(http_context));
```

### 2. Pass by Reference

```cpp
// Good: Pass context by reference (lightweight)
ConnectionManagerImpl(Config& config, Http::Context& http_context);

// Bad: Pass by value (copies)
ConnectionManagerImpl(Config& config, Http::Context http_context);
```

### 3. Don't Store Copies

```cpp
// Good: Store reference
class MyFilter {
    Http::Context& http_context_;
};

// Bad: Store copy (wastes memory)
class MyFilter {
    Http::ContextImpl http_context_;  // Don't do this!
};
```

### 4. Use for All HTTP Components

Ensure all HTTP components receive the context:
- Connection managers
- HTTP filters
- Async clients
- Health checkers
- Routers

## Debugging

### Check Context Initialization

```cpp
// Verify context is set
ASSERT(server.httpContext() != nullptr);

// Verify code stats initialized
ASSERT(&server.httpContext()->codeStats() != nullptr);
```

### Verify Stat Sharing

```bash
# Query admin interface
curl localhost:9901/stats | grep "cluster.my_cluster.upstream_rq"

# Should see stats from all listeners aggregated
cluster.my_cluster.upstream_rq_2xx: 12345
cluster.my_cluster.upstream_rq_200: 12000
```

### Check User Agent Detection

```cpp
// Enable debug logging
ENVOY_LOG(debug, "User agent: {} Type: {}",
          user_agent_string,
          static_cast<int>(user_agent_type));
```

## Testing

### Unit Test Example

```cpp
TEST_F(ContextImplTest, CodeStats) {
    Stats::TestSymbolTable symbol_table;
    Http::ContextImpl context(symbol_table.symbolTable());

    // Verify code stats accessible
    auto& code_stats = context.codeStats();
    EXPECT_NE(&code_stats, nullptr);

    // Test stat charging
    Stats::TestScope scope;
    code_stats.chargeBasicResponseStat(
        scope,
        symbol_table.symbolTable().makeStatName("test"),
        Http::Code::OK,
        false
    );

    EXPECT_EQ(1, scope.counterFromString("test.upstream_rq_2xx").value());
}

TEST_F(ContextImplTest, UserAgentContext) {
    Stats::TestSymbolTable symbol_table;
    Http::ContextImpl context(symbol_table.symbolTable());

    auto& ua_context = context.userAgentContext();

    // Test mobile detection
    auto type = ua_context.getUserAgentType("Mozilla/5.0 (iPhone; ...)");
    EXPECT_EQ(UserAgentType::Mobile, type);

    // Test bot detection
    type = ua_context.getUserAgentType("Googlebot/2.1");
    EXPECT_EQ(UserAgentType::Bot, type);
}
```

## Summary

The HTTP Context provides:

- **Singleton HTTP infrastructure** - Single instance per server
- **Code statistics** - Efficient, shared status code tracking
- **User agent detection** - Categorize clients for routing/rate limiting
- **Tracing configuration** - Default distributed tracing settings
- **Async client stats** - Consistent stat naming for internal clients

**Key Benefits**:
- **Memory efficient** - No per-listener duplication
- **Consistent stats** - All components use same counters
- **Centralized config** - Single place for HTTP-wide settings
- **Simplified testing** - Mock once, use everywhere

**Usage Pattern**:
```cpp
// 1. Create during server init
auto context = std::make_unique<Http::ContextImpl>(symbol_table);

// 2. Pass to all HTTP components
auto hcm = std::make_unique<ConnectionManagerImpl>(config, *context);
auto async_client = std::make_unique<AsyncClientImpl>(cluster, *context);

// 3. Access shared infrastructure
context->codeStats().chargeResponseStat(...);
context->userAgentContext().getUserAgentType(...);
```
