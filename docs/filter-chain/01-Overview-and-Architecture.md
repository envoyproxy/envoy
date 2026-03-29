# Part 1: Envoy Filter Chain - Overview and Architecture

## Table of Contents
1. [Introduction](#introduction)
2. [What is a Filter Chain?](#what-is-a-filter-chain)
3. [Architecture Overview](#architecture-overview)
4. [Key Components](#key-components)
5. [Filter Types](#filter-types)
6. [Document Series Overview](#document-series-overview)

## Introduction

Envoy's filter architecture is the core of its extensibility and power. This document series explains how filter chains are created, configured, and executed in Envoy, from configuration parsing to runtime instantiation.

## What is a Filter Chain?

A **filter chain** is an ordered sequence of filters that process network connections or HTTP streams. Each filter in the chain can:
- Inspect and modify data
- Make routing decisions
- Add observability (logging, metrics, tracing)
- Enforce policies (authentication, rate limiting)
- Transform protocols

### Filter Chain Processing Model

```
┌─────────────────────────────────────────────────────────┐
│                    Connection Arrives                    │
└──────────────────────┬──────────────────────────────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │  Filter Chain Matcher  │
          │  (FilterChainManager)  │
          └────────┬───────────────┘
                   │
                   ▼
          ┌────────────────────────┐
          │  Selected FilterChain  │
          │  ┌──────────────────┐  │
          │  │ Transport Socket │  │ (TLS/mTLS)
          │  └──────────────────┘  │
          │  ┌──────────────────┐  │
          │  │ Network Filters  │  │ (TCP Proxy, HTTP)
          │  └──────────────────┘  │
          └────────┬───────────────┘
                   │
                   ▼
            [Data Processing]
```

## Architecture Overview

Envoy's filter chain architecture consists of three main layers:

### Layer 1: Configuration Layer
- Parses configuration from YAML/xDS
- Creates factory functions (not actual filters)
- Validates filter configurations
- Registers filters via the Registry pattern

### Layer 2: Factory Context Layer
- Provides scoped resources (stats, thread-local storage)
- Manages initialization and lifecycle
- Hierarchical context inheritance
- Per-listener, per-filter-chain contexts

### Layer 3: Runtime Instantiation Layer
- Creates actual filter instances per connection/stream
- Manages filter iteration and state
- Handles data flow and filter callbacks
- Controls filter lifecycle

### High-Level Architecture Diagram

```
┌───────────────────────────────────────────────────────────────┐
│                    CONFIGURATION PHASE                        │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│  Bootstrap Config (YAML/xDS)                                 │
│         │                                                     │
│         ▼                                                     │
│  ┌─────────────────┐                                         │
│  │ ListenerManager │                                         │
│  └────────┬────────┘                                         │
│           │                                                   │
│           ├─────────────┬──────────────┬──────────────┐      │
│           ▼             ▼              ▼              ▼      │
│    ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│    │Listener 1│  │Listener 2│  │Listener 3│  │Listener N│  │
│    └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  │
│         │             │              │              │        │
│         ▼             ▼              ▼              ▼        │
│    FilterChainManager instances                              │
│         │                                                     │
│         ├─── FilterChain 1 (Server Name: api.example.com)   │
│         │    ├─── Transport Socket Factory (TLS)            │
│         │    └─── Network Filter Factories [TCP, HTTP...]   │
│         │                                                     │
│         ├─── FilterChain 2 (Server Name: web.example.com)   │
│         │    ├─── Transport Socket Factory (TLS)            │
│         │    └─── Network Filter Factories [...]            │
│         │                                                     │
│         └─── Default FilterChain                             │
│              └─── Network Filter Factories [...]             │
│                                                               │
└───────────────────────────────────────────────────────────────┘

┌───────────────────────────────────────────────────────────────┐
│                      RUNTIME PHASE                            │
├───────────────────────────────────────────────────────────────┤
│                                                               │
│  New Connection → Listener                                    │
│         │                                                     │
│         ▼                                                     │
│  FilterChainManager::findFilterChain()                       │
│         │                                                     │
│         ├─ Match: Destination Port                           │
│         ├─ Match: Destination IP                             │
│         ├─ Match: Server Name (SNI)                          │
│         ├─ Match: Transport Protocol                         │
│         └─ Match: Source IP/Port                             │
│         │                                                     │
│         ▼                                                     │
│  Selected FilterChain                                         │
│         │                                                     │
│         ├─► Create Transport Socket (TLS Handshake)          │
│         │                                                     │
│         └─► Create Network Filter Chain                      │
│              │                                                │
│              ▼                                                │
│        ┌─────────────────┐                                   │
│        │ Filter Instance │ (Created per connection)          │
│        │    Manager      │                                   │
│        ├─────────────────┤                                   │
│        │ Filter 1        │ ← Factory Callback 1 creates     │
│        │ Filter 2        │ ← Factory Callback 2 creates     │
│        │ Filter N        │ ← Factory Callback N creates     │
│        └─────────────────┘                                   │
│              │                                                │
│              ▼                                                │
│        [Process Connection Data]                             │
│                                                               │
└───────────────────────────────────────────────────────────────┘
```

## Key Components

### 1. Filter Chain Factory Interface
- **Purpose**: Creates filter instances at runtime
- **Key Interface**: `Network::FilterChainFactory`
- **Implementation**: `ListenerImpl`
- **Method**: `createNetworkFilterChain(Connection&, NetworkFilterFactoriesList&)`

### 2. Filter Chain Manager
- **Purpose**: Selects the correct filter chain for a connection
- **Key Class**: `FilterChainManagerImpl`
- **Method**: `findFilterChain(ConnectionSocket&, StreamInfo&)`
- **Matching Criteria**: Destination/source IP/port, SNI, transport/application protocols

### 3. Factory Context Hierarchy
- **Purpose**: Provides scoped resources to filters
- **Base**: `CommonFactoryContext` (server-wide resources)
- **Listener**: `FactoryContext` (listener-scoped)
- **Filter Chain**: `FilterChainFactoryContext` (filter-chain-scoped)

### 4. Filter Factory Callback
- **Network Filters**: `Network::FilterFactoryCb = std::function<void(FilterManager&)>`
- **HTTP Filters**: `Http::FilterFactoryCb = std::function<void(FilterChainFactoryCallbacks&)>`
- **Purpose**: Deferred filter instantiation (created at connection/stream time)

### 5. Filter Manager
- **Network**: `Network::FilterManagerImpl` - manages read/write filter chains
- **HTTP**: `Http::DownstreamFilterManager` - manages decoder/encoder filter chains
- **Purpose**: Orchestrates filter iteration and data flow

## Filter Types

### Network Filters
Process L4 (TCP/UDP) connections before HTTP processing.

**Examples**:
- TCP Proxy
- HTTP Connection Manager
- Client SSL Auth
- Rate Limit
- RBAC (Role-Based Access Control)

**Interface**: `Network::ReadFilter`, `Network::WriteFilter`, `Network::Filter`

**Iteration**:
- Read filters: FIFO (first filter sees data first)
- Write filters: LIFO (last filter sees outgoing data first)

### HTTP Filters
Process HTTP/1.1, HTTP/2, HTTP/3 requests and responses within the HTTP Connection Manager.

**Examples**:
- Router
- CORS
- JWT Authentication
- External Authorization
- Compression
- Rate Limit
- Fault Injection

**Interface**: `Http::StreamDecoderFilter`, `Http::StreamEncoderFilter`, `Http::StreamFilter`

**Iteration**:
- Decoder filters: FIFO (A→B→C)
- Encoder filters: LIFO (C→B→A)

### Listener Filters
Process connections before filter chain matching.

**Examples**:
- TLS Inspector (extracts SNI for routing)
- Original Destination
- Proxy Protocol
- HTTP Inspector

**Interface**: `Network::ListenerFilter`

### Comparison Table

| Aspect | Network Filters | HTTP Filters | Listener Filters |
|--------|----------------|--------------|------------------|
| **Scope** | Per connection | Per HTTP stream | Pre-connection |
| **Protocol** | L4 (TCP/UDP) | L7 (HTTP) | L4 inspection |
| **When Created** | After filter chain match | Per HTTP stream | On accept() |
| **Iteration** | Read: FIFO, Write: LIFO | Decoder: FIFO, Encoder: LIFO | FIFO |
| **Example** | TCP Proxy, HTTP CM | Router, Auth | TLS Inspector |

## Document Series Overview

This 10-part series covers the complete filter chain creation flow:

1. **Part 1: Overview and Architecture** (This Document)
   - Introduction to filter chains
   - Architecture layers
   - Key components

2. **Part 2: Filter Chain Factory Construction**
   - Factory pattern implementation
   - Factory registration and discovery
   - Configuration to factory mapping

3. **Part 3: Factory Context Hierarchy**
   - Context inheritance model
   - Resource scoping
   - Init manager integration

4. **Part 4: Network Filter Chain Creation**
   - Network filter instantiation
   - FilterChainManager matching
   - Connection-level filter lifecycle

5. **Part 5: HTTP Filter Chain Creation**
   - HTTP filter configuration
   - Stream-level filter lifecycle
   - Per-route configuration

6. **Part 6: Filter Chain Matching and Selection**
   - Matching tree algorithm
   - SNI, ALPN, destination matching
   - Filter chain selection flow

7. **Part 7: Filter Configuration and Registration**
   - Configuration protobuf parsing
   - Registry pattern
   - Filter factory interfaces

8. **Part 8: Runtime Filter Instantiation**
   - Filter callback execution
   - Filter manager implementation
   - Data flow and iteration

9. **Part 9: UML Class Diagrams**
   - Class relationships
   - Inheritance hierarchies
   - Key interfaces

10. **Part 10: Sequence Diagrams and Flow Charts**
    - End-to-end flow sequences
    - Configuration to runtime flow
    - Connection processing flow

## Key Design Principles

### 1. Lazy Instantiation
Filters are created at runtime (per connection/stream), not at configuration time. This allows:
- Memory efficiency (filters only exist when needed)
- Per-connection state isolation
- Dynamic configuration updates

### 2. Factory Pattern
Factories create factories (factory callbacks) which create filters:
```
Config → NamedFilterConfigFactory → FilterFactoryCb → Filter Instance
```

### 3. Context Scoping
Resources are scoped hierarchically:
- Server-wide: Cluster manager, thread-local storage
- Listener-wide: Listener stats, drain decision
- Filter-chain-wide: Init manager, stats scope

### 4. Separation of Concerns
- **Configuration layer**: Validates and parses config
- **Factory layer**: Creates factories, not filters
- **Runtime layer**: Instantiates and manages filters

### 5. Iteration Control
Filters return status codes to control iteration:
- **Continue**: Process next filter
- **StopIteration**: Halt iteration (can resume later)
- **StopIterationAndBuffer**: Buffer data and halt
- **StopIterationNoBuffer**: Discard buffered data

## Glossary

| Term | Definition |
|------|------------|
| **Filter Chain** | Ordered sequence of filters processing a connection/stream |
| **Filter Factory** | Creates filter instances at runtime |
| **Factory Callback** | `std::function` that creates and installs filters |
| **Factory Context** | Provides scoped resources to filters |
| **Filter Manager** | Orchestrates filter iteration and data flow |
| **Terminal Filter** | Last filter in chain (must handle all data) |
| **Listener** | Accepts incoming connections on a port |
| **Filter Chain Match** | Criteria for selecting which filter chain to use |

## Next Steps

Proceed to **Part 2: Filter Chain Factory Construction** to learn how filter factories are created from configuration and how the factory pattern is implemented in Envoy.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `envoy/network/filter.h`
- `envoy/http/filter_factory.h`
- `envoy/server/filter_config.h`
- `source/common/listener_manager/filter_chain_manager_impl.h`
