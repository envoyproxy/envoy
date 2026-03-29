# Part 2: Filter Chain Factory Construction

## Table of Contents
1. [Introduction](#introduction)
2. [Factory Pattern Overview](#factory-pattern-overview)
3. [Named Filter Config Factory](#named-filter-config-factory)
4. [Factory Registration](#factory-registration)
5. [Factory Creation Process](#factory-creation-process)
6. [Network Filter Factories](#network-filter-factories)
7. [HTTP Filter Factories](#http-filter-factories)
8. [Listener Filter Factories](#listener-filter-factories)
9. [Factory Lifecycle](#factory-lifecycle)

## Introduction

This document explains how filter factories are constructed in Envoy. The factory pattern is central to Envoy's extensibility - it allows filters to be registered, discovered, and instantiated dynamically without hard-coding dependencies.

## Factory Pattern Overview

Envoy uses a **double factory pattern**:

1. **Config Factory** (created at server startup): Parses configuration and creates factory callbacks
2. **Filter Factory Callback** (created per filter configuration): Creates actual filter instances

### Factory Pattern Flow

```
┌────────────────────────────────────────────────────────────────┐
│                    FACTORY PATTERN LAYERS                      │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  Layer 1: CONFIGURATION FACTORY (Server Lifetime)             │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  NamedFilterConfigFactory                            │    │
│  │  (Registered in global Registry)                     │    │
│  │                                                       │    │
│  │  Input:  Protobuf Config + FactoryContext           │    │
│  │  Output: FilterFactoryCb (std::function)            │    │
│  └──────────────────┬───────────────────────────────────┘    │
│                     │                                          │
│                     ▼                                          │
│  Layer 2: FILTER FACTORY CALLBACK (Per Config Instance)       │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  FilterFactoryCb = std::function<void(Manager&)>     │    │
│  │  (Stored in FilterChain or FilterChainFactory)       │    │
│  │                                                       │    │
│  │  Captures: Parsed config, shared resources           │    │
│  │  Called:   Per connection (network) or stream (HTTP) │    │
│  └──────────────────┬───────────────────────────────────┘    │
│                     │                                          │
│                     ▼                                          │
│  Layer 3: FILTER INSTANCE (Per Connection/Stream)             │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  Actual Filter Object (e.g., RouterFilter)           │    │
│  │                                                       │    │
│  │  Created:  By FilterFactoryCb invocation             │    │
│  │  Lifetime: Single connection/stream                  │    │
│  └──────────────────────────────────────────────────────┘    │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### Why Double Factory?

1. **Lazy Instantiation**: Filters created only when needed (per connection/stream)
2. **Configuration Isolation**: Config parsing happens once, filters created many times
3. **Memory Efficiency**: Shared config across multiple filter instances
4. **Dynamic Updates**: Can update factory callbacks without recreating all filters

## Named Filter Config Factory

All filter factories inherit from `NamedFactory` and implement type-specific interfaces.

### Base Interface Hierarchy

```cpp
// Base factory interface
class NamedFactory {
public:
  virtual ~NamedFactory() = default;
  virtual std::string name() const PURE; // Filter name for registry lookup
  virtual std::string category() const PURE; // e.g., "envoy.filters.network"
};

// Network filter factory interface
class NamedNetworkFilterConfigFactory : public NamedFactory {
public:
  // Create factory callback from typed config
  virtual absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProto( const Protobuf::Message& config, Server::Configuration::FactoryContext& context) PURE;

  // Create from untyped config (deprecated)
  virtual Network::FilterFactoryCb
  createFilterFactory( const Json::Object& config, Server::Configuration::FactoryContext& context) PURE;

  // Filter metadata
  virtual bool isTerminalFilterByProto( const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) PURE;
};

// HTTP filter factory interface
class NamedHttpFilterConfigFactory : public NamedFactory {
public:
  // Create factory callback from typed config
  virtual absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProto( const Protobuf::Message& config, const std::string& stats_prefix, Server::Configuration::FactoryContext& context) PURE;

  // Per-route configuration support
  virtual ProtobufTypes::MessagePtr
  createEmptyRouteConfigProto() {
    return nullptr;
  }

  virtual Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig( const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor& validator) {
    return nullptr;
  }

  // Filter capabilities
  virtual bool isTerminalFilterByProto( const Protobuf::Message& config, Server::Configuration::ServerFactoryContext& context) PURE;
};

// Listener filter factory interface
class NamedListenerFilterConfigFactory : public NamedFactory {
public:
  virtual Network::ListenerFilterFactoryCb
  createListenerFilterFactoryFromProto( const Protobuf::Message& config, const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher, Server::Configuration::ListenerFactoryContext& context) PURE;
};
```

## Factory Registration

Filters register their factories using the `REGISTER_FACTORY` macro, which adds them to the global `Registry`.

### Registration Mechanism

```cpp
// Example: TCP Proxy Network Filter Factory

// 1. Define the factory class
class TcpProxyConfigFactory : public NamedNetworkFilterConfigFactory {
public:
  // Factory name (must match proto config name)
  std::string name() const override {
    return "envoy.filters.network.tcp_proxy";
  }

  std::string category() const override {
    return "envoy.filters.network";
  }

  // Create the factory callback
  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProto( const Protobuf::Message& proto_config, Server::Configuration::FactoryContext& context) override {

    // Parse and validate proto config
    const auto& config = dynamic_cast<
        const envoy::extensions::filters::network::tcp_proxy::v3::TcpProxy&>( proto_config);

    // Create shared config object (lives for listener lifetime)
    auto filter_config = std::make_shared<TcpProxyConfig>(config, context);

    // Return factory callback (captures config)
    // This callback will be invoked per connection
    return [filter_config](Network::FilterManager& filter_manager) -> void {
      // Create filter instance with shared config
      filter_manager.addReadFilter( std::make_shared<TcpProxyFilter>(filter_config)
      );
    };
  }

  bool isTerminalFilterByProto( const Protobuf::Message&, Server::Configuration::ServerFactoryContext&) override {
    return true; // TCP Proxy is a terminal filter
  }
};

// 2. Register the factory (at static initialization time)
REGISTER_FACTORY(TcpProxyConfigFactory, NamedNetworkFilterConfigFactory);
```

### Registry Lookup

```cpp
// At runtime, factories are discovered by name:

// Get factory from registry
auto* factory = Registry::FactoryRegistry<
    NamedNetworkFilterConfigFactory>::getFactory("envoy.filters.network.tcp_proxy");

// Call factory method to create filter factory callback
auto filter_factory_cb = factory->createFilterFactoryFromProto(config, context);

// Store callback for later use (per connection)
network_filter_factories_.push_back(filter_factory_cb);
```

## Factory Creation Process

### Configuration to Factory Flow

```
┌────────────────────────────────────────────────────────────────┐
│  CONFIGURATION PROCESSING FLOW                                 │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  Step 1: Configuration Received                                │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  listener:                                            │    │
│  │    filter_chains:                                     │    │
│  │    - filters:                                         │    │
│  │      - name: envoy.filters.network.http_connection... │    │
│  │        typed_config:                                  │    │
│  │          "@type": type.../HttpConnectionManager       │    │
│  │          http_filters:                                │    │
│  │          - name: envoy.filters.http.router            │    │
│  │            typed_config: {...}                        │    │
│  └──────────────────────────────────────────────────────┘    │
│                     │                                          │
│                     ▼                                          │
│  Step 2: Parse Filter Configuration                           │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  FilterChainFactoryBuilder::buildFilterChain()       │    │
│  │                                                       │    │
│  │  For each filter in filter_chain.filters:            │    │
│  │    1. Extract filter name                            │    │
│  │    2. Extract typed_config                           │    │
│  │    3. Lookup factory by name in Registry            │    │
│  └──────────────────┬───────────────────────────────────┘    │
│                     │                                          │
│                     ▼                                          │
│  Step 3: Registry Lookup                                      │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  Registry::FactoryRegistry<                          │    │
│  │      NamedNetworkFilterConfigFactory>                │    │
│  │      ::getFactory(filter_name)                       │    │
│  │                                                       │    │
│  │  Returns: NamedNetworkFilterConfigFactory*           │    │
│  └──────────────────┬───────────────────────────────────┘    │
│                     │                                          │
│                     ▼                                          │
│  Step 4: Create Factory Callback                              │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  factory->createFilterFactoryFromProto(               │    │
│  │      typed_config,                                    │    │
│  │      factory_context)                                 │    │
│  │                                                       │    │
│  │  Returns: Network::FilterFactoryCb                    │    │
│  │           (std::function capturing config)            │    │
│  └──────────────────┬───────────────────────────────────┘    │
│                     │                                          │
│                     ▼                                          │
│  Step 5: Store Factory Callback                               │
│  ┌──────────────────────────────────────────────────────┐    │
│  │  FilterChainImpl::network_filter_factories_           │    │
│  │      .push_back(filter_factory_cb)                    │    │
│  │                                                       │    │
│  │  Stored for invocation at connection time            │    │
│  └──────────────────────────────────────────────────────┘    │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

## Network Filter Factories

### Network Filter Factory Implementation Example

```cpp
// Full implementation example: HTTP Connection Manager Factory

class HttpConnectionManagerConfigFactory
    : public NamedNetworkFilterConfigFactory {
public:
  std::string name() const override {
    return "envoy.filters.network.http_connection_manager";
  }

  absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProto( const Protobuf::Message& proto_config, Server::Configuration::FactoryContext& context) override {

    // Cast to specific config type
    const auto& config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::network::http_connection_manager::v3
            ::HttpConnectionManager&>( proto_config, context.messageValidationVisitor());

    // Create HTTP filter configuration (complex nested processing)
    auto http_config = std::make_shared<HttpConnectionManagerConfig>( config, context, http_filter_factory,  // Nested HTTP filters
        router_config_provider);

    // Return network filter factory callback
    return [http_config](Network::FilterManager& filter_manager) -> void {
      // Create HCM instance (which contains HTTP filter chain)
      filter_manager.addReadFilter( std::make_shared<Http::ConnectionManagerImpl>( http_config, drain_close, random_generator, http_context, runtime, local_info, cluster_manager, overload_manager, time_source));
    };
  }

  bool isTerminalFilterByProto( const Protobuf::Message&, Server::Configuration::ServerFactoryContext&) override {
    return true; // HCM is always terminal
  }
};

REGISTER_FACTORY( HttpConnectionManagerConfigFactory, NamedNetworkFilterConfigFactory);
```

### Network Filter Factory Callback Signature

```cpp
// Defined in envoy/network/filter.h
using FilterFactoryCb = std::function<void(FilterManager& manager)>;

// The callback receives a FilterManager reference and adds filters to it
// Example callback implementation:
FilterFactoryCb callback = [config](Network::FilterManager& manager) {
  // Create filter instance
  auto filter = std::make_shared<MyFilter>(config);

  // Add to filter manager
  manager.addReadFilter(filter);   // Read direction
  manager.addWriteFilter(filter);  // Write direction
  manager.addFilter(filter);       // Both directions (convenience)
};
```

### Network Filter Factory Variants

```cpp
// 1. Read-only filter (e.g., rate limiter)
return [config](Network::FilterManager& manager) { manager.addReadFilter(std::make_shared<RateLimitFilter>(config));
};

// 2. Write-only filter (e.g., encoder)
return [config](Network::FilterManager& manager) { manager.addWriteFilter(std::make_shared<EncoderFilter>(config));
};

// 3. Bidirectional filter (e.g., TCP proxy)
return [config](Network::FilterManager& manager) { manager.addFilter(std::make_shared<TcpProxyFilter>(config));
};

// 4. Multiple filters from one factory
return [config](Network::FilterManager& manager) { manager.addReadFilter(std::make_shared<DecoderFilter>(config));
  manager.addWriteFilter(std::make_shared<EncoderFilter>(config));
};
```

## HTTP Filter Factories

### HTTP Filter Factory Implementation Example

```cpp
// Example: Router HTTP Filter Factory

class RouterFilterConfigFactory : public NamedHttpFilterConfigFactory {
public:
  std::string name() const override {
    return "envoy.filters.http.router";
  }

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProto( const Protobuf::Message& proto_config, const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override {

    // Parse router config
    const auto& config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::http::router::v3::Router&>( proto_config, context.messageValidationVisitor());

    // Create shared router config
    auto router_config = std::make_shared<RouterConfig>( stats_prefix, context, shadow_writer_config, config);

    // Return HTTP filter factory callback
    return [router_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      // Create router filter (implements both decoder and encoder)
      auto router = std::make_shared<Router::RouterFilter>(router_config);

      // Add as decoder filter (request path)
      callbacks.addStreamDecoderFilter(router);

      // Add as encoder filter (response path)
      callbacks.addStreamEncoderFilter(router);
    };
  }

  bool isTerminalFilterByProto( const Protobuf::Message&, Server::Configuration::ServerFactoryContext&) override {
    return true; // Router is terminal (initiates upstream request)
  }
};

REGISTER_FACTORY(RouterFilterConfigFactory, NamedHttpFilterConfigFactory);
```

### HTTP Filter Factory Callback Signature

```cpp
// Defined in envoy/http/filter_factory.h
using FilterFactoryCb =
    std::function<void(FilterChainFactoryCallbacks& callbacks)>;

// FilterChainFactoryCallbacks interface:
class FilterChainFactoryCallbacks {
public:
  virtual void addStreamDecoderFilter( Http::StreamDecoderFilterSharedPtr filter) PURE;

  virtual void addStreamEncoderFilter( Http::StreamEncoderFilterSharedPtr filter) PURE;

  virtual void addStreamFilter( Http::StreamFilterSharedPtr filter) PURE;

  virtual void addAccessLogHandler( AccessLog::InstanceSharedPtr handler) PURE;
};
```

### HTTP Filter Factory Variants

```cpp
// 1. Decoder-only filter (request processing)
return [config](Http::FilterChainFactoryCallbacks& callbacks) { callbacks.addStreamDecoderFilter( std::make_shared<JwtAuthFilter>(config));
};

// 2. Encoder-only filter (response processing)
return [config](Http::FilterChainFactoryCallbacks& callbacks) { callbacks.addStreamEncoderFilter( std::make_shared<CompressionFilter>(config));
};

// 3. Dual filter (both directions)
return [config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<FaultFilter>(config);
  callbacks.addStreamDecoderFilter(filter);
  callbacks.addStreamEncoderFilter(filter);
};

// 4. Convenience method (implements StreamFilter interface)
return [config](Http::FilterChainFactoryCallbacks& callbacks) { callbacks.addStreamFilter( std::make_shared<BufferFilter>(config));
};
```

### Per-Route Configuration Support

```cpp
class CorsFilterConfigFactory : public NamedHttpFilterConfigFactory {
public:
  // Create empty proto for per-route config
  ProtobufTypes::MessagePtr createEmptyRouteConfigProto() override { return std::make_unique<
        envoy::extensions::filters::http::cors::v3::CorsPolicy>();
  }

  // Create route-specific config instance
  Router::RouteSpecificFilterConfigConstSharedPtr
  createRouteSpecificFilterConfig( const Protobuf::Message& proto_config, Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor& validator) override {

    const auto& config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::http::cors::v3::CorsPolicy&>( proto_config, validator);

    return std::make_shared<CorsFilterPerRouteConfig>(config);
  }

  // ... other methods ...
};
```

## Listener Filter Factories

### Listener Filter Factory Implementation

```cpp
// Example: TLS Inspector Listener Filter Factory

class TlsInspectorConfigFactory
    : public NamedListenerFilterConfigFactory {
public:
  std::string name() const override {
    return "envoy.filters.listener.tls_inspector";
  }

  Network::ListenerFilterFactoryCb
  createListenerFilterFactoryFromProto( const Protobuf::Message& proto_config, const Network::ListenerFilterMatcherSharedPtr& listener_filter_matcher, Server::Configuration::ListenerFactoryContext& context) override {

    // Parse config
    const auto& config = MessageUtil::downcastAndValidate<
        const envoy::extensions::filters::listener::tls_inspector::v3
            ::TlsInspector&>( proto_config, context.messageValidationVisitor());

    // Create config object
    auto inspector_config = std::make_shared<TlsInspectorConfig>( context.scope(), config);

    // Return listener filter factory callback
    return [inspector_config, listener_filter_matcher]( Network::ListenerFilterManager& filter_manager) -> void {
      // Create TLS inspector instance
      filter_manager.addAcceptFilter( listener_filter_matcher, std::make_unique<TlsInspectorFilter>(inspector_config));
    };
  }
};

REGISTER_FACTORY( TlsInspectorConfigFactory, NamedListenerFilterConfigFactory);
```

### Listener Filter Factory Callback

```cpp
// Defined in envoy/network/filter.h
using ListenerFilterFactoryCb = std::function<void( Network::ListenerFilterManager& filter_manager)>;

// Listener filter manager interface:
class ListenerFilterManager {
public:
  virtual void addAcceptFilter( const Network::ListenerFilterMatcherSharedPtr& matcher, Network::ListenerFilterPtr&& filter) PURE;
};
```

## Factory Lifecycle

### Factory Object Lifetimes

```
┌────────────────────────────────────────────────────────────────┐
│  FACTORY OBJECT LIFETIMES                                      │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  ┌────────────────────────────────────────────────────┐       │
│  │  NamedFilterConfigFactory                          │       │
│  │  (Static lifetime - global registry)               │       │
│  │  Created: Static initialization                    │       │
│  │  Destroyed: Process exit                           │       │
│  └────────────────────────────────────────────────────┘       │
│                     │                                          │
│                     │ creates                                  │
│                     ▼                                          │
│  ┌────────────────────────────────────────────────────┐       │
│  │  FilterFactoryCb (std::function)                   │       │
│  │  (Listener lifetime)                               │       │
│  │  Created: Listener configuration parse             │       │
│  │  Destroyed: Listener destruction/update            │       │
│  │  Captured: Parsed config, shared resources         │       │
│  └────────────────────────────────────────────────────┘       │
│                     │                                          │
│                     │ invoked per connection/stream            │
│                     ▼                                          │
│  ┌────────────────────────────────────────────────────┐       │
│  │  Filter Instance                                    │       │
│  │  (Connection/Stream lifetime)                       │       │
│  │  Created: New connection or HTTP stream             │       │
│  │  Destroyed: Connection close or stream complete     │       │
│  └────────────────────────────────────────────────────┘       │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

### Shared Config Pattern

Many filters use a **shared config** pattern to avoid duplicating config data:

```cpp
// Shared configuration (created once per listener)
class TcpProxyConfig {
public:
  TcpProxyConfig(const TcpProxyProto& config, FactoryContext& context) {
    // Parse and store config
    cluster_name_ = config.cluster();
    timeout_ = config.timeout();
    // ... initialize from proto
  }

  const std::string& clusterName() const {
    return cluster_name_; }

private:
  std::string cluster_name_;
  std::chrono::milliseconds timeout_;
  // ... other config fields
};

// Factory creates shared_ptr to config, captures in callback
absl::StatusOr<Network::FilterFactoryCb> createFilterFactoryFromProto(...) { auto shared_config = std::make_shared<TcpProxyConfig>(proto_config, context);

  // Callback captures shared_ptr (reference counted)
  return [shared_config](Network::FilterManager& manager) {
    // Multiple filter instances share same config
    manager.addFilter(std::make_shared<TcpProxyFilter>(shared_config));
  };
}
```

## Summary

Key takeaways from filter chain factory construction:

1. **Double Factory Pattern**: Config factories create filter factory callbacks, which create filter instances
2. **Registry Pattern**: Factories registered globally by name for discovery
3. **Lazy Instantiation**: Filters created per connection/stream, not at config time
4. **Shared Config**: Config parsing done once, shared across filter instances
5. **Type Safety**: Specific factory interfaces for network, HTTP, and listener filters
6. **Extensibility**: New filters added by implementing factory interface and registering

## Next Steps

Proceed to **Part 3: Factory Context Hierarchy** to learn how factory contexts provide scoped resources to filters during creation.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `envoy/server/filter_config.h` - Factory interfaces
- `envoy/registry/registry.h` - Registry implementation
- `source/common/listener_manager/filter_chain_manager_impl.cc` - Factory usage
