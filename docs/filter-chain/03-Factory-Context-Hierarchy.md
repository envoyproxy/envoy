# Part 3: Factory Context Hierarchy

## Table of Contents
1. [Introduction](#introduction)
2. [Context Hierarchy Overview](#context-hierarchy-overview)
3. [Context Interfaces](#context-interfaces)
4. [Resource Scoping](#resource-scoping)
5. [Context Implementations](#context-implementations)
6. [Init Manager Integration](#init-manager-integration)
7. [Context Usage Patterns](#context-usage-patterns)

## Introduction

Factory contexts provide scoped access to server resources for filter configuration and instantiation. The context hierarchy ensures that filters can access the right resources at the right scope while maintaining proper lifecycle management and resource isolation.

## Context Hierarchy Overview

Envoy uses a hierarchical context structure that progressively adds capabilities and narrows scope:

### Context Inheritance Diagram

```
┌──────────────────────────────────────────────────────────────┐
│              FACTORY CONTEXT HIERARCHY                       │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  CommonFactoryContext                                        │
│  ├─ Server-wide resources                                   │
│  ├─ ServerFactoryContext                                    │
│  ├─ ClusterManager, API, ThreadLocalStorage                 │
│  ├─ Stats Store, Event Dispatcher                           │
│  └─ Singleton Manager, Protobuf Validator                   │
│         │                                                    │
│         └──► ServerFactoryContext (adds)                    │
│              ├─ Server lifetime resources                   │
│              ├─ HTTP Context, Router Context                │
│              ├─ GRPC Context                                │
│              ├─ DNS Resolver, MessageValidationContext      │
│              └─ XDS Config Tracker                          │
│                  │                                           │
│                  └──► GenericFactoryContext (adds)          │
│                       ├─ Init::Manager                      │
│                       ├─ Stats::Scope (may override)        │
│                       │                                     │
│                       └──► FactoryContext (adds)            │
│                            ├─ Listener-scoped resources     │
│                            ├─ DrainDecision                 │
│                            ├─ Listener info & stats         │
│                            │                                │
│                            └──► FilterChainFactoryContext   │
│                                 ├─ Filter-chain-scoped      │
│                                 ├─ Per-chain Init::Manager  │
│                                 ├─ Per-chain Stats::Scope   │
│                                 └─ Draining state           │
│                                     │                        │
│                                     └──► ListenerFactoryCtx │
│                                          (Listener filters) │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Context Scope Summary

| Context Type | Scope | Lifetime | Primary Use |
|--------------|-------|----------|-------------|
| `CommonFactoryContext` | Server-wide | Process | Access to global singletons |
| `ServerFactoryContext` | Server | Server | HTTP/Router/GRPC contexts |
| `GenericFactoryContext` | Configurable | Varies | Init manager + stats scope |
| `FactoryContext` | Listener | Listener | Network filter creation |
| `FilterChainFactoryContext` | Filter chain | Filter chain | Per-chain initialization |
| `ListenerFactoryContext` | Listener | Listener | Listener filter creation |

## Context Interfaces

### CommonFactoryContext Interface

Base context providing server-wide resources.

```cpp
// envoy/server/factory_context.h

class CommonFactoryContext {
public:
  virtual ~CommonFactoryContext() = default;

  // Core server components
  virtual Server::Admin& admin() PURE;
  virtual Api::Api& api() PURE;
  virtual Upstream::ClusterManager& clusterManager() PURE;
  virtual Event::Dispatcher& mainThreadDispatcher() PURE;
  virtual const LocalInfo::LocalInfo& localInfo() PURE;
  virtual Envoy::Runtime::Loader& runtime() PURE;
  virtual Stats::Store& store() PURE;
  virtual ThreadLocal::SlotAllocator& threadLocal() PURE;

  // Resource management
  virtual Singleton::Manager& singletonManager() PURE;
  virtual ProtobufMessage::ValidationContext& messageValidationContext() PURE;
  virtual ProtobufMessage::ValidationVisitor& messageValidationVisitor() PURE;

  // Server metadata
  virtual TimeSource& timeSource() PURE;
  virtual OptRef<Server::ServerLifecycleNotifier> lifecycleNotifier() PURE;
};
```

### ServerFactoryContext Interface

Adds server lifetime resources.

```cpp
class ServerFactoryContext : public virtual CommonFactoryContext {
public:
  // HTTP-specific contexts
  virtual Http::Context& httpContext() PURE;
  virtual Router::Context& routerContext() PURE;
  virtual Grpc::Context& grpcContext() PURE;

  // Configuration tracking
  virtual Envoy::Server::ConfigTracker& configTracker() PURE;
  virtual OptRef<Server::Configuration::ServerFactoryContext> serverFactoryContext() PURE;

  // DNS resolution
  virtual Network::DnsResolverSharedPtr getOrCreateDnsResolver() PURE;

  // Access logging
  virtual AccessLog::AccessLogManager& accessLogManager() PURE;

  // Overload management
  virtual Server::OverloadManager& overloadManager() PURE;
};
```

### GenericFactoryContext Interface

Adds init manager and stats scope.

```cpp
class GenericFactoryContext : public virtual ServerFactoryContext {
public:
  // Initialization management
  virtual Init::Manager& initManager() PURE;

  // Stats scope (may be listener or filter-chain specific)
  virtual Stats::Scope& scope() PURE;
};
```

### FactoryContext Interface

Listener-scoped context for network filters.

```cpp
class FactoryContext : public virtual GenericFactoryContext {
public:
  // Listener-specific resources
  virtual Network::DrainDecision& drainDecision() PURE;
  virtual const Network::ListenerInfo& listenerInfo() PURE;
  virtual Stats::Scope& listenerScope() PURE;

  // Server factory context access
  virtual Server::Configuration::ServerFactoryContext& serverFactoryContext() PURE;

  // Filter chain factory context (per-filter-chain)
  virtual Configuration::FilterChainFactoryContext& getFilterChainFactoryContext() PURE;
};
```

### FilterChainFactoryContext Interface

Per-filter-chain scoped context.

```cpp
class FilterChainFactoryContext {
public:
  virtual ~FilterChainFactoryContext() = default;

  // Filter-chain-specific initialization
  virtual Init::Manager& initManager() PURE;

  // Filter-chain-specific stats scope
  virtual Stats::Scope& scope() PURE;

  // Draining management
  virtual void startDraining() PURE;
  virtual bool isDraining() const PURE;
};
```

### ListenerFactoryContext Interface

Context for listener filters.

```cpp
class ListenerFactoryContext : public virtual FactoryContext {
public:
  // Listener filter matcher
  virtual Network::ListenerFilterMatcherSharedPtr
      listenerFilterMatcher(Network::ListenerFilterType type) PURE;
};
```

## Resource Scoping

### Scoping Principles

```
┌──────────────────────────────────────────────────────────────┐
│                    RESOURCE SCOPING                          │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Server Scope (Process Lifetime)                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  • Cluster Manager (upstream clusters)             │    │
│  │  • Thread Local Storage                            │    │
│  │  • Singleton Manager                               │    │
│  │  • Runtime Configuration                           │    │
│  │  • Stats Store (global)                            │    │
│  │  • Admin Interface                                 │    │
│  │  • API (filesystem, network syscalls)              │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         └──► Listener Scope (Per Listener)                  │
│              ┌────────────────────────────────────────┐     │
│              │  • Listener Stats Scope                │     │
│              │    stats.listener.0.0.0.0_8080.*       │     │
│              │  • Drain Decision (graceful shutdown)  │     │
│              │  • Listener Info (address, metadata)   │     │
│              │  • Init Manager (listener init)        │     │
│              └────────────────────────────────────────┘     │
│                   │                                          │
│                   └──► Filter Chain Scope                   │
│                        ┌──────────────────────────────┐     │
│                        │  • Per-chain Init Manager    │     │
│                        │  • Per-chain Stats Scope     │     │
│                        │    listener.0.0.0.0_8080.    │     │
│                        │      filter_chain.sni_*.stats│     │
│                        │  • Draining State            │     │
│                        └──────────────────────────────┘     │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Stats Scoping Example

```cpp
// Server-level stats
Stats::Store& server_store = context.store();
auto counter = server_store.counterFromString("server.connections");

// Listener-level stats
Stats::Scope& listener_scope = factory_context.listenerScope();
auto listener_counter = listener_scope.counterFromString("downstream_cx_total");
// Results in: stats.listener.0.0.0.0_8080.downstream_cx_total

// Filter-chain-level stats
FilterChainFactoryContext& fc_context = context.getFilterChainFactoryContext();
Stats::Scope& fc_scope = fc_context.scope();
auto fc_counter = fc_scope.counterFromString("ssl.handshake");
// Results in: stats.listener.0.0.0.0_8080.filter_chain.sni_api.ssl.handshake
```

## Context Implementations

### PerFilterChainFactoryContextImpl

Primary implementation of FilterChainFactoryContext.

```cpp
// source/common/listener_manager/filter_chain_manager_impl.h

class PerFilterChainFactoryContextImpl
    : public Configuration::FilterChainFactoryContext {
public:
  PerFilterChainFactoryContextImpl( Configuration::FactoryContext& parent_context, Init::Manager& init_manager, Stats::Scope& stats_scope)
      : parent_context_(parent_context), init_manager_(init_manager), stats_scope_(stats_scope), draining_(false) {}

  // FilterChainFactoryContext implementation
  Init::Manager& initManager() override {
    return init_manager_;
  }
  Stats::Scope& scope() override {
    return stats_scope_;
  }

  void startDraining() override { draining_ = true; }
  bool isDraining() const override {
    return draining_; }

  // Access to parent context resources
  Configuration::FactoryContext& parentContext() {
    return parent_context_;
  }

private:
  Configuration::FactoryContext& parent_context_;
  Init::Manager& init_manager_;
  Stats::Scope& stats_scope_;
  std::atomic<bool> draining_;
};
```

### ListenerFactoryContextImpl

Implementation providing listener-scoped resources.

```cpp
class ListenerFactoryContextImpl : public Configuration::ListenerFactoryContext {
public:
  ListenerFactoryContextImpl( Server::Configuration::ServerFactoryContext& server_context, Network::ListenerConfig& listener_config, Network::DrainDecision& drain_decision, Stats::Scope& listener_scope, Init::Manager& init_manager)
      : server_context_(server_context), listener_config_(listener_config), drain_decision_(drain_decision), listener_scope_(listener_scope), init_manager_(init_manager) {}

  // Inherited from CommonFactoryContext (delegate to server)
  Api::Api& api() override {
    return server_context_.api();
  }
  Upstream::ClusterManager& clusterManager() override {
    return server_context_.clusterManager();
  }
  // ... other delegations ...

  // Inherited from FactoryContext (listener-specific)
  Network::DrainDecision& drainDecision() override {
    return drain_decision_;
  }
  const Network::ListenerInfo& listenerInfo() override {
    return listener_config_.listenerInfo();
  }
  Stats::Scope& listenerScope() override {
    return listener_scope_;
  }
  Init::Manager& initManager() override {
    return init_manager_;
  }

  // Per-filter-chain context
  Configuration::FilterChainFactoryContext& getFilterChainFactoryContext() override {
    return *per_filter_chain_context_;
  }

private:
  Server::Configuration::ServerFactoryContext& server_context_;
  Network::ListenerConfig& listener_config_;
  Network::DrainDecision& drain_decision_;
  Stats::Scope& listener_scope_;
  Init::Manager& init_manager_;
  std::unique_ptr<PerFilterChainFactoryContextImpl> per_filter_chain_context_;
};
```

## Init Manager Integration

### Initialization Flow

Init managers coordinate asynchronous initialization (e.g., warming upstream clusters, loading secrets).

```
┌──────────────────────────────────────────────────────────────┐
│              INITIALIZATION MANAGER FLOW                     │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Phase 1: Configuration Creation                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Filter factory creates init targets               │    │
│  │                                                     │    │
│  │  auto target = std::make_unique<InitTarget>(       │    │
│  │      "my_filter", [this] {                         │    │
│  │          // Async initialization (e.g., warm cache) │    │
│  │          warmCache([this] {                         │    │
│  │              target->ready(); // Signal completion  │    │
│  │          });                                        │    │
│  │      });                                            │    │
│  │                                                     │    │
│  │  context.initManager().add(std::move(target));     │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Phase 2: Initialization Execution                          │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Init::Manager::initialize()                       │    │
│  │  ├─ For each registered target:                    │    │
│  │  │   ├─ target->initialize(watcher)               │    │
│  │  │   └─ Wait for target->ready()                   │    │
│  │  │                                                  │    │
│  │  └─ When all targets ready:                        │    │
│  │      └─ Watcher notified → listener activation     │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  Phase 3: Listener Activation                               │
│  ┌────────────────────────────────────────────────────┐    │
│  │  All init targets complete → listener starts       │    │
│  │  accepting connections                              │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Init Manager Hierarchy

```cpp
// Listener-level init manager (for listener-wide initialization)
Init::Manager& listener_init = factory_context.initManager();

// Filter-chain-level init manager (for per-chain initialization)
Configuration::FilterChainFactoryContext& fc_ctx =
    factory_context.getFilterChainFactoryContext();
Init::Manager& fc_init = fc_ctx.initManager();

// Example: Filter using init manager
class MyFilter {
public:
  MyFilter(Config& config, Init::Manager& init_manager) {
    // Register initialization target
    init_target_ = std::make_unique<Init::TargetImpl>( "my_filter", [this] {
          // Async initialization
          loadConfiguration([this] { init_target_->ready(); // Signal ready
          });
        });

    init_manager.add(std::move(init_target_));
  }

private:
  Init::TargetHandlePtr init_target_;
};
```

### Init Manager Patterns

```cpp
// 1. Synchronous initialization (ready immediately)
auto target = std::make_unique<Init::TargetImpl>( "sync_filter", [this] { setupFilter();
        target->ready(); // Immediately ready
    });
init_manager.add(std::move(target));

// 2. Asynchronous initialization (network call, file load, etc.)
auto target = std::make_unique<Init::TargetImpl>( "async_filter", [this] {
        // Start async operation
        async_client_->fetch(url, [this](Response& resp) { processResponse(resp);
            target->ready(); // Ready when complete
        });
    });
init_manager.add(std::move(target));

// 3. Conditional initialization (skip if not needed)
if (config.requires_warmup()) { auto target = std::make_unique<Init::TargetImpl>( "conditional_filter", [this] { warmup([this] { target->ready(); }); });
  init_manager.add(std::move(target));
}
```

## Context Usage Patterns

### Pattern 1: Accessing Server-Wide Resources

```cpp
class MyNetworkFilter : public Network::ReadFilter {
public:
  MyNetworkFilter( Config& config, Configuration::FactoryContext& context)
      : config_(config), cluster_manager_(context.clusterManager()), time_source_(context.timeSource()), stats_scope_(context.scope()) {

    // Create filter-specific stats
    stats_ = generateStats(stats_scope_);

    // Access upstream cluster
    cluster_ = cluster_manager_.getThreadLocalCluster( config_.cluster_name());
  }

private:
  Upstream::ClusterManager& cluster_manager_;
  TimeSource& time_source_;
  Stats::Scope& stats_scope_;
};
```

### Pattern 2: Creating HTTP Filters with Context

```cpp
class MyHttpFilterFactory : public NamedHttpFilterConfigFactory {
public:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProto( const Protobuf::Message& proto_config, const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override {

    // Parse config
    auto config = std::make_shared<Config>(proto_config);

    // Capture context resources in lambda
    Upstream::ClusterManager& cm = context.clusterManager();
    Stats::Scope& scope = context.scope();
    TimeSource& time_source = context.timeSource();

    return [config, &cm, &scope, &time_source]( Http::FilterChainFactoryCallbacks& callbacks) {
      // Create filter with captured resources
      auto filter = std::make_shared<MyHttpFilter>( config, cm, scope, time_source);
      callbacks.addStreamDecoderFilter(filter);
    };
  }
};
```

### Pattern 3: Per-Filter-Chain Context

```cpp
// In filter chain factory builder
void buildFilterChain(const FilterChain& proto_config) {
  // Create per-filter-chain context
  auto fc_init_manager = std::make_unique<Init::ManagerImpl>("filter_chain");
  auto fc_stats_scope = listener_scope.createScope( fmt::format("filter_chain.{}", chain_name));

  auto fc_context = std::make_unique<PerFilterChainFactoryContextImpl>( listener_factory_context, *fc_init_manager, *fc_stats_scope);

  // Use filter-chain context for filter creation
  for (const auto& filter_config : proto_config.filters()) {
    // Get factory
    auto* factory = getFactory(filter_config.name());

    // Create filter factory callback with filter-chain context
    auto filter_cb = factory->createFilterFactoryFromProto( filter_config.typed_config(), *fc_context);  // Pass filter-chain context

    filter_factories.push_back(std::move(filter_cb));
  }
}
```

### Pattern 4: Draining Support

```cpp
// In filter implementation
class MyFilter {
public:
  MyFilter( Config& config, Configuration::FilterChainFactoryContext& fc_context)
      : config_(config), fc_context_(fc_context) {}

  void onData() {
    // Check if filter chain is draining
    if (fc_context_.isDraining()) {
      // Reject new requests or close connection gracefully
      sendServiceUnavailable();
      return;
    }

    // Normal processing
    processData();
  }

private:
  Configuration::FilterChainFactoryContext& fc_context_;
};

// Draining initiated by listener manager
void drainListener() { for (auto& filter_chain : filter_chains_) {
    // Mark all filter chains as draining
    filter_chain.context().startDraining();
  }
}
```

## Summary

Key takeaways about factory context hierarchy:

1. **Hierarchical Design**: Contexts inherit and extend capabilities from parent contexts
2. **Resource Scoping**: Each context level provides appropriately scoped resources
3. **Lifecycle Management**: Contexts manage initialization and draining
4. **Stats Isolation**: Per-listener and per-filter-chain stats scopes
5. **Init Coordination**: Init managers coordinate asynchronous initialization
6. **Draining Support**: Filter chains can gracefully drain connections

## Next Steps

Proceed to **Part 4: Network Filter Chain Creation** to see how network filter chains are instantiated using these contexts.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `envoy/server/factory_context.h` - Context interfaces
- `source/common/listener_manager/filter_chain_manager_impl.h` - Context implementations
- `envoy/init/manager.h` - Init manager interface
