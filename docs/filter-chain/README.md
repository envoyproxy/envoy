# Envoy Filter Chain Creation Flow - Complete Documentation

This directory contains comprehensive documentation explaining Envoy's filter chain creation flow, from configuration parsing to runtime filter instantiation and execution.

## Documentation Series

### Part 1: [Overview and Architecture](01-Overview-and-Architecture.md)
**Topics Covered:**
- Introduction to filter chains
- Architecture layers (Configuration, Factory Context, Runtime)
- High-level architecture diagrams
- Filter types (Network, HTTP, Listener)
- Key components and design principles

**Best For:** Getting started, understanding the big picture

---

### Part 2: [Filter Chain Factory Construction](02-Filter-Chain-Factory-Construction.md)
**Topics Covered:**
- Double factory pattern (Config Factory → Filter Factory Callback → Filter Instance)
- Factory registration and discovery
- Configuration to factory mapping
- Network, HTTP, and Listener filter factories
- Factory lifecycle and shared config pattern

**Best For:** Understanding how filters are registered and discovered

---

### Part 3: [Factory Context Hierarchy](03-Factory-Context-Hierarchy.md)
**Topics Covered:**
- Context inheritance model
- Resource scoping (Server, Listener, Filter Chain)
- Context implementations
- Init manager integration
- Draining support

**Best For:** Understanding how filters access server resources

---

### Part 4: [Network Filter Chain Creation](04-Network-Filter-Chain-Creation.md)
**Topics Covered:**
- Network filter chain instantiation flow
- FilterChain and FilterChainManager
- Filter chain selection
- Filter manager and iteration
- Complete connection flow example

**Best For:** Understanding L4 (TCP/UDP) filter processing

---

### Part 5: [HTTP Filter Chain Creation](05-HTTP-Filter-Chain-Creation.md)
**Topics Covered:**
- HTTP filter chain within HTTP Connection Manager
- FilterChainHelper and HTTP filter configuration
- HTTP filter manager and iteration
- Per-route configuration
- Stream-level filter lifecycle

**Best For:** Understanding L7 (HTTP) filter processing

---

### Part 6: [Filter Chain Matching and Selection](06-Filter-Chain-Matching-and-Selection.md)
**Topics Covered:**
- Filter chain match criteria (destination, SNI, ALPN, source)
- Matching algorithm and precedence
- Listener filters (TLS Inspector)
- Matching data structures and performance optimizations

**Best For:** Understanding how connections are routed to filter chains

---

### Part 7: [Filter Configuration and Registration](07-Filter-Configuration-and-Registration.md)
**Topics Covered:**
- Protobuf configuration schemas
- Registry system implementation
- Filter factory registration (REGISTER_FACTORY)
- Configuration validation layers
- Dynamic configuration updates (xDS)

**Best For:** Understanding configuration processing and validation

---

### Part 8: [Runtime Filter Instantiation](08-Runtime-Filter-Instantiation.md)
**Topics Covered:**
- Filter instance lifecycle
- Network and HTTP filter instantiation
- Filter callbacks and iteration control
- Memory management
- Error handling and performance considerations

**Best For:** Understanding filter runtime behavior

---

### Part 9: [UML Class Diagrams](09-UML-Class-Diagrams.md)
**Topics Covered:**
- Factory context class hierarchy
- Filter interface hierarchy
- Filter factory classes
- Filter chain classes
- Filter manager classes
- Registry system classes
- Complete system overview

**Best For:** Understanding class relationships and structure

---

### Part 10: [Sequence Diagrams and Flow Charts](10-Sequence-Diagrams-and-Flow-Charts.md)
**Topics Covered:**
- Configuration phase sequences
- Network filter chain creation sequence
- HTTP filter chain creation sequence
- Filter chain matching flow
- Data processing flow (Network and HTTP)
- Complete end-to-end flow

**Best For:** Understanding timing and interactions

---

## Quick Navigation

### By Role

**New to Envoy:**
1. Start with Part 1 (Overview)
2. Read Part 2 (Factory Construction)
3. Review Part 9 (UML Diagrams)
4. Check Part 10 (Sequence Diagrams)

**Implementing a Filter:**
1. Part 2 (Factory Construction)
2. Part 7 (Configuration and Registration)
3. Part 8 (Runtime Instantiation)
4. Part 4 or 5 (depending on Network or HTTP filter)

**Debugging Filter Issues:**
1. Part 6 (Filter Chain Matching) - for routing issues
2. Part 8 (Runtime Instantiation) - for filter lifecycle issues
3. Part 4 or 5 (for iteration issues)

**Understanding Performance:**
1. Part 3 (Factory Context) - resource scoping
2. Part 8 (Runtime Instantiation) - memory management
3. Part 6 (Matching) - matching optimizations

### By Topic

**Configuration:**
- Parts 2, 3, 7

**Network Filters:**
- Parts 4, 6, 8

**HTTP Filters:**
- Parts 5, 8

**Architecture:**
- Parts 1, 9, 10

**Advanced Topics:**
- Part 3 (Init Manager, Draining)
- Part 6 (Matching Algorithm)
- Part 7 (Dynamic Configuration)

---

## Key Concepts

### Factory Pattern
Envoy uses a double factory pattern:
```
Config → NamedFilterConfigFactory → FilterFactoryCb → Filter Instance
```
- **NamedFilterConfigFactory**: Created at startup, registered in global Registry
- **FilterFactoryCb**: Created per configuration, captures parsed config
- **Filter Instance**: Created per connection/stream

### Filter Iteration
- **Network Filters**: Read (FIFO), Write (LIFO)
- **HTTP Filters**: Decoder (FIFO), Encoder (LIFO)
- Filters can stop iteration for async operations

### Filter Chain Matching
Connections matched to filter chains based on:
1. Destination IP/Port
2. Server Name (SNI)
3. Transport Protocol
4. Application Protocols (ALPN)
5. Source IP/Port

### Context Hierarchy
Resources scoped hierarchically:
```
Server → Listener → Filter Chain
```

---

## Code References

### Key Headers
- `envoy/network/filter.h` - Network filter interfaces
- `envoy/http/filter.h` - HTTP filter interfaces
- `envoy/server/filter_config.h` - Factory interfaces
- `envoy/server/factory_context.h` - Context interfaces
- `envoy/registry/registry.h` - Registry system

### Key Implementation Files
- `source/common/listener_manager/listener_impl.cc` - Listener and network filter chain
- `source/common/listener_manager/filter_chain_manager_impl.cc` - Filter chain matching
- `source/common/http/conn_manager_impl.cc` - HTTP Connection Manager
- `source/common/http/filter_chain_helper.cc` - HTTP filter configuration
- `source/common/network/filter_manager_impl.cc` - Network filter manager
- `source/common/http/filter_manager.cc` - HTTP filter manager

---

## Related Documentation

### Envoy Official Docs
- [Architecture Overview](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/arch_overview)
- [Filter Development Guide](https://www.envoyproxy.io/docs/envoy/latest/extending/extending)
- [HTTP Filters](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/http_filters)
- [Network Filters](https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/network_filters)

### API Documentation
- [Listener API](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/listener/v3/listener.proto)
- [HTTP Connection Manager](https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/filters/network/http_connection_manager/v3/http_connection_manager.proto)
- [Filter Chain](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/listener/v3/listener_components.proto#envoy-v3-api-msg-config-listener-v3-filterchain)

---

## Contributing

This documentation was created to provide in-depth understanding of Envoy's filter chain architecture. If you find errors or have suggestions for improvement, please:

1. Review the relevant source code in the Envoy repository
2. Verify against the latest Envoy version
3. Submit corrections with references to code paths

---

## Document Metadata

- **Version**: 1.0
- **Created**: 2026-02-28
- **Envoy Version Reference**: Latest main branch (as of creation date)
- **Total Documents**: 10
- **Total Pages**: ~100+ (estimated)
- **Format**: Markdown with ASCII diagrams

---

## Summary

This 10-part series provides comprehensive coverage of Envoy's filter chain creation flow:

1. **Configuration Phase**: How filter chains are configured and validated
2. **Factory Phase**: How filter factories are registered and discovered
3. **Instantiation Phase**: How filter instances are created per connection/stream
4. **Execution Phase**: How filters process data
5. **Architecture**: Visual representations of classes and flows

Whether you're implementing a new filter, debugging an issue, or simply learning how Envoy works, this documentation provides the deep technical knowledge you need.
