# Envoy Architecture Flows - UML Diagram Series

## Overview

This directory contains comprehensive UML diagrams documenting the most important data flows across all layers of Envoy. Each document provides detailed sequence diagrams, state machines, class diagrams, and flowcharts to help understand how Envoy processes requests, manages configuration, and handles various operational scenarios.

## Documentation Structure

```mermaid
graph TD
    A[Connection Layer] --> B[HTTP Layer]
    B --> C[Routing Layer]
    C --> D[Upstream Layer]

    E[Configuration] -.->|Updates| A
    E -.->|Updates| B
    E -.->|Updates| C
    E -.->|Updates| D

    F[Observability] -.->|Monitors| A
    F -.->|Monitors| B
    F -.->|Monitors| C
    F -.->|Monitors| D

    style A fill:#ff9999
    style B fill:#99ccff
    style C fill:#99ff99
    style D fill:#ffff99
    style E fill:#ff99ff
    style F fill:#ffcc99
```

## Document Index

### Core Request Processing

#### 1. [Connection Lifecycle](01_connection_lifecycle.md)
**From TCP accept to connection close**

Topics covered:
- TCP connection acceptance and setup
- Listener filter processing (TLS Inspector, Proxy Protocol)
- Filter chain matching and selection
- Connection state machine
- Event loop integration (epoll/kqueue)
- Buffer management and watermarks
- Connection pooling
- Graceful vs immediate close
- Connection statistics

**Key Diagrams:**
- High-level connection flow sequence
- Connection state machine
- Buffer watermark flow
- TLS handshake integration
- Connection close sequence

**When to read:** Understanding how Envoy handles network connections at the TCP level.

#### 2. [HTTP Request/Response Flow](02_http_request_flow.md)
**Complete HTTP request processing pipeline**

Topics covered:
- HTTP Connection Manager architecture
- HTTP/1.1, HTTP/2, HTTP/3 codec differences
- Decoder filter chain processing
- Encoder filter chain processing
- Request routing decision
- Upstream request creation
- Header processing pipeline
- Body streaming vs buffering
- Trailer handling
- Stream reset and WebSocket handling

**Key Diagrams:**
- Complete HTTP request flow
- Filter chain state machine
- HTTP/2 stream multiplexing
- Request routing decision tree
- Response processing flow

**When to read:** Understanding HTTP layer processing and filter execution.

### Cluster and Load Balancing

#### 3. [Cluster Management and Load Balancing](03_cluster_load_balancing.md)
**Host selection and traffic distribution**

Topics covered:
- Cluster Manager architecture
- Cluster initialization (Static, DNS, EDS)
- Load balancing algorithms:
  - Round Robin
  - Least Request
  - Ring Hash
  - Maglev
  - Random
  - Weighted
- Priority and locality-aware routing
- Zone-aware load balancing
- Host health status management
- Active health checking
- Outlier detection (passive health checking)
- Panic threshold handling
- Subset load balancing
- Connection pools per host

**Key Diagrams:**
- Load balancing decision flow
- Round Robin vs Least Request
- Ring Hash algorithm
- Maglev consistent hashing
- Host health transitions
- Outlier detection flow
- Zone-aware routing

**When to read:** Understanding how Envoy selects upstream hosts and distributes traffic.

### Dynamic Configuration

#### 4. [xDS Configuration Flow](04_xds_configuration_flow.md)
**Dynamic configuration updates via xDS protocol**

Topics covered:
- xDS protocol overview (LDS, RDS, CDS, EDS, SDS)
- State-of-the-World (SotW) protocol
- Incremental/Delta xDS protocol
- Configuration dependency graph
- ACK/NACK mechanism
- Listener Discovery Service (LDS)
- Route Discovery Service (RDS)
- Cluster Discovery Service (CDS)
- Endpoint Discovery Service (EDS)
- Aggregated Discovery Service (ADS)
- Cluster warming process
- Resource version management
- Error handling and recovery

**Key Diagrams:**
- xDS protocol sequence
- Delta xDS flow
- Configuration dependency graph
- Cluster warming state machine
- NACK handling
- ADS flow

**When to read:** Understanding how Envoy receives and applies dynamic configuration updates.

## Flow Categories

### 📥 Inbound Processing
```mermaid
flowchart LR
    A[Client Connection] --> B[Connection Lifecycle]
    B --> C[HTTP Request Flow]
    C --> D[Filter Chain Execution]
    D --> E[Routing Decision]
```

Documents: 01, 02, 05

### 🔄 Traffic Management
```mermaid
flowchart LR
    A[Routing Decision] --> B[Cluster Selection]
    B --> C[Load Balancing]
    C --> D[Upstream Connection]
    D --> E[Retry/Circuit Breaking]
```

Documents: 03, 06, 07

### ⚙️ Configuration & Control
```mermaid
flowchart LR
    A[Control Plane] --> B[xDS Updates]
    B --> C[Configuration Validation]
    C --> D[Resource Warming]
    D --> E[Active Configuration]
```

Documents: 04

### 🏥 Health & Reliability
```mermaid
flowchart LR
    A[Health Checking] --> B[Outlier Detection]
    B --> C[Circuit Breaking]
    C --> D[Retry Logic]
    D --> E[Failover]
```

Documents: 07, 08

### 📊 Observability
```mermaid
flowchart LR
    A[Request Metrics] --> B[Stats Aggregation]
    B --> C[Access Logging]
    C --> D[Tracing]
    D --> E[Health Endpoints]
```

Documents: 09

## Complete Request Journey

Here's how a typical request flows through Envoy across all layers:

```mermaid
sequenceDiagram
    participant Client
    participant Conn as Connection<br/>(Doc 01)
    participant HTTP as HTTP Layer<br/>(Doc 02)
    participant Router as Routing<br/>(Doc 03)
    participant LB as Load Balancer<br/>(Doc 03)
    participant Pool as Connection Pool<br/>(Doc 06)
    participant Upstream
    participant xDS as xDS Updates<br/>(Doc 04)

    Note over xDS: Dynamic config loaded

    Client->>Conn: TCP SYN
    Conn->>Conn: Accept connection
    Conn->>HTTP: Create HTTP codec

    Client->>HTTP: HTTP Request
    HTTP->>HTTP: Decode headers
    HTTP->>HTTP: Run decoder filters

    HTTP->>Router: Route request
    Router->>LB: Select host
    LB->>LB: Load balance algorithm
    LB-->>Router: Selected host

    Router->>Pool: Get connection
    alt Connection available
        Pool-->>Router: Reuse connection
    else No connection
        Pool->>Upstream: Create new connection
        Pool-->>Router: New connection
    end

    Router->>Upstream: Send request
    Upstream-->>Router: Response

    Router->>HTTP: Encode response
    HTTP->>HTTP: Run encoder filters
    HTTP->>Conn: Write to socket
    Conn->>Client: HTTP Response

    par Configuration updates
        xDS->>Router: Route update
        xDS->>LB: Cluster update
        xDS->>Pool: Endpoint update
    end
```

## Reading Guide

### For Beginners
Start with these documents in order:
1. **Connection Lifecycle** - Understand basic networking
2. **HTTP Request Flow** - Learn HTTP processing
3. **Cluster Management** - Understand upstream selection

### For Operators
Focus on these for troubleshooting:
1. **xDS Configuration Flow** - Debug config issues
2. **Cluster Management** - Understand load balancing
3. **Health Checking** - Debug health check failures

### For Developers
Deep dive into these for extending Envoy:
1. **Filter Chain Execution** - Build custom filters
2. **HTTP Request Flow** - Understand filter lifecycle
3. **Connection Lifecycle** - Network layer details

### For Architects
System design perspective:
1. **xDS Configuration Flow** - Control plane integration
2. **Cluster Management** - Traffic distribution strategy
3. **Retry and Circuit Breaking** - Reliability patterns

## Common Patterns Across Flows

### State Machines
Most Envoy components use state machines:
- Connections: Init → Active → Closing → Closed
- Streams: Created → Processing → Complete
- Clusters: Initializing → Warming → Active
- xDS: Requested → Validating → Applied

### Event-Driven Architecture
All I/O is event-driven:
- **Read events**: Data available
- **Write events**: Buffer space available
- **Timer events**: Timeouts and periodic tasks
- **File events**: Configuration file changes

### Asynchronous Processing
Filters can pause and resume:
- `Continue`: Process immediately
- `StopIteration`: Pause until `continueDecoding()`
- `StopIterationAndBuffer`: Pause and buffer data
- `StopIterationAndWatermark`: Pause with flow control

### Statistics Collection
Every component emits stats:
- **Counters**: Monotonically increasing (requests, errors)
- **Gauges**: Current value (active connections)
- **Histograms**: Distribution (latency)

## Diagram Types Used

### Sequence Diagrams
Show interactions between components over time.
```mermaid
sequenceDiagram
    participant A
    participant B
    A->>B: Request
    B-->>A: Response
```

### State Machines
Show state transitions and conditions.
```mermaid
stateDiagram-v2
    [*] --> State1
    State1 --> State2: Event
    State2 --> [*]
```

### Flowcharts
Show decision logic and process flow.
```mermaid
flowchart TD
    A[Start] --> B{Decision?}
    B -->|Yes| C[Action]
    B -->|No| D[Other]
```

### Class Diagrams
Show component relationships.
```mermaid
classDiagram
    class A {
        +method()
    }
    class B {
        +method()
    }
    A --> B
```

## Performance Insights

### Zero-Copy Optimization
```mermaid
flowchart LR
    A[Socket] -->|Buffer Slice| B[Filter 1]
    B -->|Buffer Slice| C[Filter 2]
    C -->|Buffer Slice| D[Upstream]

    Note[No data copying<br/>Only reference passing]
```

### Connection Pooling
```mermaid
graph TD
    A[Request 1] -->|Reuse| B[Connection Pool]
    C[Request 2] -->|Reuse| B
    D[Request 3] -->|Reuse| B
    B --> E[Upstream Host]

    F[Reduces latency<br/>Saves resources]
```

### Filter Short-Circuiting
```mermaid
flowchart LR
    A[Request] --> B[Filter 1]
    B --> C{Continue?}
    C -->|No| D[Send Response]
    C -->|Yes| E[Filter 2]
    E --> F[Filter 3]

    style D fill:#ff6b6b
```

## Troubleshooting with Flows

### Connection Issues
→ See [Connection Lifecycle](01_connection_lifecycle.md)
- Connection refused
- Connection timeouts
- Connection resets

### Routing Issues
→ See [HTTP Request Flow](02_http_request_flow.md) & [Cluster Management](03_cluster_load_balancing.md)
- 404 Not Found
- No healthy upstream
- Wrong cluster selected

### Configuration Issues
→ See [xDS Configuration Flow](04_xds_configuration_flow.md)
- NACK errors
- Stale configuration
- Missing dependencies

### Load Balancing Issues
→ See [Cluster Management](03_cluster_load_balancing.md)
- Uneven distribution
- Host selection problems
- Health check failures

## Additional Resources

### Envoy Documentation
- [Official Architecture Overview](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/arch_overview)
- [API Reference](https://www.envoyproxy.io/docs/envoy/latest/api-v3/api)

### Related Documentation in This Repo
- [HTTP Filters](../http_filters/) - Filter-specific flows
- [Security](../security/) - TLS and authorization flows
- [Configuration Examples](../examples/) - Working configurations

### External Resources
- [Envoy GitHub](https://github.com/envoyproxy/envoy)
- [Envoy Slack](https://envoyproxy.slack.com)
- [xDS Protocol](https://github.com/envoyproxy/data-plane-api)

## Contributing

To add new flow documentation:

1. **Choose a specific flow** - Focus on one aspect
2. **Create comprehensive diagrams** - Multiple diagram types
3. **Follow the template**:
   - Overview
   - Architecture diagrams
   - Detailed sequence flows
   - State machines
   - Configuration examples
   - Key takeaways
   - Related flows
4. **Use consistent styling** - Match existing documents
5. **Add to this README** - Update the index

## Document Status

| Document | Status | Last Updated | Completeness |
|----------|--------|--------------|--------------|
| 01 - Connection Lifecycle | ✅ Complete | 2026-02-28 | 95% |
| 02 - HTTP Request Flow | ✅ Complete | 2026-02-28 | 95% |
| 03 - Cluster & Load Balancing | ✅ Complete | 2026-02-28 | 95% |
| 04 - xDS Configuration | ✅ Complete | 2026-02-28 | 95% |
| 05 - Filter Chain Execution | 🔄 Planned | - | 0% |
| 06 - Upstream Connection Mgmt | 🔄 Planned | - | 0% |
| 07 - Retry & Circuit Breaking | 🔄 Planned | - | 0% |
| 08 - Health Checking | 🔄 Planned | - | 0% |
| 09 - Stats & Observability | 🔄 Planned | - | 0% |

---

*Last Updated: 2026-02-28*
*Envoy Version: Latest (4.x)*
*Mermaid Version: 10.x*
