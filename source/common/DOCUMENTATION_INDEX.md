# Envoy Source Documentation Index

**Purpose:** This index helps you navigate the comprehensive documentation for Envoy's core subsystems.

---

## Quick Start Guides

### New to Envoy?
Start with these foundational documents:

1. **[Sockets, Connections, and Listeners Explained](listener_manager/SOCKETS_CONNECTIONS_LISTENERS_EXPLAINED.md)**
   - Socket 101: Listen vs connected sockets
   - From OS sockets to Envoy abstractions
   - Complete class hierarchy
   - **Start here** if you're new to network programming or Envoy

2. **[Listener Manager Overview Part 1](listener_manager/OVERVIEW_PART1_architecture.md)**
   - High-level architecture
   - Listener lifecycle (warming → active → draining)
   - Worker thread model

3. **[Network Layer Overview Part 1](network/OVERVIEW_PART1_architecture_and_connections.md)**
   - Connection architecture
   - Data flow (read/write paths)
   - Filter chain basics

4. **[HTTP Layer Overview Part 1](http/OVERVIEW_PART1_request_pipeline.md)**
   - Request/response pipeline
   - Filter chain execution
   - Codec abstraction

---

## By Subsystem

### Listener Manager (`source/common/listener_manager/`)

**Core Concepts:**
- Manages listener lifecycle from configuration to active connections
- Handles dynamic listener updates via LDS
- Distributes listeners across worker threads

**Key Documents:**

| Document | Description | Start Here? |
|----------|-------------|-------------|
| [**Sockets, Connections, Listeners Explained**](listener_manager/SOCKETS_CONNECTIONS_LISTENERS_EXPLAINED.md) | Foundational concepts: OS sockets → Envoy classes | ⭐ Yes - Start here |
| [**Overview Part 1: Architecture**](listener_manager/OVERVIEW_PART1_architecture.md) | ListenerManager, worker dispatch, lifecycle | ⭐ Yes |
| [**Overview Part 2: Filter Chains**](listener_manager/OVERVIEW_PART2_filter_chains.md) | Filter chain matching, trie structure, SNI | After Part 1 |
| [**Overview Part 3: Active TCP**](listener_manager/OVERVIEW_PART3_active_tcp.md) | ActiveTcpListener, connection tracking | After Part 2 |
| [**Overview Part 4: LDS & Advanced**](listener_manager/OVERVIEW_PART4_lds_and_advanced.md) | Dynamic config, UDP, internal listeners | Advanced |
| [**Code Path Scenarios**](listener_manager/CODE_PATH_SCENARIOS.md) | Detailed walkthroughs: config, connection, drain | Reference |
| [**Listener Architecture**](listener_manager/LISTENER_ARCHITECTURE.md) | Component interactions, sequences | Visual reference |
| [**Active Stream Listener Architecture**](listener_manager/ACTIVE_STREAM_LISTENER_ARCHITECTURE.md) | Socket → connection lifecycle | Deep dive |
| [**Active TCP Listener Invocation**](listener_manager/ACTIVE_TCP_LISTENER_INVOCATION.md) | How ActiveTcpListener is called | Deep dive |

**Class-Specific Documentation:**
- `connection_handler_impl.md` - Per-worker connection handler
- `filter_chain_manager_impl.md` - Filter chain matching engine
- `listener_impl.md` - Per-listener configuration
- `listener_manager_impl.md` - Top-level listener orchestrator
- `active_tcp_listener_and_socket.md` - TCP accept handling
- `lds_api.md` - Listener Discovery Service

---

### Network Layer (`source/common/network/`)

**Core Concepts:**
- TCP/UDP connection management
- Network filter chain execution
- Transport socket abstraction (TLS, raw, QUIC)
- Platform-specific I/O (epoll, kqueue, io_uring)

**Key Documents:**

| Document | Description | Start Here? |
|----------|-------------|-------------|
| [**Overview Part 1: Architecture & Connections**](network/OVERVIEW_PART1_architecture_and_connections.md) | Connection hierarchy, data flow, backpressure | ⭐ Yes |
| [**Overview Part 2: Filters & Listeners**](network/OVERVIEW_PART2_filters_and_listeners.md) | Network filters, listener types | After Part 1 |
| [**Overview Part 3: Sockets & I/O**](network/OVERVIEW_PART3_sockets_and_io.md) | Socket options, I/O handles, platform APIs | After Part 2 |
| [**Overview Part 4: Addressing, DNS & Utilities**](network/OVERVIEW_PART4_addressing_dns_and_utilities.md) | Address resolution, DNS, CIDR ranges | Reference |
| [**Network Code Path Scenarios**](network/NETWORK_CODE_PATH_SCENARIOS.md) | Detailed walkthroughs | Reference |

**Class-Specific Documentation:**
- `connection_impl.md` - Core connection implementation
- `filter_manager_impl.md` - Network filter chain engine
- `socket_and_io_handle.md` - Socket abstraction and I/O
- `happy_eyeballs_connection_impl.md` - Dual-stack IPv4/IPv6 racing
- `listeners.md` - TCP/UDP/QUIC listener implementations
- `address_impl.md` - Address abstractions
- `transport_socket_options.md` - Transport configuration

---

### HTTP Layer (`source/common/http/`)

**Core Concepts:**
- HTTP/1.1, HTTP/2, HTTP/3 protocol support
- HTTP filter chain (auth, rate limit, routing, etc.)
- Connection pooling for upstream connections
- Request/response lifecycle management

**Key Documents:**

| Document | Description | Start Here? |
|----------|-------------|-------------|
| [**Overview Part 1: Request Pipeline**](http/OVERVIEW_PART1_request_pipeline.md) | End-to-end request flow, ConnectionManager, filters | ⭐ Yes |
| [**Overview Part 2: Codecs & Pools**](http/OVERVIEW_PART2_codecs_and_pools.md) | HTTP/1/2/3 codecs, connection pooling | After Part 1 |
| **Overview Part 3: Headers & Utilities** | Header manipulation, path utilities | Reference |
| **Overview Part 4: Advanced Topics** | Async client, matching, dependency mgmt | Advanced |

**Class-Specific Documentation:**
- `filter_manager.md` - HTTP filter chain execution engine
- `codec_client.md` - Upstream HTTP client wrapper
- `conn_pool_base_and_grid.md` - Connection pool architecture
- `filter_chain_helper.md` - Building filter chains
- `conn_manager_config.md` - HTTP Connection Manager config
- `conn_manager_utility.md` - Protocol detection, header mutation
- `header_utility.md` - Header manipulation
- `headers.md` - Header map implementation

**HTTP/1 Specific:**
- `http1/codec_impl.md` - HTTP/1.1 codec
- `http1/parser.md` - HTTP/1.1 parser
- `http1/conn_pool.md` - HTTP/1.1 connection pool
- `http1/balsa_parser.md` - Balsa parser (alternative)

---

## By Learning Path

### Path 1: Understanding Connection Flow

**Goal:** Understand how a client connection becomes an active request

1. [Sockets, Connections, Listeners Explained](listener_manager/SOCKETS_CONNECTIONS_LISTENERS_EXPLAINED.md) - Fundamentals
2. [Listener Manager Overview Part 1](listener_manager/OVERVIEW_PART1_architecture.md) - Listener lifecycle
3. [Active Stream Listener Architecture](listener_manager/ACTIVE_STREAM_LISTENER_ARCHITECTURE.md) - Socket processing
4. [Network Overview Part 1](network/OVERVIEW_PART1_architecture_and_connections.md) - Connection layer
5. [HTTP Overview Part 1](http/OVERVIEW_PART1_request_pipeline.md) - HTTP processing

### Path 2: Understanding Filter Chains

**Goal:** Learn how filters process requests and responses

1. [Listener Overview Part 2](listener_manager/OVERVIEW_PART2_filter_chains.md) - Filter chain matching
2. [Network Filter Manager](network/filter_manager_impl.md) - Network filters
3. [HTTP Filter Manager](http/filter_manager.md) - HTTP filters
4. [Filter Chain Helper](http/filter_chain_helper.md) - Building chains

### Path 3: Understanding Configuration Updates

**Goal:** Learn how Envoy updates configuration without dropping connections

1. [Listener Overview Part 4](listener_manager/OVERVIEW_PART4_lds_and_advanced.md) - LDS basics
2. [Code Path Scenarios](listener_manager/CODE_PATH_SCENARIOS.md) - Update scenarios
3. [LDS API](listener_manager/lds_api.md) - LDS implementation

### Path 4: Understanding Upstream Connections

**Goal:** Learn how Envoy connects to backends

1. [Network Overview Part 1](network/OVERVIEW_PART1_architecture_and_connections.md) - Client connections
2. [Happy Eyeballs](network/happy_eyeballs_connection_impl.md) - Dual-stack connections
3. [HTTP Overview Part 2](http/OVERVIEW_PART2_codecs_and_pools.md) - Connection pooling
4. [Codec Client](http/codec_client.md) - HTTP client wrapper

---

## By Use Case

### Debugging Connection Issues

**Start with:**
- [Active TCP Listener Invocation](listener_manager/ACTIVE_TCP_LISTENER_INVOCATION.md) - How connections are accepted
- [Connection Implementation](network/connection_impl.md) - Connection state machine
- [Network Code Path Scenarios](network/NETWORK_CODE_PATH_SCENARIOS.md) - Common scenarios

**Check:**
- Listener filters - Are they timing out?
- Filter chain matching - Is correct chain selected?
- Connection limits - Are limits being hit?

### Debugging Request Processing

**Start with:**
- [HTTP Request Pipeline](http/OVERVIEW_PART1_request_pipeline.md) - End-to-end flow
- [HTTP Filter Manager](http/filter_manager.md) - Filter execution
- [Filter Chain Helper](http/filter_chain_helper.md) - Filter ordering

**Check:**
- Filter return codes - Is a filter stopping the chain?
- Header mutations - Are headers being modified correctly?
- Routing - Is correct upstream selected?

### Adding New Filter

**Network Filter:**
1. Understand [Network Filter Manager](network/filter_manager_impl.md)
2. Study existing filter in `source/extensions/filters/network/`
3. Implement `Network::ReadFilter` or `Network::WriteFilter` interface

**HTTP Filter:**
1. Understand [HTTP Filter Manager](http/filter_manager.md)
2. Study existing filter in `source/extensions/filters/http/`
3. Implement `StreamDecoderFilter` or `StreamEncoderFilter` interface

### Performance Analysis

**Key Documents:**
- [Network Overview Part 1](network/OVERVIEW_PART1_architecture_and_connections.md) - Backpressure, watermarks
- [HTTP Connection Pool](http/conn_pool_base_and_grid.md) - Pool efficiency
- [Connection Implementation](network/connection_impl.md) - Buffer management

---

## Document Conventions

### Diagram Types

**Flowcharts:** Show process flows and decision trees
```
Used for: Request flow, state transitions, conditional logic
```

**Sequence Diagrams:** Show interactions between components over time
```
Used for: Method calls, async operations, multi-component flows
```

**Class Diagrams:** Show class hierarchies and relationships
```
Used for: Inheritance, composition, interfaces
```

**State Machines:** Show states and transitions
```
Used for: Connection lifecycle, filter states, request states
```

### Explanation Format

Each diagram is preceded by plain English explanation covering:
- **Context**: Why this diagram exists
- **Key concepts**: Important ideas to understand
- **Flow description**: Step-by-step walkthrough
- **Design rationale**: Why it works this way

---

## Contributing to Documentation

### Adding New Documentation

1. **Choose location**: Place doc near relevant source files
2. **Follow structure**: Overview → Diagrams → Detailed explanations
3. **Add plain English**: Explain WHY, not just WHAT
4. **Use mermaid diagrams**: Visual > text-only
5. **Update this index**: Add links to new documents

### Improving Existing Documentation

1. **Add missing context**: If diagram lacks explanation, add it
2. **Fix outdated info**: Code evolves, docs should too
3. **Add examples**: Real scenarios help understanding
4. **Cross-link**: Reference related documents

### Documentation Standards

- **Start with context**: Why does this exist?
- **Explain design decisions**: Why this approach vs alternatives?
- **Include examples**: Concrete beats abstract
- **Link related docs**: Help readers navigate
- **Keep diagrams simple**: One concept per diagram

---

## Quick Reference

### Common Classes

| Class | Purpose | Documentation |
|-------|---------|---------------|
| `ListenerManagerImpl` | Orchestrates listeners | [listener_manager_impl.md](listener_manager/listener_manager_impl.md) |
| `ConnectionHandlerImpl` | Per-worker connection handler | [connection_handler_impl.md](listener_manager/connection_handler_impl.md) |
| `ActiveTcpListener` | TCP connection acceptor | [active_tcp_listener_and_socket.md](listener_manager/active_tcp_listener_and_socket.md) |
| `ConnectionImpl` | Core connection implementation | [connection_impl.md](network/connection_impl.md) |
| `FilterManagerImpl` | Network filter chain engine | [filter_manager_impl.md](network/filter_manager_impl.md) |
| `ConnectionManagerImpl` | HTTP connection manager | Various in http/ |
| `FilterManager` | HTTP filter chain engine | [filter_manager.md](http/filter_manager.md) |
| `CodecClient` | Upstream HTTP client | [codec_client.md](http/codec_client.md) |

### Common Patterns

| Pattern | Where Used | Description |
|---------|------------|-------------|
| **Factory Pattern** | Filters, connections, codecs | Create objects via factories for flexibility |
| **Template Method** | Connection, filter bases | Define skeleton, subclasses implement specifics |
| **Chain of Responsibility** | Filter chains | Pass request through chain of handlers |
| **Deferred Deletion** | Connections, streams | Delete objects at safe points (end of event loop) |
| **Event-Driven** | All I/O | Async I/O via event dispatcher (epoll/kqueue) |

---

## Glossary

**Accept Queue**: Kernel buffer of completed TCP connections waiting for `accept()`

**Active Connection**: Established connection with filters and data flowing

**Active Socket**: Newly accepted socket undergoing listener filter processing

**Codec**: Protocol encoder/decoder (HTTP/1, HTTP/2, HTTP/3)

**Deferred Deletion**: Objects scheduled for deletion at end of event loop iteration

**Downstream**: Client → Envoy direction

**Filter Chain**: Ordered list of filters processing requests/responses

**Happy Eyeballs**: RFC 8305 - Racing IPv4 and IPv6 connections

**Listener Filter**: Pre-connection filter (TLS Inspector, Proxy Protocol)

**Network Filter**: L4 filter seeing raw bytes (TCP Proxy, HTTP Connection Manager)

**HTTP Filter**: L7 filter seeing parsed HTTP (Router, Auth, Rate Limit)

**Server Connection**: Downstream connection (accepted)

**Client Connection**: Upstream connection (initiated)

**Transport Socket**: Encryption layer (TLS, raw, QUIC)

**Upstream**: Envoy → Backend direction

**Watermark**: Buffer threshold triggering backpressure

---

## Getting Help

**Found an issue?**
- Check if it's already documented
- Look for related GitHub issues
- File new issue with clear reproduction

**Need clarification?**
- Read related documents (see cross-links)
- Check source code comments
- Ask in Envoy Slack #envoy-dev

**Want to contribute?**
- Improve existing docs
- Add missing explanations
- Create examples
- Fix errors

---

*Last Updated: 2026-03-29*
