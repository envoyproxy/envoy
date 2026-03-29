# Part 8: Runtime Filter Instantiation

## Table of Contents
1. [Introduction](#introduction)
2. [Filter Lifecycle Overview](#filter-lifecycle-overview)
3. [Network Filter Instantiation](#network-filter-instantiation)
4. [HTTP Filter Instantiation](#http-filter-instantiation)
5. [Filter Callbacks and Iteration](#filter-callbacks-and-iteration)
6. [Memory Management](#memory-management)
7. [Error Handling](#error-handling)
8. [Performance Considerations](#performance-considerations)

## Introduction

This document explains how filter factory callbacks are invoked at runtime to create actual filter instances, and how those filter instances process data throughout their lifecycle.

## Filter Lifecycle Overview

### Complete Filter Lifecycle

```
┌──────────────────────────────────────────────────────────────┐
│                  FILTER LIFECYCLE                            │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Phase 1: CONFIGURATION (Server Startup)                     │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Parse config → Create FilterFactoryCb             │    │
│  │  Lifetime: Listener lifetime                       │    │
│  │  Cost: One-time per listener                       │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Phase 2: INSTANTIATION (Per Connection/Stream)             │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Invoke FilterFactoryCb → Create filter instance   │    │
│  │  Lifetime: Connection/stream lifetime              │    │
│  │  Cost: Per connection/stream                       │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Phase 3: INITIALIZATION                                     │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Network: onNewConnection()                         │    │
│  │  HTTP: decodeHeaders() called first time           │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Phase 4: PROCESSING                                         │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Network: onData(), onWrite()                       │    │
│  │  HTTP: decode*/encode* methods                      │    │
│  │  Repeated for each data frame                       │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Phase 5: CLEANUP                                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Connection close or stream complete                │    │
│  │  Filter destructor called                           │    │
│  │  Resources released                                 │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Lifecycle Timeline

```
Time: ────────────────────────────────────────────────────────►

Config Phase (once):
├─ FilterFactoryCb created
└─ Stored in FilterChain

Connection 1:
├─ FilterFactoryCb invoked → Filter1 instance
├─ onNewConnection()
├─ onData() ... onData() ... onData()
└─ ~Filter() (destructor)

Connection 2:
├─ FilterFactoryCb invoked → Filter2 instance
├─ onNewConnection()
├─ onData() ... onData()
└─ ~Filter()

Connection 3:
├─ FilterFactoryCb invoked → Filter3 instance
└─ ...

(FilterFactoryCb reused for all connections)
```

## Network Filter Instantiation

### Network Filter Creation Flow

```cpp
// source/common/listener_manager/listener_impl.cc

bool ListenerImpl::createNetworkFilterChain( Network::Connection& connection, const std::vector<Network::FilterFactoryCb>& filter_factories) {

  // Get filter manager from connection
  Network::FilterManager& filter_manager = connection.filterManager();

  // Iterate through filter factory callbacks
  for (const Network::FilterFactoryCb& factory_cb : filter_factories) {
    // Invoke factory callback
    // This creates the filter instance and adds it to the manager
    try { factory_cb(filter_manager);
    } catch (const EnvoyException& e) { ENVOY_CONN_LOG(error, connection, "Failed to create filter: {}", e.what());
      return false;
    }
  }

  // Initialize read filters (call onNewConnection())
  filter_manager.initializeReadFilters();

  return true;
}
```

### Network Filter Factory Callback Execution

```cpp
// Example: TCP Proxy filter factory callback

// Created at config time:
Network::FilterFactoryCb tcp_proxy_factory = [config, &cm]( Network::FilterManager& manager) {

  // Create filter instance (happens per connection)
  auto filter = std::make_shared<TcpProxyFilter>(config, cm);

  // Add to filter manager
  manager.addFilter(filter);  // Bidirectional filter

  // Filter is now installed and ready to process data
};

// Invoked at connection time:
tcp_proxy_factory(connection.filterManager());

// What happens inside:
void FilterManagerImpl::addFilter(FilterSharedPtr filter) {
  // Add as read filter
  ActiveReadFilter active_read;
  active_read.filter_ = filter;
  upstream_filters_.push_back(std::move(active_read));

  // Add as write filter
  ActiveWriteFilter active_write;
  active_write.filter_ = filter;
  downstream_filters_.push_back(std::move(active_write));

  // Initialize callbacks
  filter->initializeReadFilterCallbacks(*this);
  filter->initializeWriteFilterCallbacks(*this);
}
```

### Network Filter Initialization

```cpp
void FilterManagerImpl::initializeReadFilters() {
  // Call onNewConnection() on all read filters
  for (auto& active_filter : upstream_filters_) { if (!active_filter.initialized_) { active_filter.initialized_ = true;

      FilterStatus status = active_filter.filter_->onNewConnection();

      if (status == FilterStatus::StopIteration) {
        // Filter wants to stop (e.g., rejected connection)
        return;
      }
    }
  }

  // All filters initialized, connection ready
}
```

### Network Filter Processing

```cpp
// Example: TCP Proxy filter processing

class TcpProxyFilter : public Network::ReadFilter, public Network::WriteFilter {
public:
  TcpProxyFilter(ConfigSharedPtr config, ClusterManager& cm)
      : config_(config), cluster_manager_(cm) {}

  // Called once when connection established
  Network::FilterStatus onNewConnection() override {
    // Get upstream cluster
    cluster_ = cluster_manager_.getThreadLocalCluster( config_->cluster_name());

    if (cluster_ == nullptr) { read_callbacks_->connection().close( Network::ConnectionCloseType::NoFlush);
      return Network::FilterStatus::StopIteration;
    }

    // Create upstream connection
    upstream_connection_ = cluster_->tcpConnPool().newConnection(*this);

    return Network::FilterStatus::Continue;
  }

  // Called for each data frame from downstream
  Network::FilterStatus onData( Buffer::Instance& data, bool end_stream) override {

    if (upstream_connection_ == nullptr) {
      return Network::FilterStatus::StopIteration;
    }

    // Forward data to upstream
    upstream_connection_->write(data, end_stream);

    // Clear downstream buffer (data forwarded)
    data.drain(data.length());

    return Network::FilterStatus::Continue;
  }

  // Called for each data frame from upstream
  Network::FilterStatus onWrite( Buffer::Instance& data, bool end_stream) override {

    // Data from upstream, forward to downstream
    // (FilterManager will handle actual transmission)

    return Network::FilterStatus::Continue;
  }

  // Destructor called when connection closes
  ~TcpProxyFilter() override { if (upstream_connection_) { upstream_connection_->close();
    }
  }

private:
  ConfigSharedPtr config_;
  ClusterManager& cluster_manager_;
  Upstream::TcpPoolDataPtr upstream_connection_;
  Network::ReadFilterCallbacks* read_callbacks_;
};
```

## HTTP Filter Instantiation

### HTTP Filter Creation Flow

```cpp
// source/common/http/filter_chain_helper.cc

bool FilterChainHelper::createFilterChain( Http::FilterChainFactoryCallbacks& callbacks) const {

  // Iterate through configured HTTP filters
  for (const FilterConfig& filter_config : filter_factories_) {
    // Skip disabled filters
    if (filter_config.disabled) { continue;
    }

    // Get filter factory callback
    Http::FilterFactoryCb factory_cb =
        filter_config.filter_factory_provider->getConfig();

    // Invoke callback to create and install filter
    try { factory_cb(callbacks);
    } catch (const EnvoyException& e) { ENVOY_LOG(error, "Failed to create HTTP filter: {}", e.what());
      return false;
    }
  }

  return true;
}
```

### HTTP Filter Factory Callback Execution

```cpp
// Example: JWT Authentication filter factory callback

// Created at config time:
Http::FilterFactoryCb jwt_factory = [config]( Http::FilterChainFactoryCallbacks& callbacks) {

  // Create filter instance (happens per HTTP stream)
  auto filter = std::make_shared<JwtAuthenticationFilter>(config);

  // Add as decoder filter (processes requests)
  callbacks.addStreamDecoderFilter(filter);

  // JWT filter doesn't need to process responses
};

// Invoked per HTTP stream:
jwt_factory(filter_manager_callbacks);

// What happens inside:
void FilterManager::addStreamDecoderFilter( Http::StreamDecoderFilterSharedPtr filter) {

  // Create active filter wrapper
  ActiveStreamDecoderFilter active_filter;
  active_filter.filter_ = filter;
  active_filter.iteration_state_ = FilterIterationState::Continue;

  // Add to decoder filter list
  decoder_filters_.push_back(std::move(active_filter));

  // Initialize filter callbacks
  filter->setDecoderFilterCallbacks(*this);
}
```

### HTTP Filter Processing

```cpp
// Example: JWT Authentication filter processing

class JwtAuthenticationFilter : public Http::StreamDecoderFilter {
public:
  JwtAuthenticationFilter(ConfigSharedPtr config)
      : config_(config) {}

  // Called with request headers
  Http::FilterHeadersStatus decodeHeaders( Http::RequestHeaderMap& headers, bool end_stream) override {

    // Extract JWT from Authorization header
    const auto* auth_header = headers.get(Http::Headers::get().Authorization);
    if (auth_header == nullptr) {
      // No JWT provided
      decoder_callbacks_->sendLocalReply( Http::Code::Unauthorized, "Missing authorization header", nullptr, absl::nullopt, "");
      return Http::FilterHeadersStatus::StopIteration;
    }

    // Verify JWT asynchronously
    jwt_verifier_->verify( auth_header->value().getStringView(), [this](const JwtVerificationResult& result) { onJwtVerified(result);
        });

    // Stop iteration until JWT verified
    return Http::FilterHeadersStatus::StopIteration;
  }

  // Called with request body
  Http::FilterDataStatus decodeData( Buffer::Instance& data, bool end_stream) override {

    // JWT filter doesn't need to process body
    return Http::FilterDataStatus::Continue;
  }

  // Called with request trailers
  Http::FilterTrailersStatus decodeTrailers( Http::RequestTrailerMap& trailers) override {

    return Http::FilterTrailersStatus::Continue;
  }

private:
  void onJwtVerified(const JwtVerificationResult& result) { if (result.status == Status::Ok) {
      // JWT valid, continue filter chain
      decoder_callbacks_->continueDecoding();
    } else {
      // JWT invalid, reject request
      decoder_callbacks_->sendLocalReply( Http::Code::Forbidden, "Invalid JWT", nullptr, absl::nullopt, "");
    }
  }

  ConfigSharedPtr config_;
  JwtVerifierPtr jwt_verifier_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_;
};
```

### HTTP Filter Iteration with Async Operations

```cpp
// FilterManager handling async filter operations

void FilterManager::decodeHeaders( RequestHeaderMapPtr&& headers, bool end_stream) {

  for (auto it = decoder_filters_.begin();
       it != decoder_filters_.end();
       ++it) {

    // Skip if filter stopped iteration
    if (it->iteration_state_ == FilterIterationState::StopIteration) { continue;
    }

    // Call filter's decodeHeaders
    FilterHeadersStatus status = it->filter_->decodeHeaders( *headers, end_stream);

    switch (status) { case FilterHeadersStatus::Continue:
        // Continue to next filter immediately
        continue;

      case FilterHeadersStatus::StopIteration:
        // Filter wants to stop (async operation pending)
        it->iteration_state_ = FilterIterationState::StopIteration;
        current_decoder_filter_ = it;
        return;  // Iteration paused

      case FilterHeadersStatus::StopAllIterationAndBuffer:
        // Stop and buffer all subsequent data
        it->iteration_state_ = FilterIterationState::StopIteration;
        current_decoder_filter_ = it;
        buffering_ = true;
        return;
    }
  }

  // All filters passed, send to upstream
  sendUpstream();
}

// Called by filter when async operation completes
void FilterManager::continueDecoding() {
  // Resume iteration from current filter
  auto it = current_decoder_filter_;
  ++it;  // Move to next filter

  // Continue iteration
  for (; it != decoder_filters_.end(); ++it) { if (it->iteration_state_ == FilterIterationState::StopIteration) { continue;
    }

    FilterHeadersStatus status = it->filter_->decodeHeaders( *headers_, end_stream_);

    // Handle status as before...
  }
}
```

## Filter Callbacks and Iteration

### Network Filter Callbacks

```cpp
// envoy/network/filter.h

class ReadFilterCallbacks {
public:
  virtual ~ReadFilterCallbacks() = default;

  // Get connection
  virtual Connection& connection() PURE;

  // Continue iteration (after StopIteration)
  virtual void continueReading() PURE;

  // Get upstream host (for stats)
  virtual const Upstream::HostDescriptionConstSharedPtr& upstreamHost() PURE;

  // Inject data into read buffer
  virtual void injectReadDataToFilterChain( Buffer::Instance& data, bool end_stream) PURE;
};

class WriteFilterCallbacks {
public:
  virtual ~WriteFilterCallbacks() = default;

  // Get connection
  virtual Connection& connection() PURE;

  // Inject data into write buffer
  virtual void injectWriteDataToFilterChain( Buffer::Instance& data, bool end_stream) PURE;
};
```

### HTTP Filter Callbacks

```cpp
// envoy/http/filter.h

class StreamDecoderFilterCallbacks {
public:
  virtual ~StreamDecoderFilterCallbacks() = default;

  // Get route
  virtual Router::RouteConstSharedPtr route() PURE;

  // Get upstream cluster
  virtual Upstream::ClusterInfoConstSharedPtr clusterInfo() PURE;

  // Continue decoding after StopIteration
  virtual void continueDecoding() PURE;

  // Send local reply (bypass upstream)
  virtual void sendLocalReply( Http::Code code, absl::string_view body, std::function<void(ResponseHeaderMap&)> modify_headers, const absl::optional<Grpc::Status::GrpcStatus>& grpc_status, absl::string_view details) PURE;

  // Add decoded data
  virtual void addDecodedData( Buffer::Instance& data, bool streaming) PURE;

  // Access decoder buffer
  virtual const Buffer::Instance* decodingBuffer() PURE;

  // Modify request headers
  virtual void clearRouteCache() PURE;
};

class StreamEncoderFilterCallbacks {
public:
  virtual ~StreamEncoderFilterCallbacks() = default;

  // Continue encoding after StopIteration
  virtual void continueEncoding() PURE;

  // Add encoded data
  virtual void addEncodedData( Buffer::Instance& data, bool streaming) PURE;

  // Access encoder buffer
  virtual const Buffer::Instance* encodingBuffer() PURE;

  // Modify response headers
  virtual ResponseHeaderMap& responseHeaders() PURE;

  // Access upstream info
  virtual const StreamInfo::StreamInfo& streamInfo() const PURE;
};
```

## Memory Management

### Filter Instance Lifecycle

```cpp
// Network filters: shared_ptr managed by FilterManager

class FilterManagerImpl {
private:
  struct ActiveReadFilter { ReadFilterSharedPtr filter_;  // shared_ptr<ReadFilter> bool initialized_{false};
  };

  std::list<ActiveReadFilter> upstream_filters_;
};

// Cleanup:
// - FilterManager destroyed when connection closes
// - Each ActiveReadFilter destroyed
// - shared_ptr refcount decrements
// - If refcount reaches 0, filter destroyed

// HTTP filters: shared_ptr managed by FilterManager

class FilterManager {
private:
  struct ActiveStreamDecoderFilter { StreamDecoderFilterSharedPtr filter_;  // shared_ptr
    FilterIterationState iteration_state_;
  };

  std::list<ActiveStreamDecoderFilter> decoder_filters_;
};

// Cleanup:
// - FilterManager destroyed when stream completes
// - Each ActiveStreamDecoderFilter destroyed
// - shared_ptr refcount decrements
// - Filter destroyed when refcount reaches 0
```

### Shared Config Pattern

```cpp
// Config created once, shared across filter instances

// Config time:
auto shared_config = std::make_shared<FilterConfig>(proto_config);

Network::FilterFactoryCb factory = [shared_config]( Network::FilterManager& manager) {
  // Each filter instance shares the same config
  auto filter = std::make_shared<MyFilter>(shared_config);
  manager.addFilter(filter);
};

// Memory:
// - shared_config: 1 instance (refcount = N+1, N = active connections)
// - filter: N instances (1 per connection)
// - Total memory: sizeof(Config) + N * sizeof(Filter)

// Without sharing:
// - Total memory: N * (sizeof(Config) + sizeof(Filter))
// - Much higher memory usage for large configs
```

### Buffer Management

```cpp
// Buffers managed by connection, shared by filters

class Connection {
private:
  Buffer::OwnedImpl read_buffer_;
  Buffer::OwnedImpl write_buffer_;
};

// Filters receive references to buffers:
FilterStatus MyFilter::onData( Buffer::Instance& data,  // Reference to connection's read buffer
    bool end_stream) {

  // Option 1: Consume data (remove from buffer)
  std::string consumed_data = data.toString();
  data.drain(data.length());

  // Option 2: Leave data in buffer (next filter sees it)
  // (Don't drain)

  // Option 3: Modify data in place
  data.prepend("prefix");

  return FilterStatus::Continue;
}
```

## Error Handling

### Configuration Errors

```cpp
// Detected at config time, prevent server startup

absl::StatusOr<Network::FilterFactoryCb> MyFilterFactory::createFilterFactoryFromProto(...) {
  // Validation error
  if (!config.has_cluster_name()) {
    return absl::InvalidArgumentError( "cluster_name is required");
  }

  // Resource error
  auto cluster = context.clusterManager().getThreadLocalCluster( config.cluster_name());
  if (cluster == nullptr) {
    return absl::NotFoundError( fmt::format("Unknown cluster '{}'", config.cluster_name()));
  }

  // Return factory callback
  return [config](Network::FilterManager& manager) { manager.addFilter(std::make_shared<MyFilter>(config));
  };
}
```

### Runtime Errors

```cpp
// Detected during filter execution, handled per connection/stream

class MyFilter : public Http::StreamDecoderFilter {
public:
  FilterHeadersStatus decodeHeaders( RequestHeaderMap& headers, bool end_stream) override {

    try {
      // Process headers
      processHeaders(headers);
    } catch (const std::exception& e) {
      // Log error
      ENVOY_LOG(error, "Filter error: {}", e.what());

      // Send error response
      decoder_callbacks_->sendLocalReply( Http::Code::InternalServerError, "Internal filter error", nullptr, absl::nullopt, "");

      // Stop iteration
      return FilterHeadersStatus::StopIteration;
    }

    return FilterHeadersStatus::Continue;
  }
};
```

### Connection Errors

```cpp
class TcpProxyFilter : public Network::ReadFilter {
public:
  Network::FilterStatus onNewConnection() override {
    // Get cluster
    cluster_ = cluster_manager_.getThreadLocalCluster( config_->cluster_name());

    if (cluster_ == nullptr) {
      // Cluster not found, close connection
      ENVOY_CONN_LOG(error, read_callbacks_->connection(), "Unknown cluster '{}'", config_->cluster_name());

      read_callbacks_->connection().close( Network::ConnectionCloseType::NoFlush);

      // Stop filter chain iteration
      return Network::FilterStatus::StopIteration;
    }

    // Create upstream connection
    try { upstream_connection_ = cluster_->tcpConnPool().newConnection(*this);
    } catch (const EnvoyException& e) { ENVOY_CONN_LOG(error, read_callbacks_->connection(), "Failed to create upstream connection: {}", e.what());

      read_callbacks_->connection().close( Network::ConnectionCloseType::NoFlush);

      return Network::FilterStatus::StopIteration;
    }

    return Network::FilterStatus::Continue;
  }
};
```

## Performance Considerations

### Filter Instance Creation Cost

```cpp
// Minimize per-instance allocation

class MyFilter {
public:
  // Good: Share heavy resources via config
  MyFilter(ConfigSharedPtr config)
      : config_(config),  // Shared, no copy
        state_(State::Initial) {}  // Lightweight per-instance state

  // Bad: Allocate heavy resources per instance
  MyFilter(const Config& config)
      : heavy_resource_(std::make_unique<HeavyResource>(config)), cache_(10000) {}  // Wasteful if config doesn't change

private:
  ConfigSharedPtr config_;  // Good: Shared
  State state_;  // Good: Lightweight per-instance state
};
```

### Buffer Copy Avoidance

```cpp
// Good: Use references, avoid copies
FilterStatus MyFilter::onData( Buffer::Instance& data,  // Reference, no copy
    bool end_stream) {

  // Process data in place
  for (const auto& slice : data) { processSlice(slice.data(), slice.size());
  }

  return FilterStatus::Continue;
}

// Bad: Unnecessary copy
FilterStatus MyFilter::onData( Buffer::Instance& data, bool end_stream) {

  // Copy entire buffer
  std::string data_copy = data.toString();

  // Process copy
  process(data_copy);

  return FilterStatus::Continue;
}
```

### Async Operation Optimization

```cpp
// Good: Batch async operations
class MyFilter {
public:
  FilterHeadersStatus decodeHeaders( RequestHeaderMap& headers, bool end_stream) override {

    // Collect all async operations
    std::vector<AsyncOperation> operations;
    operations.push_back(lookupCache(key1));
    operations.push_back(lookupCache(key2));

    // Execute in parallel
    async_client_->executeParallel( operations, [this](const Results& results) { onAsyncComplete(results);
        });

    return FilterHeadersStatus::StopIteration;
  }
};

// Bad: Sequential async operations
class MyFilter {
public:
  FilterHeadersStatus decodeHeaders( RequestHeaderMap& headers, bool end_stream) override {

    // First operation
    async_client_->lookup(key1, [this](Result r1) {
      // Second operation (only after first completes)
      async_client_->lookup(key2, [this, r1](Result r2) { onAsyncComplete(r1, r2);
      });
    });

    return FilterHeadersStatus::StopIteration;
  }
};
```

## Summary

Key takeaways about runtime filter instantiation:

1. **Per-Connection/Stream**: Filter instances created per connection (network) or stream (HTTP)
2. **Factory Callbacks**: FilterFactoryCb invoked to create instances
3. **Shared Config**: Config objects shared across instances for efficiency
4. **Lifecycle Management**: shared_ptr ensures proper cleanup
5. **Async Support**: Filters can stop iteration for async operations
6. **Error Handling**: Errors handled per instance, don't affect other connections
7. **Performance**: Minimize per-instance allocation, avoid buffer copies

## Next Steps

Proceed to **Part 9: UML Class Diagrams** to see visual representations of the class relationships.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `source/common/listener_manager/listener_impl.cc` - Network filter instantiation
- `source/common/http/filter_chain_helper.cc` - HTTP filter instantiation
- `source/common/network/filter_manager_impl.cc` - Network filter management
- `source/common/http/filter_manager.cc` - HTTP filter management
