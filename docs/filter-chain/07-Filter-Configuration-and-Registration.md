# Part 7: Filter Configuration and Registration

## Table of Contents
1. [Introduction](#introduction)
2. [Configuration Flow Overview](#configuration-flow-overview)
3. [Protobuf Configuration](#protobuf-configuration)
4. [Registry System](#registry-system)
5. [Filter Factory Registration](#filter-factory-registration)
6. [Configuration Validation](#configuration-validation)
7. [Dynamic Configuration](#dynamic-configuration)
8. [Extension Configuration Providers](#extension-configuration-providers)

## Introduction

This document explains how filters are configured through protobuf definitions and how they register themselves with Envoy's registry system for discovery and instantiation.

## Configuration Flow Overview

### End-to-End Configuration Flow

```
┌──────────────────────────────────────────────────────────────┐
│           FILTER CONFIGURATION FLOW                          │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Protobuf Definition (at development time)                │
│  ┌────────────────────────────────────────────────────┐    │
│  │  // api/envoy/extensions/filters/http/router/v3/   │    │
│  │  // router.proto                                    │    │
│  │                                                     │    │
│  │  message Router {                                   │    │
│  │    bool dynamic_stats = 1;                          │    │
│  │    bool start_child_span = 2;                       │    │
│  │    repeated string upstream_log = 3;                │    │
│  │    // ... more config fields                        │    │
│  │  }                                                  │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  2. Code Generation (build time)                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  protoc generates C++ classes:                      │    │
│  │  - Router proto class                               │    │
│  │  - Serialization/deserialization                    │    │
│  │  - Validation helpers                               │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  3. Factory Implementation (development time)               │
│  ┌────────────────────────────────────────────────────┐    │
│  │  class RouterFilterConfigFactory :                  │    │
│  │      public NamedHttpFilterConfigFactory {          │    │
│  │                                                     │    │
│  │    absl::StatusOr<Http::FilterFactoryCb>            │    │
│  │    createFilterFactoryFromProto(                    │    │
│  │        const Protobuf::Message& proto,              │    │
│  │        ...) override {                              │    │
│  │      // Parse and validate proto config             │    │
│  │      // Return filter factory callback              │    │
│  │    }                                                │    │
│  │  };                                                 │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  4. Factory Registration (static init)                      │
│  ┌────────────────────────────────────────────────────┐    │
│  │  REGISTER_FACTORY(RouterFilterConfigFactory,        │    │
│  │                   NamedHttpFilterConfigFactory);    │    │
│  │                                                     │    │
│  │  → Added to global Registry at startup             │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  5. User Configuration (runtime)                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  http_filters:                                      │    │
│  │  - name: envoy.filters.http.router                  │    │
│  │    typed_config:                                    │    │
│  │      "@type": type.googleapis.com/.../Router        │    │
│  │      dynamic_stats: true                            │    │
│  │      start_child_span: true                         │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  6. Configuration Parsing (Envoy startup)                   │
│  ┌────────────────────────────────────────────────────┐    │
│  │  ListenerManager processes config                   │    │
│  │  ├─ Parse filter name: "envoy.filters.http.router" │    │
│  │  ├─ Parse typed_config into Router proto           │    │
│  │  ├─ Lookup factory in Registry by name             │    │
│  │  │   └─► Registry returns RouterFilterConfigFactory│    │
│  │  ├─ Call factory->createFilterFactoryFromProto()   │    │
│  │  └─ Store resulting FilterFactoryCb                │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  7. Runtime Instantiation (per request/connection)          │
│  ┌────────────────────────────────────────────────────┐    │
│  │  FilterFactoryCb invoked                            │    │
│  │  └─► Creates actual filter instance                │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## Protobuf Configuration

### Filter Configuration Protobuf Structure

```protobuf
// Example: Router filter configuration proto
// api/envoy/extensions/filters/http/router/v3/router.proto

syntax = "proto3";

package envoy.extensions.filters.http.router.v3;

import "envoy/config/accesslog/v3/accesslog.proto";
import "google/protobuf/wrappers.proto";

message Router {
  // Whether to dynamically allocate stats per upstream cluster
  google.protobuf.BoolValue dynamic_stats = 1;

  // Whether to start a child span for egress routed calls
  bool start_child_span = 2;

  // Configuration for HTTP upstream logs
  repeated config.accesslog.v3.AccessLog upstream_log = 3;

  // Suppress envoy headers
  bool suppress_envoy_headers = 4;

  // Strict header checking
  repeated HeaderValidationConfig strict_check_headers = 5;

  // Respect expected request timeout
  bool respect_expected_rq_timeout = 6;

  // Suppress gRPC request failure codes
  bool suppress_grpc_request_failure_code_stats = 7;

  // Configuration for upstream HTTP filters
  repeated HttpFilter upstream_http_filters = 8;
}
```

### Common Configuration Patterns

```protobuf
// Pattern 1: Simple configuration with primitives
message SimpleFilter {
  string cluster_name = 1;
  google.protobuf.Duration timeout = 2;
  uint32 max_retries = 3;
  bool enabled = 4;
}

// Pattern 2: Nested configuration
message ComplexFilter {
  message RateLimitConfig {
    string domain = 1;
    repeated Action actions = 2;
  }

  RateLimitConfig rate_limit = 1;
  google.protobuf.Any custom_config = 2;
}

// Pattern 3: Oneof for alternatives
message FlexibleFilter {
  oneof config_type {
    InlineConfig inline = 1;
    DataSource file = 2;
    string datasource_ref = 3;
  }
}

// Pattern 4: Repeated for lists
message MultiFilter {
  repeated string allowed_domains = 1;
  repeated HeaderMatcher headers = 2;
}

// Pattern 5: Any for extensibility
message ExtensibleFilter {
  string name = 1;
  google.protobuf.Any typed_config = 2;
}
```

## Registry System

### Registry Implementation

```cpp
// envoy/registry/registry.h

template <class Base> class FactoryRegistry {
public:
  // Register a factory
  static void registerFactory(Base& factory, const std::string& name) { auto result = factories().emplace(name, &factory);
    RELEASE_ASSERT( result.second, fmt::format("Double registration for name: '{}'", name));
  }

  // Get factory by name
  static Base* getFactory(const std::string& name) { auto it = factories().find(name);
    if (it != factories().end()) {
      return it->second;
    }
    return nullptr;
  }

  // Get all registered factories
  static std::vector<Base*> allFactories() { std::vector<Base*> result;
    for (const auto& pair : factories()) { result.push_back(pair.second);
    }
    return result;
  }

private:
  // Static map of name -> factory
  static absl::flat_hash_map<std::string, Base*>& factories() { static auto* factories =
        new absl::flat_hash_map<std::string, Base*>();
    return *factories;
  }
};
```

### Registry Categories

```cpp
// Different registry types for different extension points

// 1. HTTP Filter Registry
using HttpFilterRegistry =
    Registry::FactoryRegistry<
        Server::Configuration::NamedHttpFilterConfigFactory>;

// 2. Network Filter Registry
using NetworkFilterRegistry =
    Registry::FactoryRegistry<
        Server::Configuration::NamedNetworkFilterConfigFactory>;

// 3. Listener Filter Registry
using ListenerFilterRegistry =
    Registry::FactoryRegistry<
        Server::Configuration::NamedListenerFilterConfigFactory>;

// 4. Transport Socket Registry
using TransportSocketRegistry =
    Registry::FactoryRegistry<
        Server::Configuration::DownstreamTransportSocketConfigFactory>;

// 5. Cluster Registry
using ClusterRegistry =
    Registry::FactoryRegistry<
        Upstream::ClusterFactory>;

// 6. Access Log Registry
using AccessLogRegistry =
    Registry::FactoryRegistry<
        Server::Configuration::AccessLogInstanceFactory>;

// ... and many more extension points
```

## Filter Factory Registration

### Registration Macro

```cpp
// envoy/registry/registry.h

#define REGISTER_FACTORY(factory, base_class_name)                    \ static Envoy::Registry::RegisterFactory<factory, base_class_name>   \ factory##_registered

// Expands to:
// static Envoy::Registry::RegisterFactory<MyFactory, BaseFactory>
//     MyFactory_registered;

// This creates a static instance that registers the factory at startup
```

### RegisterFactory Template

```cpp
template <class Factory, class Base> class RegisterFactory {
public:
  RegisterFactory() {
    // Create factory instance
    static Factory instance;

    // Register with appropriate registry
    Registry::FactoryRegistry<Base>::registerFactory( instance, instance.name());
  }
};
```

### Complete Registration Example

```cpp
// 1. Define factory class
class RouterFilterConfigFactory
    : public Server::Configuration::NamedHttpFilterConfigFactory {
public:
  // Factory name (must match config)
  std::string name() const override {
    return "envoy.filters.http.router";
  }

  // Category for grouping
  std::string category() const override {
    return "envoy.filters.http";
  }

  // Create empty config proto for route-specific config
  ProtobufTypes::MessagePtr createEmptyConfigProto() override { return std::make_unique<
        envoy::extensions::filters::http::router::v3::Router>();
  }

  // Create filter factory from proto config
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProto( const Protobuf::Message& proto_config, const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override {

    // Cast to specific config type
    const auto& router_config =
        MessageUtil::downcastAndValidate<
            const envoy::extensions::filters::http::router::v3::Router&>( proto_config, context.messageValidationVisitor());

    // Create shared config
    auto config = std::make_shared<Router::RouterFilterConfig>( stats_prefix, context, router_config);

    // Return factory callback
    return [config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<Router::RouterFilter>(config);
      callbacks.addStreamDecoderFilter(filter);
      callbacks.addStreamEncoderFilter(filter);
    };
  }

  // Indicate if terminal filter
  bool isTerminalFilterByProto( const Protobuf::Message&, Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

// 2. Register factory
REGISTER_FACTORY( RouterFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

// At static initialization time, this:
// 1. Creates static instance of RouterFilterConfigFactory
// 2. Calls Registry::FactoryRegistry<NamedHttpFilterConfigFactory>
//    ::registerFactory(instance, "envoy.filters.http.router")
// 3. Factory now discoverable by name
```

## Configuration Validation

### Validation Layers

```
┌──────────────────────────────────────────────────────────────┐
│              CONFIGURATION VALIDATION LAYERS                 │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Layer 1: Protobuf Schema Validation                         │
│  ┌────────────────────────────────────────────────────┐    │
│  │  - Field types (string, uint32, bool, etc.)        │    │
│  │  - Required fields                                  │    │
│  │  - Oneof constraints                                │    │
│  │  - Enum values                                      │    │
│  │  Performed by: protobuf library                     │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Layer 2: Protobuf Validation (PGV)                         │
│  ┌────────────────────────────────────────────────────┐    │
│  │  - Value ranges (gt, gte, lt, lte)                  │    │
│  │  - String patterns (regex)                          │    │
│  │  - Collection sizes (min_items, max_items)          │    │
│  │  - Custom rules (via validate annotations)          │    │
│  │  Performed by: ValidateMessage()                    │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Layer 3: Factory Validation                                │
│  ┌────────────────────────────────────────────────────┐    │
│  │  - Semantic validation                              │    │
│  │  - Cross-field constraints                          │    │
│  │  - Resource availability                            │    │
│  │  - Cluster existence                                │    │
│  │  Performed by: createFilterFactoryFromProto()       │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Layer 4: Runtime Validation                                │
│  ┌────────────────────────────────────────────────────┐    │
│  │  - Filter chain completeness (terminal filter)      │    │
│  │  - Dependency validation                            │    │
│  │  - Compatibility checks                             │    │
│  │  Performed by: FilterChainHelper, DependencyManager │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Protobuf Validation (PGV) Example

```protobuf
// api/envoy/extensions/filters/http/router/v3/router.proto

import "validate/validate.proto";

message Router {
  // Timeout must be at least 1ms
  google.protobuf.Duration timeout = 1 [
    (validate.rules).duration = {
      required: true,
      gte: {nanos: 1000000}  // >= 1ms
    }
  ];

  // Max retries must be between 0 and 10
  uint32 max_retries = 2 [
    (validate.rules).uint32 = {
      lte: 10
    }
  ];

  // Cluster name must not be empty
  string cluster_name = 3 [
    (validate.rules).string = {
      min_len: 1
    }
  ];

  // Domain must match pattern
  string domain = 4 [
    (validate.rules).string = {
      pattern: "^[a-z0-9-]+$"
    }
  ];

  // At least one action required
  repeated Action actions = 5 [
    (validate.rules).repeated = {
      min_items: 1
    }
  ];
}
```

### Factory Validation Example

```cpp
absl::StatusOr<Http::FilterFactoryCb> RouterFilterConfigFactory::createFilterFactoryFromProto( const Protobuf::Message& proto_config, const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  // 1. Cast and validate proto
  const auto& config =
      MessageUtil::downcastAndValidate<
          const envoy::extensions::filters::http::router::v3::Router&>( proto_config, context.messageValidationVisitor());

  // 2. Semantic validation
  if (config.has_cluster_name()) {
    // Validate cluster exists
    auto cluster = context.clusterManager().getThreadLocalCluster( config.cluster_name());
    if (cluster == nullptr) {
      return absl::InvalidArgumentError( fmt::format("Unknown cluster '{}'", config.cluster_name()));
    }
  }

  // 3. Cross-field validation
  if (config.has_timeout() && config.has_idle_timeout()) { if (config.timeout() < config.idle_timeout()) {
    return absl::InvalidArgumentError( "timeout must be >= idle_timeout");
    }
  }

  // 4. Resource validation
  if (config.upstream_log_size() > 0) { for (const auto& log : config.upstream_log()) {
      // Validate access log configuration
      auto access_log_factory = /* get factory */;
      if (access_log_factory == nullptr) {
        return absl::InvalidArgumentError( fmt::format("Unknown access log type: '{}'", log.name()));
      }
    }
  }

  // 5. Create config and return factory callback
  auto router_config = std::make_shared<RouterConfig>( stats_prefix, context, config);

  return [router_config](Http::FilterChainFactoryCallbacks& callbacks) { auto filter = std::make_shared<RouterFilter>(router_config);
    callbacks.addStreamDecoderFilter(filter);
    callbacks.addStreamEncoderFilter(filter);
  };
}
```

### Dependency Validation

```cpp
// source/common/http/filter_chain_helper.h

class FilterChainDependencyManager {
public:
  // Add filter with dependencies
  void addFilter( const std::string& filter_name, const std::set<std::string>& dependencies) { filter_dependencies_[filter_name] = dependencies;
  }

  // Validate filter chain
  absl::Status validate( const std::vector<std::string>& filter_chain) const {

    std::set<std::string> available_filters;

    for (const auto& filter_name : filter_chain) {
      // Check if dependencies are satisfied
      auto deps_iter = filter_dependencies_.find(filter_name);
      if (deps_iter != filter_dependencies_.end()) {
        for (const auto& dep : deps_iter->second) {
          if (available_filters.find(dep) == available_filters.end()) { return absl::FailedPreconditionError( fmt::format( "Filter '{}' depends on '{}' which appears "
                    "later in the chain or is missing", filter_name, dep));
          }
        }
      }

      available_filters.insert(filter_name);
    }

    return absl::OkStatus();
  }

private:
  absl::flat_hash_map<std::string, std::set<std::string>> filter_dependencies_;
};

// Example usage:
// JWT filter depends on CORS filter
dependency_manager.addFilter( "envoy.filters.http.jwt_authn", {"envoy.filters.http.cors"});

// Validate chain: [cors, jwt, router] → OK
// Validate chain: [jwt, cors, router] → ERROR (jwt before cors)
```

## Dynamic Configuration

### Configuration Update Flow

```
┌──────────────────────────────────────────────────────────────┐
│            DYNAMIC CONFIGURATION UPDATE FLOW                 │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. xDS Server pushes new config                             │
│     └─► LDS (Listener Discovery Service)                    │
│          └─► Updated listener configuration                  │
│                                                              │
│  2. Envoy receives config update                             │
│     └─► ListenerManager::addOrUpdateListener()              │
│          ├─ Parse new listener config                       │
│          ├─ Build new filter chains                         │
│          └─ Validate new configuration                      │
│                                                              │
│  3. Warm new listener                                        │
│     └─► Initialize new listener                             │
│          ├─ Run init managers                               │
│          ├─ Warm clusters                                   │
│          └─ Start accepting connections on new listener     │
│                                                              │
│  4. Drain old listener                                       │
│     └─► Mark old listener as draining                       │
│          ├─ Stop accepting new connections                  │
│          ├─ Wait for active connections to complete         │
│          └─ Destroy old listener                            │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Extension Configuration Provider

```cpp
// envoy/config/extension_config_provider.h

template <class FactoryCb> class ExtensionConfigProvider {
public:
  virtual ~ExtensionConfigProvider() = default;

  // Get current config
  virtual FactoryCb getConfig() const PURE;

  // Optional callback for config updates
  virtual void onConfigUpdate(std::function<void()> callback) PURE;
};

// Example: Dynamic HTTP filter config provider
class DynamicHttpFilterConfigProvider
    : public ExtensionConfigProvider<Http::FilterFactoryCb> {
public:
  DynamicHttpFilterConfigProvider( const std::string& filter_name, Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager)
      : filter_name_(filter_name), context_(context) {

    // Subscribe to ECDS (Extension Config Discovery Service)
    subscription_ = context.clusterManager()
        .subscriptionFactory()
        .subscriptionFromConfigSource( ecds_config, Grpc::Common::typeUrl(filter_config_type), stats_scope, *this);

    // Add init target
    init_target_ = std::make_unique<Init::TargetImpl>( fmt::format("DynamicHttpFilter {}", filter_name), [this] { subscription_->start(); });

    init_manager.add(std::move(init_target_));
  }

  // Get current filter factory callback
  Http::FilterFactoryCb getConfig() const override { absl::ReaderMutexLock lock(&mu_);
    return current_config_;
  }

  // Called when new config received from xDS
  void onConfigUpdate( const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources, const std::string& version_info) override {

    // Parse new filter config
    const auto& filter_config = /* parse from resources */;

    // Create new filter factory callback
    auto* factory = Registry::FactoryRegistry<
        NamedHttpFilterConfigFactory>::getFactory(filter_name_);

    auto new_config_or_error =
        factory->createFilterFactoryFromProto( filter_config, stats_prefix_, context_);

    if (!new_config_or_error.ok()) {
      // Reject invalid config
      throw EnvoyException( fmt::format("Invalid config: {}", new_config_or_error.status().message()));
    }

    // Update current config atomically
    { absl::WriterMutexLock lock(&mu_);
      current_config_ = std::move(new_config_or_error.value());
    }

    // Notify listeners
    for (auto& callback : update_callbacks_) { callback();
    }
  }

private:
  std::string filter_name_;
  Server::Configuration::ServerFactoryContext& context_;
  Envoy::Config::SubscriptionPtr subscription_;
  Init::TargetHandlePtr init_target_;

  mutable absl::Mutex mu_;
  Http::FilterFactoryCb current_config_ ABSL_GUARDED_BY(mu_);

  std::vector<std::function<void()>> update_callbacks_;
};
```

## Summary

Key takeaways about filter configuration and registration:

1. **Protobuf Schema**: Filters defined using protobuf for type safety and validation
2. **Registry Pattern**: Global registry maps filter names to factory instances
3. **Static Registration**: REGISTER_FACTORY macro registers at static init time
4. **Validation Layers**: Schema, PGV, factory, and runtime validation
5. **Dynamic Updates**: Extension Config Providers support runtime config updates
6. **Type Safety**: Strong typing throughout configuration pipeline
7. **Extensibility**: New filters added without modifying core code

## Next Steps

Proceed to **Part 8: Runtime Filter Instantiation** to see how filter factory callbacks are invoked to create actual filter instances.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `envoy/registry/registry.h` - Registry implementation
- `api/envoy/extensions/filters/` - Filter protobuf definitions
- `source/extensions/filters/*/config.cc` - Factory implementations
