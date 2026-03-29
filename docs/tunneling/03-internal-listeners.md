# Part 3: Internal Listeners & Reverse Connections

## Overview

Internal listeners enable **in-process connections** within a single Envoy instance. Instead of using real OS sockets and network I/O, internal listeners use user-space `IoHandle` pairs to pass data between two filter chains. This is the foundation for advanced features like chaining listeners, applying different filter policies, and passing metadata between processing stages.

## Architecture

```mermaid
graph TB
    subgraph "Downstream Connection"
        Client["Client"]
        L1["Listener A\n(port 8443, TLS)"]
        FC1["Filter Chain 1\nNetwork Filters → HCM"]
    end
    
    subgraph "Internal Connection (no real I/O)"
        IL["Internal Listener\n(envoy_internal_listener_B)"]
        PAIR["IoHandle Pair\n(user-space buffers)"]
    end
    
    subgraph "Second Processing Stage"
        FC2["Filter Chain 2\nDifferent filters, policies"]
    end
    
    Client -->|"TCP"| L1
    L1 --> FC1
    FC1 -->|"envoy_internal address"| PAIR
    PAIR --> IL
    IL --> FC2
```

## Key Classes

```mermaid
classDiagram
    class InternalClientConnectionFactory {
        +createClientConnection(dispatcher, address, source, transport_socket, options)
        -buffer_size_ : uint32_t
    }
    class TlsInternalListenerRegistry {
        +createActiveInternalListener(handler, config)
    }
    class ActiveInternalListener {
        +onAccept(socket)
        +newConnection(socket, stream_info)
        +newActiveConnection(chain, conn, info)
    }
    class ThreadLocalRegistryImpl {
        +setInternalListenerManager(manager)
        +findByAddress(address) InternalListener*
    }
    class EnvoyInternalInstance {
        -address_id_ : string
        +addressId() string
        +envoyInternalAddress() EnvoyInternalAddress*
    }
    class InternalSocket {
        -inner_socket_ : TransportSocketPtr
        -metadata_ : StructMap
        -filter_state_objects_ : vector
        +doRead(buffer) IoResult
        +doWrite(buffer, end_stream) IoResult
        +onConnected()
    }

    InternalClientConnectionFactory --> EnvoyInternalInstance : "creates connections to"
    ActiveInternalListener --> ThreadLocalRegistryImpl : "registered in"
    InternalSocket --> InternalSocket : "metadata passthrough"
```

## Connection Flow — Step by Step

```mermaid
sequenceDiagram
    participant Router as Router / TCP Proxy
    participant ICCF as InternalClientConnectionFactory
    participant IOPair as IoHandle Pair (user-space)
    participant Registry as ThreadLocalRegistryImpl
    participant AIL as ActiveInternalListener
    participant FC2 as Filter Chain 2

    Router->>Router: Upstream cluster has envoy_internal address
    Router->>ICCF: createClientConnection(dispatcher, envoy_internal_addr)
    
    ICCF->>IOPair: createBufferLimitedIoHandlePair(buffer_size)
    Note over IOPair: Creates two connected IoHandles\nClient side + Server side
    
    ICCF->>ICCF: Create ClientConnectionImpl with io_handle_client
    ICCF->>Registry: findByAddress(address_id)
    Registry-->>ICCF: InternalListener reference
    
    ICCF->>ICCF: Create AcceptedSocketImpl with io_handle_server
    ICCF->>AIL: onAccept(server_socket)
    
    AIL->>AIL: Create listener filter chain
    AIL->>AIL: Run listener filters
    AIL->>AIL: findFilterChain(socket)
    AIL->>AIL: createServerConnection(socket)
    AIL->>FC2: createNetworkFilterChain(connection)
    
    Note over Router,FC2: Both sides connected via IoHandle pair
    
    Router->>IOPair: Write data (client side)
    IOPair->>FC2: Data appears (server side)
    
    FC2->>IOPair: Write response (server side)
    IOPair->>Router: Data appears (client side)
```

## IoHandle Pair — User-Space I/O

```mermaid
graph LR
    subgraph "IoHandle Pair"
        subgraph "Client Side"
            CW["Write Buffer →"]
            CR["← Read Buffer"]
        end
        
        subgraph "Shared Buffers"
            B1["Buffer A\n(client→server)"]
            B2["Buffer B\n(server→client)"]
        end
        
        subgraph "Server Side"
            SW["Write Buffer →"]
            SR["← Read Buffer"]
        end
        
        CW --> B1
        B1 --> SR
        SW --> B2
        B2 --> CR
    end
    
    Note["No kernel involvement\nNo system calls\nDirect memory transfer"]
```

The IoHandle pair is created by `IoHandleFactory::createBufferLimitedIoHandlePair()`. Each side reads from one buffer and writes to the other, with configurable buffer limits for flow control.

## Internal Upstream Transport Socket

The `internal_upstream` transport socket passes metadata and filter state from the downstream connection to the internal listener:

```mermaid
sequenceDiagram
    participant DS as Downstream Connection
    participant ITS as InternalSocket (transport)
    participant IOPair as IoHandle (UserSpace)
    participant AIL as ActiveInternalListener
    participant US as Upstream Connection

    Note over DS: Downstream request has metadata,\nfilter state, etc.
    
    DS->>ITS: setTransportSocketCallbacks()
    ITS->>ITS: Extract metadata from host/cluster
    ITS->>IOPair: passthroughState()->initialize(metadata, filter_state)
    
    Note over IOPair: Metadata stored in IoHandle
    
    AIL->>AIL: newActiveConnection()
    AIL->>IOPair: passthroughState()->mergeInto(conn_metadata, conn_filter_state)
    
    Note over US: Internal connection now has\ndownstream's metadata and filter state
```

### What Gets Passed Through

```mermaid
graph TD
    subgraph "Downstream Context"
        DM["Dynamic Metadata\n(e.g., auth decisions)"]
        FS["Filter State\n(e.g., per-request data)"]
        CM["Cluster/Host Metadata"]
    end
    
    subgraph "InternalSocket::initialize()"
        Extract["Extract configured\nmetadata namespaces"]
        Serialize["Store in IoHandle\npassthroughState"]
    end
    
    subgraph "ActiveInternalListener::mergeInto()"
        Merge["Merge metadata into\nnew connection's StreamInfo"]
    end
    
    DM --> Extract
    FS --> Extract
    CM --> Extract
    Extract --> Serialize
    Serialize --> Merge
```

## Use Cases

### 1. Multi-Stage Processing

```mermaid
graph LR
    subgraph "Stage 1: External Listener"
        L1["Listener :443\nTLS termination"]
        HCM1["HCM\nAuth, Rate Limit"]
    end
    
    subgraph "Internal"
        IL["Internal Listener\n(no TLS overhead)"]
    end
    
    subgraph "Stage 2: Internal Processing"
        HCM2["HCM\nRouting, Lua scripting"]
        Router["Router\n→ real upstream"]
    end
    
    L1 --> HCM1 --> IL --> HCM2 --> Router
```

### 2. Protocol Bridging

```mermaid
graph LR
    subgraph "Downstream: HTTP/1.1"
        H1["HTTP/1.1 Client"]
        L1["Listener\nHTTP/1.1"]
    end
    
    subgraph "Bridge"
        TCPProxy["TCP Proxy\ntunneling_config"]
        IL["Internal Listener"]
    end
    
    subgraph "Upstream: HTTP/2"
        H2Router["HCM\nHTTP/2 routing"]
    end
    
    H1 --> L1 --> TCPProxy --> IL --> H2Router
```

### 3. Policy Separation

```mermaid
graph TD
    subgraph "External Traffic"
        ExtL["External Listener :8443\nStrict mTLS, RBAC"]
    end
    
    subgraph "Internal Traffic"
        IntL["Internal Listener\nRelaxed policy, telemetry only"]
    end
    
    subgraph "Shared Upstream"
        Router["Router → Backend"]
    end
    
    ExtL -->|"envoy_internal"| IntL
    IntL --> Router
```

## Configuration Example

```mermaid
graph TD
    subgraph "Bootstrap Config"
        IE["internal_listener\nextension in bootstrap"]
    end
    
    subgraph "Listener Config"
        L["listener:\n  name: internal_listener_1\n  internal_listener: {}\n  address:\n    envoy_internal_address:\n      server_listener_name: internal_1"]
    end
    
    subgraph "Cluster Config"
        C["cluster:\n  load_assignment:\n    endpoints:\n      - address:\n          envoy_internal_address:\n            server_listener_name: internal_1"]
    end
    
    IE --> L
    C -->|"routes to"| L
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `source/extensions/bootstrap/internal_listener/internal_listener_registry.cc` | `TlsInternalListenerRegistry`, `InternalListenerExtension` | Bootstrap extension |
| `source/extensions/bootstrap/internal_listener/thread_local_registry.cc` | `ThreadLocalRegistryImpl` | Per-worker listener registry |
| `source/extensions/bootstrap/internal_listener/active_internal_listener.cc` | `ActiveInternalListener` | Internal listener accepting connections |
| `source/extensions/bootstrap/internal_listener/client_connection_factory.cc` | `InternalClientConnectionFactory` | Creates internal connections |
| `source/extensions/transport_sockets/internal_upstream/internal_upstream.cc` | `InternalSocket`, `InternalSocketFactory` | Metadata passthrough transport socket |
| `source/extensions/io_socket/user_space/io_handle_impl.h` | User-space `IoHandle` | Buffer-based I/O handle pair |
| `source/common/network/address_impl.h:417` | `EnvoyInternalInstance` | Internal address type |
| `source/common/listener_manager/connection_handler_impl.cc:242` | `findByAddress()` | Internal listener lookup |

---

**Previous:** [Part 2 — TCP-over-HTTP Tunneling](02-tcp-over-http-tunneling.md)  
**Back to:** [Part 1 — Overview](01-overview-http-connect.md)
