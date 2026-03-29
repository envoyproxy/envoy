# Part 5: HTTP Filter Chain Creation

## Table of Contents
1. [Introduction](#introduction)
2. [HTTP Filter Chain Overview](#http-filter-chain-overview)
3. [HTTP Connection Manager](#http-connection-manager)
4. [Filter Chain Helper](#filter-chain-helper)
5. [HTTP Filter Instantiation](#http-filter-instantiation)
6. [HTTP Filter Manager](#http-filter-manager)
7. [Per-Route Configuration](#per-route-configuration)
8. [Complete HTTP Flow](#complete-http-flow)

## Introduction

HTTP filters operate at the L7 (application) layer and are created per HTTP stream. HTTP filters run within the HTTP Connection Manager network filter, which itself is part of the network filter chain.

## HTTP Filter Chain Overview

### HTTP Filter Processing Model

```
┌──────────────────────────────────────────────────────────────┐
│             HTTP FILTER CHAIN PROCESSING                     │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Network Layer (Connection)                                  │
│  ┌────────────────────────────────────────────────────┐    │
│  │  HTTP Connection Manager (Network Filter)          │    │
│  │  ├─ Decodes HTTP/1.1, HTTP/2, HTTP/3              │    │
│  │  ├─ Manages HTTP streams                           │    │
│  │  └─ Contains HTTP filter chain factory             │    │
│  └────────────────┬───────────────────────────────────┘    │
│                   │                                          │
│                   ▼                                          │
│  HTTP Stream Layer (Per Request)                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  HTTP Filter Chain (Per Stream)                    │    │
│  │                                                     │    │
│  │  Request Path (Decoder Filters) - FIFO:            │    │
│  │  ┌──────────────────────────────────────────┐     │    │
│  │  │ Filter A  │ Filter B  │ Filter C         │     │    │
│  │  │  (CORS)   │  (JWT)    │  (Router)        │     │    │
│  │  └──────────────────────────────────────────┘     │    │
│  │      │           │            │                    │    │
│  │      ▼           ▼            ▼                    │    │
│  │  decodeHeaders() → decodeData() → decodeTrailers() │    │
│  │                                                     │    │
│  │  Response Path (Encoder Filters) - LIFO:           │    │
│  │  ┌──────────────────────────────────────────┐     │    │
│  │  │ Filter C  │ Filter B  │ Filter A         │     │    │
│  │  │  (Router) │  (JWT)    │  (CORS)          │     │    │
│  │  └──────────────────────────────────────────┘     │    │
│  │      │           │            │                    │    │
│  │      ▼           ▼            ▼                    │    │
│  │  encodeHeaders() ← encodeData() ← encodeTrailers() │    │
│  │                                                     │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Layer Relationship

```
┌────────────────────────────────────────────────────────┐
│  Network Connection (1 connection, many streams)       │
│  ┌──────────────────────────────────────────────────┐ │
│  │  Network Filter Chain                             │ │
│  │  ┌─────────────────────────────────────────────┐ │ │
│  │  │ HTTP Connection Manager                      │ │ │
│  │  │                                              │ │ │
│  │  │  HTTP Stream 1                               │ │ │
│  │  │  ┌─────────────────────────────────────┐   │ │ │
│  │  │  │ HTTP Filter Chain (Stream 1)         │   │ │ │
│  │  │  │ [CORS, JWT, Router]                  │   │ │ │
│  │  │  └─────────────────────────────────────┘   │ │ │
│  │  │                                              │ │ │
│  │  │  HTTP Stream 2                               │ │ │
│  │  │  ┌─────────────────────────────────────┐   │ │ │
│  │  │  │ HTTP Filter Chain (Stream 2)         │   │ │ │
│  │  │  │ [CORS, JWT, Router]                  │   │ │ │
│  │  │  └─────────────────────────────────────┘   │ │ │
│  │  │                                              │ │ │
│  │  │  HTTP Stream N                               │ │ │
│  │  │  ┌─────────────────────────────────────┐   │ │ │
│  │  │  │ HTTP Filter Chain (Stream N)         │   │ │ │
│  │  │  └─────────────────────────────────────┘   │ │ │
│  │  └──────────────────────────────────────────────┘ │ │
│  └──────────────────────────────────────────────────┘ │
└────────────────────────────────────────────────────────┘
```

## HTTP Connection Manager

### HttpConnectionManager Interface

```cpp
// source/common/http/conn_manager_impl.h

class ConnectionManagerImpl : public Network::ReadFilter, public Http::FilterChainFactory {
public:
  // Network filter interface (L4)
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;

  // HTTP filter chain factory interface (L7)
  bool createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;
  bool createUpgradeFilterChain(...) override;

private:
  // HTTP filter configuration
  Http::FilterChainHelper filter_chain_helper_;

  // Active streams
  std::list<ActiveStreamPtr> streams_;
};
```

### HTTP Connection Manager Configuration

```cpp
// source/common/http/conn_manager_config.h

class HttpConnectionManagerConfig {
public:
  HttpConnectionManagerConfig( const envoy::extensions::filters::network::http_connection_manager::v3
          ::HttpConnectionManager& config, Server::Configuration::FactoryContext& context, Http::DateProvider& date_provider, Router::RouteConfigProviderSharedPtr route_config_provider);

  // Get HTTP filter configuration
  const Http::FilterChainFactory& filterFactory() const {
    return filter_factory_;
  }

  // Get route configuration
  Router::RouteConfigProvider& routeConfigProvider() const {
    return *route_config_provider_;
  }

private:
  // HTTP filter chain factory
  Http::FilterChainHelper filter_factory_;

  // Route configuration provider
  Router::RouteConfigProviderSharedPtr route_config_provider_;
};
```

## Filter Chain Helper

### FilterChainHelper Implementation

The FilterChainHelper processes HTTP filter configuration and creates filter factory callbacks.

```cpp
// source/common/http/filter_chain_helper.h

class FilterChainHelper {
public:
  // Process HTTP filter configuration
  absl::Status processFilters( const Protobuf::RepeatedPtrField<
          envoy::extensions::filters::network::http_connection_manager::v3
              ::HttpFilter>& filters, const std::string& prefix, FilterChainFactoryContextCreator& context_creator);

  // Create filter chain for a stream
  bool createFilterChain( Http::FilterChainFactoryCallbacks& callbacks, bool only_create_if_health_check_request = false) const;

  // Create upgrade filter chain (WebSocket, CONNECT)
  bool createUpgradeFilterChain(...) const;

private:
  // Filter factory providers
  using FilterFactoryProvider =
      DynamicExtensionConfigProvider<
          Http::FilterFactoryCb, Server::Configuration::NamedHttpFilterConfigFactory>;

  struct FilterConfig { FilterConfigProviderPtr<Http::FilterFactoryCb> filter_factory_provider;
    bool disabled{false};
    bool is_terminal{false};
  };

  // List of filter configurations
  std::vector<FilterConfig> filter_factories_;

  // Dependency manager (validates filter dependencies)
  std::unique_ptr<FilterChainUtility::FilterChainDependencyManager> dependency_manager_;
};
```

### Processing HTTP Filter Configuration

```cpp
absl::Status FilterChainHelper::processFilters( const Protobuf::RepeatedPtrField<
        envoy::extensions::filters::network::http_connection_manager::v3
            ::HttpFilter>& filters, const std::string& prefix, FilterChainFactoryContextCreator& context_creator) {

  // Validate and process each filter
  for (const auto& filter_config : filters) { const std::string& filter_name = filter_config.name();

    // Get filter factory from registry
    auto* factory = Registry::FactoryRegistry<
        Server::Configuration::NamedHttpFilterConfigFactory>::getFactory( filter_name);

    if (factory == nullptr) {
      return absl::InvalidArgumentError( fmt::format("Didn't find a registered filter factory for '{}'", filter_name));
    }

    // Create filter factory callback
    auto filter_factory_cb_or_error =
        factory->createFilterFactoryFromProto( filter_config.typed_config(), prefix, context_creator.createFilterChainFactoryContext());

    if (!filter_factory_cb_or_error.ok()) {
      return filter_factory_cb_or_error.status();
    }

    // Store filter configuration
    FilterConfig config;
    config.filter_factory_provider =
        std::make_unique<StaticFilterConfigProvider>( std::move(filter_factory_cb_or_error.value()), filter_name);
    config.disabled = filter_config.disabled();
    config.is_terminal = factory->isTerminalFilterByProto( filter_config.typed_config(), context);

    filter_factories_.push_back(std::move(config));
  }

  // Validate filter chain (terminal filter placement, dependencies)
  return validateFilterChain();
}
```

## HTTP Filter Instantiation

### Creating Filter Chain for a Stream

```cpp
bool FilterChainHelper::createFilterChain( Http::FilterChainFactoryCallbacks& callbacks, bool only_create_if_health_check_request) const {

  bool is_health_check_request = /* check if health check */;

  // Iterate through configured filters
  for (const auto& filter_config : filter_factories_) {
    // Skip if filter is disabled
    if (filter_config.disabled) { continue;
    }

    // Skip non-health-check filters if only creating health check chain
    if (only_create_if_health_check_request && !is_health_check_request) { continue;
    }

    // Get filter factory callback
    Http::FilterFactoryCb filter_factory_cb =
        filter_config.filter_factory_provider->getConfig();

    // Invoke callback to create and install filter
    // Callback will call callbacks.addStreamDecoderFilter/addStreamEncoderFilter
    filter_factory_cb(callbacks);
  }

  return true;
}
```

### Filter Factory Callback Examples

```cpp
// Example 1: Decoder-only filter (CORS)
Http::FilterFactoryCb cors_factory =
    [config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<CorsFilter>(config);
      callbacks.addStreamDecoderFilter(filter);
    };

// Example 2: Encoder-only filter (Compression)
Http::FilterFactoryCb compression_factory =
    [config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<CompressionFilter>(config);
      callbacks.addStreamEncoderFilter(filter);
    };

// Example 3: Dual filter (Fault Injection)
Http::FilterFactoryCb fault_factory =
    [config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<FaultFilter>(config);
      callbacks.addStreamDecoderFilter(filter);
      callbacks.addStreamEncoderFilter(filter);
    };

// Example 4: StreamFilter (both decoder and encoder)
Http::FilterFactoryCb buffer_factory =
    [config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<BufferFilter>(config);
      callbacks.addStreamFilter(filter);  // Convenience method
    };

// Example 5: Terminal filter (Router)
Http::FilterFactoryCb router_factory =
    [config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<Router::RouterFilter>(config);
      callbacks.addStreamDecoderFilter(filter);
      callbacks.addStreamEncoderFilter(filter);
    };
```

## HTTP Filter Manager

### FilterManager Interface

```cpp
// source/common/http/filter_manager.h

class FilterManager : public Http::FilterChainFactoryCallbacks {
public:
  // FilterChainFactoryCallbacks implementation
  void addStreamDecoderFilter( Http::StreamDecoderFilterSharedPtr filter) override;

  void addStreamEncoderFilter( Http::StreamEncoderFilterSharedPtr filter) override;

  void addStreamFilter( Http::StreamFilterSharedPtr filter) override;

  void addAccessLogHandler( AccessLog::InstanceSharedPtr handler) override;

  // Decode path iteration
  void decodeHeaders(RequestHeaderMapPtr&& headers, bool end_stream);
  void decodeData(Buffer::Instance& data, bool end_stream);
  void decodeTrailers(RequestTrailerMapPtr&& trailers);

  // Encode path iteration
  void encodeHeaders(ResponseHeaderMapPtr&& headers, bool end_stream);
  void encodeData(Buffer::Instance& data, bool end_stream);
  void encodeTrailers(ResponseTrailerMapPtr&& trailers);

private:
  // Active filter wrappers
  struct ActiveStreamDecoderFilter { StreamDecoderFilterSharedPtr filter_;
    FilterIterationState iteration_state_;
    bool headers_continued_{false};
    bool headers_only_{false};
  };

  struct ActiveStreamEncoderFilter { StreamEncoderFilterSharedPtr filter_;
    FilterIterationState iteration_state_;
    bool headers_continued_{false};
  };

  // Filter lists
  std::list<ActiveStreamDecoderFilter> decoder_filters_;
  std::list<ActiveStreamEncoderFilter> encoder_filters_;

  // Current iteration state
  std::list<ActiveStreamDecoderFilter>::iterator current_decoder_filter_;
  std::list<ActiveStreamEncoderFilter>::reverse_iterator current_encoder_filter_;
};
```

### Adding HTTP Filters

```cpp
void FilterManager::addStreamDecoderFilter( Http::StreamDecoderFilterSharedPtr filter) {

  ActiveStreamDecoderFilter active_filter;
  active_filter.filter_ = filter;
  active_filter.iteration_state_ = FilterIterationState::Continue;

  // Add to decoder filter list
  decoder_filters_.push_back(std::move(active_filter));

  // Initialize filter callbacks
  filter->setDecoderFilterCallbacks(*this);
}

void FilterManager::addStreamEncoderFilter( Http::StreamEncoderFilterSharedPtr filter) {

  ActiveStreamEncoderFilter active_filter;
  active_filter.filter_ = filter;
  active_filter.iteration_state_ = FilterIterationState::Continue;

  // Add to encoder filter list
  encoder_filters_.push_back(std::move(active_filter));

  // Initialize filter callbacks
  filter->setEncoderFilterCallbacks(*this);
}

void FilterManager::addStreamFilter( Http::StreamFilterSharedPtr filter) {
  // Add as both decoder and encoder
  addStreamDecoderFilter(filter);
  addStreamEncoderFilter(filter);
}
```

### Decoder Filter Iteration (FIFO)

```cpp
void FilterManager::decodeHeaders( RequestHeaderMapPtr&& headers, bool end_stream) {

  // Iterate through decoder filters in FIFO order
  for (auto it = decoder_filters_.begin();
       it != decoder_filters_.end();
       ++it) {

    // Skip if filter already processed this frame
    if (it->iteration_state_ == FilterIterationState::StopIteration) { continue;
    }

    // Call decodeHeaders on filter
    FilterHeadersStatus status =
        it->filter_->decodeHeaders(*headers, end_stream);

    // Handle return status
    switch (status) { case FilterHeadersStatus::Continue:
        // Continue to next filter
        it->iteration_state_ = FilterIterationState::Continue;
        continue;

      case FilterHeadersStatus::StopIteration:
        // Stop iteration, can resume later
        it->iteration_state_ = FilterIterationState::StopIteration;
        current_decoder_filter_ = it;
        return;

      case FilterHeadersStatus::StopAllIterationAndBuffer:
        // Stop and buffer all subsequent data
        it->iteration_state_ = FilterIterationState::StopIteration;
        current_decoder_filter_ = it;
        buffering_ = true;
        return;

      case FilterHeadersStatus::StopAllIterationAndWatermark:
        // Stop and apply backpressure
        it->iteration_state_ = FilterIterationState::StopIteration;
        current_decoder_filter_ = it;
        buffering_ = true;
        high_watermark_ = true;
        return;
    }
  }

  // All filters processed, continue to upstream
}
```

### Encoder Filter Iteration (LIFO)

```cpp
void FilterManager::encodeHeaders( ResponseHeaderMapPtr&& headers, bool end_stream) {

  // Iterate through encoder filters in LIFO order (reverse)
  for (auto it = encoder_filters_.rbegin();
       it != encoder_filters_.rend();
       ++it) {

    if (it->iteration_state_ == FilterIterationState::StopIteration) { continue;
    }

    // Call encodeHeaders on filter
    FilterHeadersStatus status =
        it->filter_->encodeHeaders(*headers, end_stream);

    // Handle return status (same as decoder)
    switch (status) { case FilterHeadersStatus::Continue:
        it->iteration_state_ = FilterIterationState::Continue;
        continue;

      case FilterHeadersStatus::StopIteration:
        it->iteration_state_ = FilterIterationState::StopIteration;
        current_encoder_filter_ = it;
        return;

      // ... other cases
    }
  }

  // All filters processed, send downstream
}
```

### Filter Iteration Diagram

```
Request Path (Decoder Filters): FIFO

Client Request
      │
      ▼
┌──────────────┐
│  Filter A    │  decodeHeaders(req, false)
│  (CORS)      │  ├─ Validate origin
└──────┬───────┘  └─ Return Continue
       │
       ▼
┌──────────────┐
│  Filter B    │  decodeHeaders(req, false)
│  (JWT Auth)  │  ├─ Verify token
└──────┬───────┘  └─ Return Continue
       │
       ▼
┌──────────────┐
│  Filter C    │  decodeHeaders(req, false)
│  (Router)    │  ├─ Select upstream
└──────┬───────┘  └─ Return StopIteration
       │              (initiates upstream request)
       ▼
   Upstream


Response Path (Encoder Filters): LIFO

   Upstream
       │
       ▼
┌──────────────┐
│  Filter C    │  encodeHeaders(resp, true)
│  (Router)    │  ├─ Update headers
└──────┬───────┘  └─ Return Continue
       │
       ▼
┌──────────────┐
│  Filter B    │  encodeHeaders(resp, true)
│  (JWT Auth)  │  ├─ No-op for response
└──────┬───────┘  └─ Return Continue
       │
       ▼
┌──────────────┐
│  Filter A    │  encodeHeaders(resp, true)
│  (CORS)      │  ├─ Add CORS headers
└──────┬───────┘  └─ Return Continue
       │
       ▼
Client Response
```

## Per-Route Configuration

### Route-Specific Filter Configuration

HTTP filters can have different configurations per route.

```cpp
// Example: CORS filter with per-route config

class CorsFilter : public Http::StreamDecoderFilter {
public:
  FilterHeadersStatus decodeHeaders( RequestHeaderMap& headers, bool end_stream) override {

    // Get per-route config (if exists)
    const auto* per_route_config =
        Http::Utility::resolveMostSpecificPerFilterConfig<CorsFilterPerRouteConfig>( "envoy.filters.http.cors", decoder_callbacks_->route());

    // Use per-route config if available, otherwise use default
    const CorsConfig& config = per_route_config
        ? per_route_config->config()
        : default_config_;

    // Apply CORS policy from config
    if (config.enabled()) { addCorsHeaders(headers, config);
    }

    return FilterHeadersStatus::Continue;
  }

private:
  const CorsConfig& default_config_;
};
```

### Per-Route Configuration Flow

```
┌──────────────────────────────────────────────────────────────┐
│         PER-ROUTE CONFIGURATION RESOLUTION                   │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Request arrives with path: /api/v1/users                │
│                                                              │
│  2. Router filter matches route:                             │
│     match:                                                   │
│       prefix: "/api/v1"                                      │
│     route:                                                   │
│       cluster: api_cluster                                   │
│     typed_per_filter_config:                                 │
│       envoy.filters.http.cors:                               │
│         allow_origin: ["https://app.example.com"]            │
│         allow_methods: "GET, POST, PUT"                      │
│                                                              │
│  3. CORS filter calls:                                       │
│     decoder_callbacks_->route()                              │
│       └─► Returns matched route                             │
│                                                              │
│  4. Filter resolves config:                                  │
│     resolveMostSpecificPerFilterConfig<CorsConfig>(          │
│         "envoy.filters.http.cors", route)                    │
│       └─► Returns per-route CORS config                     │
│                                                              │
│  5. Filter applies per-route config:                         │
│     ├─ Use per-route allow_origin                           │
│     └─ Use per-route allow_methods                          │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Dynamic Filter Enabling/Disabling

Filters can be enabled or disabled per route:

```yaml
# Listener config: Enable JWT filter
http_filters:
- name: envoy.filters.http.jwt_authn
  typed_config: {...}
- name: envoy.filters.http.router

# Route config: Disable JWT filter for public endpoints
routes:
- match:
    prefix: "/public"
  route:
    cluster: backend
  typed_per_filter_config:
    envoy.filters.http.jwt_authn:
      "@type": type.googleapis.com/envoy.config.route.v3.FilterConfig
      disabled: true  # Skip JWT validation for /public endpoints

- match:
    prefix: "/api"
  route:
    cluster: backend
  # JWT filter active for /api endpoints (not disabled)
```

## Complete HTTP Flow

### Example Configuration

```yaml
http_connection_manager:
  stat_prefix: ingress_http
  codec_type: AUTO
  route_config:
    name: local_route
    virtual_hosts:
    - name: backend
      domains: ["*"]
      routes:
      - match: { prefix: "/api" }
        route: { cluster: api_cluster }
      - match: { prefix: "/public" }
        route: { cluster: public_cluster }
        typed_per_filter_config:
          envoy.filters.http.jwt_authn:
            disabled: true

  http_filters:
  - name: envoy.filters.http.cors
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.Cors
      allow_origin_string_match:
      - exact: "https://app.example.com"

  - name: envoy.filters.http.jwt_authn
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.jwt_authn.v3.JwtAuthentication
      providers: {...}

  - name: envoy.filters.http.router
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
```

### Runtime Flow

```
1. Configuration Phase (HTTP CM creation):
   ├─ FilterChainHelper::processFilters()
   │   ├─ Process CORS filter config:
   │   │   ├─ Lookup factory: "envoy.filters.http.cors"
   │   │   ├─ Call createFilterFactoryFromProto()
   │   │   └─ Store CorsFilterFactoryCb
   │   │
   │   ├─ Process JWT filter config:
   │   │   ├─ Lookup factory: "envoy.filters.http.jwt_authn"
   │   │   ├─ Call createFilterFactoryFromProto()
   │   │   └─ Store JwtAuthFilterFactoryCb
   │   │
   │   └─ Process Router filter config:
   │       ├─ Lookup factory: "envoy.filters.http.router"
   │       ├─ Call createFilterFactoryFromProto()
   │       └─ Store RouterFilterFactoryCb
   │
   └─ Validate filter chain (terminal filter, dependencies)

2. HTTP Stream Creation (New Request):
   ├─ HTTP CM receives HTTP/2 stream
   ├─ FilterChainHelper::createFilterChain(callbacks)
   │   ├─ For each FilterFactoryCb:
   │   │   ├─ CorsFilterFactoryCb(callbacks):
   │   │   │   ├─ Create CorsFilter instance
   │   │   │   └─ callbacks.addStreamDecoderFilter(cors)
   │   │   │
   │   │   ├─ JwtAuthFilterFactoryCb(callbacks):
   │   │   │   ├─ Create JwtAuthFilter instance
   │   │   │   └─ callbacks.addStreamDecoderFilter(jwt)
   │   │   │
   │   │   └─ RouterFilterFactoryCb(callbacks):
   │   │       ├─ Create RouterFilter instance
   │   │       ├─ callbacks.addStreamDecoderFilter(router)
   │   │       └─ callbacks.addStreamEncoderFilter(router)
   │   │
   │   └─ Return true (filter chain created)
   │
   └─ FilterManager now has 3 filters installed

3. Request Processing (decodeHeaders):
   ├─ FilterManager::decodeHeaders(req_headers, false)
   │   ├─ CORS filter->decodeHeaders():
   │   │   ├─ Validate origin
   │   │   └─ Return Continue
   │   │
   │   ├─ JWT Auth filter->decodeHeaders():
   │   │   ├─ Check route for per-route config
   │   │   ├─ Route = "/api" → config active
   │   │   ├─ Verify JWT token
   │   │   └─ Return Continue
   │   │
   │   └─ Router filter->decodeHeaders():
   │       ├─ Match route → api_cluster
   │       ├─ Select upstream host
   │       ├─ Initiate upstream request
   │       └─ Return StopIteration
   │
   └─ Request sent to upstream

4. Request Data (decodeData):
   ├─ FilterManager::decodeData(body, true)
   │   └─ Forward data to upstream
   │
   └─ Request body streamed

5. Response Processing (encodeHeaders):
   ├─ Upstream responds
   ├─ FilterManager::encodeHeaders(resp_headers, true)
   │   ├─ Router filter->encodeHeaders():
   │   │   ├─ Update response headers
   │   │   └─ Return Continue
   │   │
   │   ├─ JWT Auth filter->encodeHeaders():
   │   │   ├─ No-op for response
   │   │   └─ Return Continue
   │   │
   │   └─ CORS filter->encodeHeaders():
   │       ├─ Add Access-Control-* headers
   │       └─ Return Continue
   │
   └─ Response sent to client

6. Alternate Flow (/public route):
   ├─ Request to /public
   ├─ CORS filter processes
   ├─ JWT Auth filter checks per-route config:
   │   ├─ Route = "/public"
   │   ├─ Per-route config: disabled = true
   │   └─ Skip JWT validation (Return Continue immediately)
   ├─ Router filter matches public_cluster
   └─ Response processed same as above
```

## Summary

Key takeaways about HTTP filter chain creation:

1. **Per-Stream Creation**: HTTP filters created per HTTP stream (request), not per connection
2. **Nested in Network Filter**: HTTP filters live within HTTP Connection Manager network filter
3. **FIFO/LIFO Iteration**: Decoders FIFO, encoders LIFO
4. **Per-Route Config**: Filters can have different configurations per route
5. **Dynamic Enabling**: Filters can be disabled per route
6. **Filter Callbacks**: Multiple filters can be added from a single factory callback
7. **Terminal Filter**: Router filter is always terminal (initiates upstream request)

## Next Steps

Proceed to **Part 6: Filter Chain Matching and Selection** to dive deeper into how filter chains are matched to connections.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `source/common/http/conn_manager_impl.cc` - HTTP Connection Manager
- `source/common/http/filter_chain_helper.cc` - HTTP filter configuration
- `source/common/http/filter_manager.cc` - HTTP filter manager
