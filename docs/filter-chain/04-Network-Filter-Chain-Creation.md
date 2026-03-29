# Part 4: Network Filter Chain Creation

## Table of Contents
1. [Introduction](#introduction)
2. [Network Filter Chain Overview](#network-filter-chain-overview)
3. [Filter Chain Data Structures](#filter-chain-data-structures)
4. [Filter Chain Selection](#filter-chain-selection)
5. [Network Filter Instantiation](#network-filter-instantiation)
6. [Filter Manager](#filter-manager)
7. [Filter Iteration](#filter-iteration)
8. [Complete Flow Example](#complete-flow-example)

## Introduction

Network filters operate at the L4 (TCP/UDP) layer and are created per connection. This document explains how network filter chains are configured, selected, and instantiated when a new connection arrives.

## Network Filter Chain Overview

### Network Filter Processing Model

```
┌──────────────────────────────────────────────────────────────┐
│           NETWORK FILTER CHAIN PROCESSING                    │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  New Connection Arrives                                      │
│         │                                                    │
│         ▼                                                    │
│  ┌──────────────────────────┐                               │
│  │  Listener Filter Chain   │ (Pre-processing)              │
│  │  ┌────────────────────┐  │                               │
│  │  │ TLS Inspector      │  │ ← Extract SNI                │
│  │  │ Proxy Protocol     │  │ ← Parse source IP            │
│  │  │ Original Dst       │  │ ← Restore original dest      │
│  │  └────────────────────┘  │                               │
│  └────────────┬─────────────┘                               │
│               │                                              │
│               ▼                                              │
│  ┌──────────────────────────┐                               │
│  │ FilterChainManager       │                               │
│  │ findFilterChain()        │                               │
│  │                          │                               │
│  │ Match by:                │                               │
│  │  ├─ Destination port     │                               │
│  │  ├─ Destination IP       │                               │
│  │  ├─ Server name (SNI)    │                               │
│  │  ├─ Transport protocol   │                               │
│  │  └─ Source IP/port       │                               │
│  └────────────┬─────────────┘                               │
│               │                                              │
│               ▼                                              │
│  ┌──────────────────────────┐                               │
│  │  Selected FilterChain    │                               │
│  │  ┌────────────────────┐  │                               │
│  │  │ Transport Socket   │  │ ← TLS handshake              │
│  │  └────────────────────┘  │                               │
│  │  ┌────────────────────┐  │                               │
│  │  │ Network Filters    │  │                               │
│  │  │  ├─ RBAC           │  │ ← Authorization              │
│  │  │  ├─ Rate Limit     │  │ ← Rate limiting              │
│  │  │  ├─ TCP Proxy or   │  │                               │
│  │  │  └─ HTTP CM        │  │ ← Terminal filter            │
│  │  └────────────────────┘  │                               │
│  └────────────┬─────────────┘                               │
│               │                                              │
│               ▼                                              │
│  ┌──────────────────────────┐                               │
│  │  Data Processing         │                               │
│  │  Read: FIFO iteration    │  Filter1 → Filter2 → Filter3  │
│  │  Write: LIFO iteration   │  Filter3 ← Filter2 ← Filter1  │
│  └──────────────────────────┘                               │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## Filter Chain Data Structures

### FilterChain Interface

```cpp
// envoy/network/filter.h

class FilterChain {
public:
  virtual ~FilterChain() = default;

  // Get transport socket factory (for TLS/SSL)
  virtual const DownstreamTransportSocketFactory& transportSocketFactory() const PURE;

  // Get list of network filter factory callbacks
  virtual const NetworkFilterFactoriesList& networkFilterFactories() const PURE;

  // Transport socket connection timeout
  virtual std::chrono::milliseconds
      transportSocketConnectTimeout() const PURE;

  // Filter chain name
  virtual absl::string_view name() const PURE;

  // Metadata
  virtual const envoy::config::core::v3::Metadata& metadata() const PURE;
};

// Type definitions
using NetworkFilterFactoriesList =
    std::vector<FilterConfigProviderPtr<FilterFactoryCb>>;

using FilterFactoryCb =
    std::function<void(FilterManager& manager)>;
```

### FilterChainImpl Implementation

```cpp
// source/common/listener_manager/filter_chain_manager_impl.h

class FilterChainImpl : public FilterChain {
public:
  FilterChainImpl( DownstreamTransportSocketFactoryPtr&& transport_socket_factory, NetworkFilterFactoriesList&& network_filter_factories, std::chrono::milliseconds transport_socket_connect_timeout, std::string name, envoy::config::core::v3::Metadata metadata)
      : transport_socket_factory_(std::move(transport_socket_factory)), network_filter_factories_(std::move(network_filter_factories)), transport_socket_connect_timeout_(transport_socket_connect_timeout), name_(std::move(name)), metadata_(std::move(metadata)) {}

  const DownstreamTransportSocketFactory& transportSocketFactory() const override {
    return *transport_socket_factory_;
  }

  const NetworkFilterFactoriesList& networkFilterFactories() const override {
    return network_filter_factories_;
  }

  std::chrono::milliseconds transportSocketConnectTimeout() const override {
    return transport_socket_connect_timeout_;
  }

  absl::string_view name() const override {
    return name_; }

  const envoy::config::core::v3::Metadata& metadata() const override {
    return metadata_;
  }

  // Set filter chain factory context
  void setFilterChainFactoryContext( FilterChainFactoryContextPtr context) { filter_chain_context_ = std::move(context);
  }

private:
  DownstreamTransportSocketFactoryPtr transport_socket_factory_;
  NetworkFilterFactoriesList network_filter_factories_;
  std::chrono::milliseconds transport_socket_connect_timeout_;
  std::string name_;
  envoy::config::core::v3::Metadata metadata_;
  FilterChainFactoryContextPtr filter_chain_context_;
};
```

### FilterChainManager Interface

```cpp
class FilterChainManager {
public:
  virtual ~FilterChainManager() = default;

  // Find matching filter chain for a connection
  virtual const FilterChain* findFilterChain( const ConnectionSocket& socket, const StreamInfo::StreamInfo& stream_info) const PURE;

  // Get all filter chains
  virtual const std::vector<const FilterChain*>& filterChains() const PURE;
};
```

## Filter Chain Selection

### FilterChainManagerImpl

The filter chain manager matches connections to filter chains using a hierarchical matching algorithm.

```cpp
// source/common/listener_manager/filter_chain_manager_impl.h

class FilterChainManagerImpl : public FilterChainManager {
public:
  const FilterChain* findFilterChain( const ConnectionSocket& socket, const StreamInfo::StreamInfo& stream_info) const override;

private:
  // Matching tree structure
  struct FilterChainsByDestinationPort {
    // Exact destination ports
    absl::flat_hash_map<uint16_t, FilterChainsByServerNames> destination_ports_;
    // Wildcard (any port)
    std::unique_ptr<FilterChainsByServerNames> any_port_;
  };

  struct FilterChainsByServerNames {
    // Exact server names (e.g., "api.example.com")
    absl::flat_hash_map<std::string, FilterChainsByTransportProtocol> server_names_;
    // Wildcard server names (e.g., "*.example.com")
    std::vector<std::pair<std::string, FilterChainsByTransportProtocol>> wildcard_server_names_;
    // No server name
    std::unique_ptr<FilterChainsByTransportProtocol> no_server_name_;
  };

  struct FilterChainsByTransportProtocol { absl::flat_hash_map<std::string, FilterChainsByApplicationProtocols> protocols_;
  };

  struct FilterChainsByApplicationProtocols { absl::flat_hash_map<std::string, FilterChainPtr> protocols_;
    FilterChainPtr default_filter_chain_;
  };

  // Root of matching tree
  FilterChainsByDestinationIP destination_ips_;
  FilterChainPtr default_filter_chain_;
};
```

### Filter Chain Matching Algorithm

```
┌──────────────────────────────────────────────────────────────┐
│           FILTER CHAIN MATCHING ALGORITHM                    │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Step 1: Match Destination IP                               │
│    ├─ Exact match: 192.168.1.10                             │
│    ├─ CIDR match: 192.168.1.0/24                            │
│    └─ Wildcard: 0.0.0.0/0 (any IP)                          │
│         │                                                    │
│         ▼                                                    │
│  Step 2: Match Destination Port                             │
│    ├─ Exact match: 8443                                     │
│    └─ Wildcard: 0 (any port)                                │
│         │                                                    │
│         ▼                                                    │
│  Step 3: Match Server Name (SNI from TLS)                   │
│    ├─ Exact match: api.example.com                          │
│    ├─ Wildcard match: *.example.com                         │
│    ├─ Suffix match: longest suffix wins                     │
│    └─ No SNI                                                 │
│         │                                                    │
│         ▼                                                    │
│  Step 4: Match Transport Protocol                           │
│    ├─ "tls" (TLS connection)                                │
│    ├─ "raw_buffer" (plain TCP)                              │
│    └─ Empty (any protocol)                                  │
│         │                                                    │
│         ▼                                                    │
│  Step 5: Match Application Protocol (ALPN)                  │
│    ├─ "h2" (HTTP/2)                                         │
│    ├─ "http/1.1"                                            │
│    └─ Empty (any application protocol)                      │
│         │                                                    │
│         ▼                                                    │
│  Step 6: Match Source IP/Port (optional)                    │
│    ├─ Source IP ranges                                      │
│    ├─ Source port ranges                                    │
│    └─ Default if no match                                   │
│         │                                                    │
│         ▼                                                    │
│  Step 7: Return Matched FilterChain                         │
│    └─ Or return default filter chain if configured          │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Matching Example

```cpp
// Connection arrives with:
// - Destination: 192.168.1.10:8443
// - SNI: api.example.com
// - ALPN: h2
// - Source: 10.0.0.5:52345

const FilterChain* FilterChainManagerImpl::findFilterChain( const ConnectionSocket& socket, const StreamInfo::StreamInfo& stream_info) const {

  // Extract connection attributes
  const auto& local_address = socket.connectionInfoProvider().localAddress();
  const auto& remote_address = socket.connectionInfoProvider().remoteAddress();
  const std::string& server_name = socket.requestedServerName();
  const std::vector<std::string>& app_protocols =
      socket.requestedApplicationProtocols();

  // Step 1: Match destination IP
  auto dest_ip_match = findDestinationIP(local_address->ip());
  if (!dest_ip_match) return default_filter_chain_.get();

  // Step 2: Match destination port
  auto dest_port_match = findDestinationPort( *dest_ip_match, local_address->ip()->port());
  if (!dest_port_match) return default_filter_chain_.get();

  // Step 3: Match server name (SNI)
  auto server_name_match = findServerName(*dest_port_match, server_name);
  if (!server_name_match) return default_filter_chain_.get();

  // Step 4: Match transport protocol
  const std::string& transport_protocol =
      socket.detectedTransportProtocol();
  auto transport_match = findTransportProtocol( *server_name_match, transport_protocol);
  if (!transport_match) return default_filter_chain_.get();

  // Step 5: Match application protocols (ALPN)
  auto app_protocol_match = findApplicationProtocol( *transport_match, app_protocols);
  if (!app_protocol_match) return default_filter_chain_.get();

  // Step 6: Match source IP/port (if configured)
  if (hasSourceMatching(*app_protocol_match)) { auto source_match = findSourceMatch( *app_protocol_match, remote_address);
    if (source_match) return source_match;
  }

  return app_protocol_match;
}
```

## Network Filter Instantiation

### Filter Chain Creation Flow

```
┌──────────────────────────────────────────────────────────────┐
│        NETWORK FILTER CHAIN INSTANTIATION FLOW               │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  1. Connection Accepted                                      │
│     └─► Listener::onAccept(socket)                          │
│                                                              │
│  2. Listener Filters Run                                     │
│     └─► ListenerFilterManager::accept()                     │
│          ├─ TLS Inspector extracts SNI                      │
│          └─ Proxy Protocol parses header                    │
│                                                              │
│  3. Find Filter Chain                                        │
│     └─► FilterChainManager::findFilterChain(socket, info)   │
│          └─ Returns selected FilterChain                    │
│                                                              │
│  4. Create Transport Socket                                  │
│     └─► FilterChain::transportSocketFactory()               │
│          .createTransportSocket(options)                    │
│          └─ Creates SSL/TLS socket if configured            │
│                                                              │
│  5. Create Connection                                        │
│     └─► Dispatcher::createServerConnection(socket)          │
│          └─ Wraps socket in Connection object               │
│                                                              │
│  6. Create Network Filter Chain                              │
│     └─► NetworkFilterChainFactory::createNetworkFilterChain()│
│          ├─ Get NetworkFilterFactoriesList from FilterChain │
│          ├─ For each FilterFactoryCb:                       │
│          │   ├─ Invoke callback(filter_manager)            │
│          │   │   └─ Callback creates filter instance       │
│          │   └─ Filter added to FilterManager              │
│          │       ├─ addReadFilter()                         │
│          │       ├─ addWriteFilter()                        │
│          │       └─ addFilter()                             │
│          └─ Return success                                  │
│                                                              │
│  7. Initialize Filters                                       │
│     └─► FilterManager::initializeReadFilters()              │
│          └─ Calls onNewConnection() on each filter          │
│                                                              │
│  8. Begin Processing                                         │
│     └─► Connection::read() triggered                        │
│          └─ Data flows through filter chain                 │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### ListenerImpl::createNetworkFilterChain

```cpp
// source/common/listener_manager/listener_impl.h

class ListenerImpl : public Network::FilterChainFactory {
public:
  bool createNetworkFilterChain( Network::Connection& connection, const std::vector<Network::FilterFactoryCb>& filter_factories) override {

    // Create filter manager for this connection
    auto& filter_manager = connection.filterManager();

    // Iterate through filter factory callbacks
    for (const auto& filter_factory_cb : filter_factories) {
      // Invoke factory callback to create and install filter
      // The callback will call filter_manager.addReadFilter/addWriteFilter/addFilter
      filter_factory_cb(filter_manager);
    }

    // Initialize read filters (call onNewConnection())
    filter_manager.initializeReadFilters();

    return true;
  }
};
```

### Factory Callback Invocation Example

```cpp
// Example filter factory callback
Network::FilterFactoryCb tcp_proxy_factory =
    [config](Network::FilterManager& manager) {
      // Create TCP proxy filter instance
      auto filter = std::make_shared<TcpProxyFilter>( config, config->clusterManager());

      // Add as bidirectional filter (both read and write)
      manager.addFilter(filter);
    };

// When connection arrives:
tcp_proxy_factory(connection.filterManager());
// → Creates TcpProxyFilter instance
// → Adds to filter manager
// → Filter now ready to process data
```

## Filter Manager

### FilterManagerImpl

The filter manager orchestrates filter iteration and manages filter state.

```cpp
// source/common/network/filter_manager_impl.h

class FilterManagerImpl : public FilterManager {
public:
  // Add filters
  void addReadFilter(ReadFilterSharedPtr filter) override;
  void addWriteFilter(WriteFilterSharedPtr filter) override;
  void addFilter(FilterSharedPtr filter) override;

  // Initialize filters
  void initializeReadFilters() override;

  // Data processing entry points
  FilterStatus onRead() override;
  FilterStatus onWrite() override;

private:
  // Active filter wrappers
  struct ActiveReadFilter { ReadFilterSharedPtr filter_;
    bool initialized_{false};
  };

  struct ActiveWriteFilter { WriteFilterSharedPtr filter_;
    bool initialized_{false};
  };

  // Filter lists
  std::list<ActiveReadFilter> upstream_filters_;  // Read filters
  std::list<ActiveWriteFilter> downstream_filters_;  // Write filters

  // Current iteration state
  std::list<ActiveReadFilter>::iterator current_read_filter_;
  std::list<ActiveWriteFilter>::iterator current_write_filter_;
};
```

### Adding Filters

```cpp
void FilterManagerImpl::addReadFilter(ReadFilterSharedPtr filter) { ActiveReadFilter active_filter;
  active_filter.filter_ = filter;

  // Add to end of read filter list
  upstream_filters_.push_back(std::move(active_filter));

  // Initialize filter callbacks
  filter->initializeReadFilterCallbacks(*this);
}

void FilterManagerImpl::addWriteFilter(WriteFilterSharedPtr filter) { ActiveWriteFilter active_filter;
  active_filter.filter_ = filter;

  // Add to end of write filter list (LIFO iteration)
  downstream_filters_.push_back(std::move(active_filter));

  // Initialize filter callbacks
  filter->initializeWriteFilterCallbacks(*this);
}

void FilterManagerImpl::addFilter(FilterSharedPtr filter) {
  // Add as both read and write filter
  addReadFilter(filter);
  addWriteFilter(filter);
}
```

### Initializing Filters

```cpp
void FilterManagerImpl::initializeReadFilters() {
  // Call onNewConnection() on all read filters in order
  for (auto& active_filter : upstream_filters_) { if (!active_filter.initialized_) { active_filter.initialized_ = true;

      FilterStatus status = active_filter.filter_->onNewConnection();

      if (status == FilterStatus::StopIteration) {
        // Filter wants to stop iteration
        return;
      }
    }
  }
}
```

## Filter Iteration

### Read Filter Iteration (FIFO)

```cpp
FilterStatus FilterManagerImpl::onRead() {
  // Iterate through read filters in FIFO order
  for (auto it = upstream_filters_.begin();
       it != upstream_filters_.end();
       ++it) {

    Buffer::Instance& read_buffer = connection_.readBuffer();

    if (read_buffer.length() == 0) {
      // No more data to process
      return FilterStatus::Continue;
    }

    FilterStatus status = it->filter_->onData(read_buffer, end_stream);

    if (status == FilterStatus::StopIteration) {
      // Filter wants to stop iteration (e.g., needs more data)
      current_read_filter_ = it;
      return FilterStatus::StopIteration;
    }

    if (status == FilterStatus::Continue && read_buffer.length() == 0) {
      // Filter consumed all data
      return FilterStatus::Continue;
    }
  }

  return FilterStatus::Continue;
}
```

### Write Filter Iteration (LIFO)

```cpp
FilterStatus FilterManagerImpl::onWrite() {
  // Iterate through write filters in LIFO order (reverse)
  for (auto it = downstream_filters_.rbegin();
       it != downstream_filters_.rend();
       ++it) {

    Buffer::Instance& write_buffer = connection_.writeBuffer();

    if (write_buffer.length() == 0) {
      return FilterStatus::Continue;
    }

    FilterStatus status = it->filter_->onWrite(write_buffer, end_stream);

    if (status == FilterStatus::StopIteration) { current_write_filter_ = it.base();
      return FilterStatus::StopIteration;
    }

    if (status == FilterStatus::Continue && write_buffer.length() == 0) {
      return FilterStatus::Continue;
    }
  }

  return FilterStatus::Continue;
}
```

### Filter Iteration Diagram

```
Read Filters (Downstream → Upstream): FIFO
┌──────────┐     ┌──────────┐     ┌──────────┐
│ Filter A │ ──► │ Filter B │ ──► │ Filter C │
│  (RBAC)  │     │  (Rate   │     │  (TCP    │
│          │     │  Limit)  │     │  Proxy)  │
└──────────┘     └──────────┘     └──────────┘
     │                │                │
     ▼                ▼                ▼
  onData()        onData()        onData()
  Continue        Continue        StopIteration
                                  (terminal filter)

Write Filters (Upstream → Downstream): LIFO
┌──────────┐     ┌──────────┐     ┌──────────┐
│ Filter C │ ◄── │ Filter B │ ◄── │ Filter A │
│  (TCP    │     │  (Rate   │     │  (RBAC)  │
│  Proxy)  │     │  Limit)  │     │          │
└──────────┘     └──────────┘     └──────────┘
     │                │                │
     ▼                ▼                ▼
  onWrite()       onWrite()       onWrite()
  Continue        Continue        Continue
```

## Complete Flow Example

### Example Configuration

```yaml
listeners:
- name: main_listener
  address:
    socket_address:
      address: 0.0.0.0
      port_value: 8443
  filter_chains:
  - filter_chain_match:
      server_names: ["api.example.com"]
      transport_protocol: "tls"
      application_protocols: ["h2"]
    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
        common_tls_context:
          tls_certificates:
          - certificate_chain: {filename: "/certs/cert.pem"}
            private_key: {filename: "/certs/key.pem"}
    filters:
    - name: envoy.filters.network.rbac
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
        rules: {...}
    - name: envoy.filters.network.http_connection_manager
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
        stat_prefix: ingress_http
        http_filters:
        - name: envoy.filters.http.router
```

### Runtime Flow

```
1. Configuration Parse Phase:
   ├─ Parse listener config
   ├─ Parse filter chain match criteria
   ├─ Create transport socket factory (TLS)
   ├─ For RBAC filter:
   │   ├─ Lookup factory: "envoy.filters.network.rbac"
   │   ├─ Call createFilterFactoryFromProto()
   │   └─ Store FilterFactoryCb
   ├─ For HTTP CM filter:
   │   ├─ Lookup factory: "envoy.filters.network.http_connection_manager"
   │   ├─ Parse nested HTTP filters
   │   ├─ Call createFilterFactoryFromProto()
   │   └─ Store FilterFactoryCb
   └─ Build FilterChainImpl with factories

2. Connection Arrival:
   ├─ Client connects to 0.0.0.0:8443
   ├─ TLS Inspector extracts SNI: "api.example.com"
   ├─ TLS Inspector extracts ALPN: "h2"
   └─ Socket ready for filter chain matching

3. Filter Chain Selection:
   ├─ FilterChainManager::findFilterChain()
   ├─ Match destination port: 8443 ✓
   ├─ Match server name: "api.example.com" ✓
   ├─ Match transport protocol: "tls" ✓
   ├─ Match application protocol: "h2" ✓
   └─ Return FilterChain

4. Transport Socket Creation:
   ├─ Get DownstreamTlsContext
   ├─ Create TLS transport socket
   ├─ Load certificate chain
   ├─ Perform TLS handshake
   └─ Connection encrypted

5. Network Filter Chain Creation:
   ├─ Get NetworkFilterFactoriesList
   ├─ For each FilterFactoryCb:
   │   ├─ RBAC FilterFactoryCb invoked:
   │   │   ├─ Create RbacFilter instance
   │   │   └─ manager.addReadFilter(rbac_filter)
   │   │
   │   └─ HTTP CM FilterFactoryCb invoked:
   │       ├─ Create HttpConnectionManagerImpl instance
   │       └─ manager.addFilter(http_cm)
   │
   └─ FilterManager::initializeReadFilters()
       ├─ rbac_filter->onNewConnection() → Continue
       └─ http_cm->onNewConnection() → Continue

6. Data Processing:
   ├─ Client sends HTTP/2 request
   ├─ FilterManager::onRead():
   │   ├─ rbac_filter->onData() → Authorize → Continue
   │   └─ http_cm->onData() → Parse HTTP → StopIteration
   │       └─ (HTTP CM is terminal, doesn't continue)
   │
   ├─ HTTP CM processes request internally
   │   └─ (Creates HTTP filter chain - see Part 5)
   │
   └─ Response path:
       └─ FilterManager::onWrite():
           ├─ http_cm->onWrite() → Encode HTTP → Continue
           └─ rbac_filter->onWrite() → Continue
```

## Summary

Key takeaways about network filter chain creation:

1. **Filter Chain Selection**: Hierarchical matching by destination, SNI, protocols, source
2. **Lazy Instantiation**: Filter instances created per connection, not at config time
3. **Factory Callbacks**: Stored at config time, invoked at connection time
4. **Filter Manager**: Orchestrates filter iteration in FIFO (read) and LIFO (write) order
5. **Terminal Filters**: Last filter in chain (TCP Proxy or HTTP CM)
6. **Transport Sockets**: TLS handshake before filter chain creation

## Next Steps

Proceed to **Part 5: HTTP Filter Chain Creation** to learn how HTTP filters are created within the HTTP Connection Manager.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `source/common/listener_manager/listener_impl.cc` - Network filter chain creation
- `source/common/network/filter_manager_impl.cc` - Filter manager implementation
- `source/common/listener_manager/filter_chain_manager_impl.cc` - Filter chain matching
