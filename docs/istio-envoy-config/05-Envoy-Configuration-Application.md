# Part 5: Envoy Configuration Application

## Table of Contents
1. [Introduction](#introduction)
2. [Envoy xDS Client Architecture](#envoy-xds-client-architecture)
3. [Configuration Reception](#configuration-reception)
4. [Configuration Validation](#configuration-validation)
5. [Warming and Initialization](#warming-and-initialization)
6. [Configuration Activation](#configuration-activation)
7. [Draining Old Configuration](#draining-old-configuration)
8. [Error Handling and Recovery](#error-handling-and-recovery)

## Introduction

Once Envoy receives xDS configurations from Istiod, it must validate, prepare, and activate them while maintaining zero-downtime operation. This document explains how Envoy processes and applies dynamic configurations.

## Envoy xDS Client Architecture

### xDS Subscription Manager

```mermaid
classDiagram
    class SubscriptionManager {
        +Config::SubscriptionFactory& factory
        +map~string, Subscription~ subscriptions
        +subscribe(type_url, callbacks) Subscription
        +unsubscribe(type_url)
    }

    class GrpcMux {
        +Grpc::AsyncClient& client
        +map~string, Watch~ watches
        +LocalInfo::LocalInfo& local_info
        +start()
        +sendDiscoveryRequest(type_url)
        +onDiscoveryResponse(response)
        +pause(type_url)
        +resume(type_url)
    }

    class GrpcSubscription {
        +GrpcMux& grpc_mux
        +string type_url
        +SubscriptionCallbacks& callbacks
        +start(resources)
        +updateResourceInterest(resources)
    }

    class SubscriptionCallbacks {
        <<interface>>
        +onConfigUpdate(resources, version)
        +onConfigUpdate(added, modified, removed, version)
        +onConfigUpdateFailed(reason, exception)
    }

    class LdsApiImpl {
        +ClusterManager& cm
        +Init::Manager& init_manager
        +onConfigUpdate(resources, version)
        +onConfigUpdateFailed(reason, exception)
    }

    SubscriptionManager --> GrpcSubscription: creates
    GrpcSubscription --> GrpcMux: uses
    GrpcSubscription --> SubscriptionCallbacks: notifies
    LdsApiImpl ..|> SubscriptionCallbacks: implements

    note for GrpcMux "Manages single gRPC stream\nfor ADS or multiple streams\nfor separate xDS types"

    note for SubscriptionCallbacks "Different implementations:\n- LdsApiImpl\n- RdsRouteConfigSubscription\n- CdsApiImpl\n- ClusterManager (EDS)"
```

### Configuration Flow in Envoy

```mermaid
graph TB
    subgraph "Envoy Internal Flow"
        RECEIVE[xDS Response Received]

        RECEIVE --> DECODE[Decode Protobuf]
        DECODE --> DISPATCH[Dispatch to Handler]

        DISPATCH --> HANDLER{Resource Type?}

        HANDLER -->|LDS| LDS_HANDLER[LDS Handler]
        HANDLER -->|RDS| RDS_HANDLER[RDS Handler]
        HANDLER -->|CDS| CDS_HANDLER[CDS Handler]
        HANDLER -->|EDS| EDS_HANDLER[EDS Handler]
        HANDLER -->|SDS| SDS_HANDLER[SDS Handler]

        LDS_HANDLER --> LDS_VALIDATE[Validate Listeners]
        RDS_HANDLER --> RDS_VALIDATE[Validate Routes]
        CDS_HANDLER --> CDS_VALIDATE[Validate Clusters]
        EDS_HANDLER --> EDS_VALIDATE[Validate Endpoints]
        SDS_HANDLER --> SDS_VALIDATE[Validate Secrets]

        LDS_VALIDATE --> LDS_APPLY[Apply Listeners]
        RDS_VALIDATE --> RDS_APPLY[Apply Routes]
        CDS_VALIDATE --> CDS_APPLY[Apply Clusters]
        EDS_VALIDATE --> EDS_APPLY[Apply Endpoints]
        SDS_VALIDATE --> SDS_APPLY[Apply Secrets]

        LDS_APPLY --> ACK[Send ACK]
        RDS_APPLY --> ACK
        CDS_APPLY --> ACK
        EDS_APPLY --> ACK
        SDS_APPLY --> ACK

        LDS_VALIDATE --> NACK[Send NACK]
        RDS_VALIDATE --> NACK
        CDS_VALIDATE --> NACK
        EDS_VALIDATE --> NACK
        SDS_VALIDATE --> NACK
    end

    style RECEIVE fill:#3498DB,color:#fff
    style ACK fill:#27AE60,color:#fff
    style NACK fill:#E74C3C,color:#fff
```

## Configuration Reception

### gRPC Stream Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Connecting: Bootstrap loaded

    Connecting --> Authenticating: TCP connected

    state Authenticating {
        [*] --> TLS_Handshake
        TLS_Handshake --> mTLS_Verify
        mTLS_Verify --> [*]
    }

    Authenticating --> Streaming: Auth success
    Authenticating --> Failed: Auth failed

    state Streaming {
        [*] --> Subscribing
        Subscribing --> Receiving: Subscription sent
        Receiving --> Processing: Response received
        Processing --> Receiving: ACK sent
        Processing --> Receiving: NACK sent
    }

    Streaming --> Reconnecting: Connection lost
    Reconnecting --> Connecting: Backoff expired

    Failed --> Reconnecting: Retry

    note right of Streaming
        Active bidirectional stream
        Send: DiscoveryRequest
        Receive: DiscoveryResponse
    end note

    note right of Reconnecting
        Exponential backoff:
        1s → 2s → 4s → 8s → max 30s
        Keep existing config active
    end note
```

### Message Reception and Parsing

```mermaid
sequenceDiagram
    participant G as gRPC Stream
    participant P as Proto Parser
    participant V as Version Tracker
    participant H as Resource Handler
    participant C as Config Store

    G->>P: DiscoveryResponse received

    Note over P: Deserialize protobuf<br/>to DiscoveryResponse

    P->>V: Extract version_info & nonce

    V->>V: Check if version is new

    alt New Version
        P->>H: resources: [Any, Any, ...]<br/>type_url: type.googleapis...

        Note over H: Unpack each Any message<br/>to specific resource type

        loop For each resource
            H->>H: Deserialize to:<br/>Listener / RouteConfig /<br/>Cluster / Endpoint
        end

        H->>C: Validate resources

        alt Validation Success
            C->>H: OK
            H->>V: Mark version accepted
            V->>G: Send ACK
        else Validation Failed
            C->>H: Error details
            H->>V: Keep old version
            V->>G: Send NACK with error
        end

    else Same Version (Re-push)
        V->>G: Send ACK<br/>(already applied)
    end
```

## Configuration Validation

### Multi-Level Validation

```mermaid
graph TB
    START[Received xDS Resources]

    START --> L1[Level 1: Protobuf Validation]
    L1 --> L1_CHECK{Valid?}
    L1_CHECK -->|No| L1_ERR[Error: Malformed protobuf]
    L1_CHECK -->|Yes| L2

    L2[Level 2: Schema Validation]
    L2 --> L2_CHECK{Valid?}
    L2_CHECK -->|No| L2_ERR[Error: Missing required fields]
    L2_CHECK -->|Yes| L3

    L3[Level 3: Semantic Validation]
    L3 --> L3_CHECKS[Check:]
    L3_CHECKS --> L3_1[Valid regex patterns?]
    L3_CHECKS --> L3_2[Valid IP addresses?]
    L3_CHECKS --> L3_3[Valid port numbers?]
    L3_CHECKS --> L3_4[Valid weights?]

    L3_1 --> L3_CHECK{All valid?}
    L3_2 --> L3_CHECK
    L3_3 --> L3_CHECK
    L3_4 --> L3_CHECK

    L3_CHECK -->|No| L3_ERR[Error: Invalid values]
    L3_CHECK -->|Yes| L4

    L4[Level 4: Dependency Validation]
    L4 --> L4_CHECKS[Check:]
    L4_CHECKS --> L4_1[Referenced clusters exist?]
    L4_CHECKS --> L4_2[Referenced routes exist?]
    L4_CHECKS --> L4_3[TLS contexts valid?]

    L4_1 --> L4_CHECK{Dependencies OK?}
    L4_2 --> L4_CHECK
    L4_3 --> L4_CHECK

    L4_CHECK -->|No| L4_ERR[Error: Missing dependency]
    L4_CHECK -->|Yes| L5

    L5[Level 5: Runtime Validation]
    L5 --> L5_CHECKS[Check:]
    L5_CHECKS --> L5_1[Can bind ports?]
    L5_CHECKS --> L5_2[Sufficient resources?]
    L5_CHECKS --> L5_3[No conflicts?]

    L5_1 --> L5_CHECK{Runtime OK?}
    L5_2 --> L5_CHECK
    L5_3 --> L5_CHECK

    L5_CHECK -->|No| L5_ERR[Error: Runtime constraint]
    L5_CHECK -->|Yes| ACCEPT[Accept Configuration]

    L1_ERR --> NACK[Send NACK]
    L2_ERR --> NACK
    L3_ERR --> NACK
    L4_ERR --> NACK
    L5_ERR --> NACK

    ACCEPT --> WARM[Proceed to Warming]

    style ACCEPT fill:#27AE60,color:#fff
    style NACK fill:#E74C3C,color:#fff
    style WARM fill:#3498DB,color:#fff
```

### Listener Validation Example

```mermaid
graph TB
    LISTENER[Listener Resource]

    LISTENER --> CHECK_ADDR[Validate Address]
    CHECK_ADDR --> ADDR_VALID{Valid IP:Port?}
    ADDR_VALID -->|No| ERR1[Error: Invalid address]
    ADDR_VALID -->|Yes| CHECK_FC

    CHECK_FC[Validate Filter Chains]
    CHECK_FC --> FC_VALID{Valid filters?}
    FC_VALID -->|No| ERR2[Error: Invalid filter config]
    FC_VALID -->|Yes| CHECK_RDS

    CHECK_RDS[Check RDS Reference]
    CHECK_RDS --> RDS_EXISTS{Route exists?}
    RDS_EXISTS -->|No| WARN[Warning: RDS not yet received<br/>Will wait for RDS]
    RDS_EXISTS -->|Yes| CHECK_TLS

    CHECK_TLS[Validate TLS Config]
    CHECK_TLS --> TLS_VALID{Valid certs?}
    TLS_VALID -->|No| ERR3[Error: Invalid TLS]
    TLS_VALID -->|Yes| CHECK_CONFLICT

    CHECK_CONFLICT[Check Port Conflicts]
    CHECK_CONFLICT --> CONFLICT{Port in use?}
    CONFLICT -->|Yes| ERR4[Error: Port conflict]
    CONFLICT -->|No| VALID

    VALID[Listener Valid]

    ERR1 --> REJECT[Reject Listener]
    ERR2 --> REJECT
    ERR3 --> REJECT
    ERR4 --> REJECT
    WARN --> DEFER[Defer Activation]

    VALID --> ACCEPT[Accept Listener]

    style VALID fill:#27AE60,color:#fff
    style REJECT fill:#E74C3C,color:#fff
    style WARN fill:#F39C12,color:#fff
```

## Warming and Initialization

### Cluster Warming Process

```mermaid
sequenceDiagram
    participant CM as Cluster Manager
    participant C as New Cluster
    participant E as Endpoints (EDS)
    participant HC as Health Checker
    participant T as Timeout Timer

    Note over CM: New CDS received

    CM->>C: Create cluster (warming state)

    C->>E: Request endpoints (EDS)

    alt EDS Already Cached
        E-->>C: Endpoints ready
    else EDS Not Ready
        E->>E: Wait for EDS update
        Note over E: May take 0-5 seconds
        E-->>C: Endpoints received
    end

    C->>HC: Initialize health checking

    par Health Check First Batch
        HC->>HC: Check endpoint 1
        HC->>HC: Check endpoint 2
        HC->>HC: Check endpoint N
    end

    alt All Endpoints Healthy
        HC-->>C: Healthy endpoints available
        C->>CM: Cluster warmed
    else Timeout (10s default)
        T->>C: Warming timeout
        C->>CM: Cluster warmed<br/>(with available endpoints)
    else No Healthy Endpoints
        HC-->>C: No healthy endpoints
        C->>CM: Cluster warmed<br/>(empty endpoint set)
    end

    CM->>CM: Activate cluster

    Note over CM: Cluster ready for traffic
```

### Warming State Machine

```mermaid
stateDiagram-v2
    [*] --> Created: Cluster created

    state Created {
        [*] --> Initializing
        Initializing --> WaitingEDS: EDS subscription
        WaitingEDS --> WaitingHealth: Endpoints received
        WaitingHealth --> CheckingHealth: Health check started
    }

    Created --> Warmed: Warming complete

    state Warmed {
        [*] --> Ready
        Ready --> Active: Activated
    }

    Warmed --> [*]: Cluster ready

    note right of Created
        Warming phase:
        - Request endpoints
        - Initialize health checks
        - Wait for healthy endpoints
        Timeout: 10 seconds
    end note

    note right of Warmed
        Cluster ready to serve traffic
        Old cluster (if any) still active
        Switch happens atomically
    end note
```

### Initialization Manager

```mermaid
classDiagram
    class InitManager {
        +name string
        +state State
        +targets []Target
        +watcher Watcher
        +add(target Target)
        +initialize(watcher Watcher)
        +ready()
    }

    class Target {
        <<interface>>
        +name() string
        +initialize(watcher Watcher)
        +ready()
    }

    class ClusterInitTarget {
        +cluster Cluster
        +initialize(watcher)
        +ready()
    }

    class ListenerInitTarget {
        +listener Listener
        +initialize(watcher)
        +ready()
    }

    class InitWatcher {
        <<interface>>
        +ready()
    }

    InitManager --> Target: manages
    ClusterInitTarget ..|> Target: implements
    ListenerInitTarget ..|> Target: implements
    InitManager --> InitWatcher: notifies

    note for InitManager "Coordinates async initialization\nMultiple targets can init in parallel\nNotifies watcher when all ready"

    note for Target "Represents something that needs\ninitializing (cluster, listener, etc.)\nCalls ready() when complete"
```

## Configuration Activation

### Atomic Activation

```mermaid
sequenceDiagram
    participant Old as Old Config
    participant New as New Config (Warming)
    participant LM as Listener Manager
    participant CM as Cluster Manager
    participant Conn as Active Connections

    Note over New: All warming complete

    Note over LM,CM: Activation Phase

    CM->>CM: Atomic swap:<br/>old clusters → new clusters

    Note over CM: New clusters now active<br/>Old clusters in draining

    LM->>LM: Atomic swap:<br/>old listeners → new listeners

    Note over LM: New listeners now active<br/>Old listeners in draining

    Note over Conn: New connections use new config

    Old->>Old: Begin draining

    loop While connections active
        Conn->>Old: Existing connections<br/>continue on old config
        Old->>Old: Wait for connections to complete
    end

    Old->>Old: All connections drained

    Note over Old: Destroy old config

    Note over New: Now the current config
```

### Zero-Downtime Update Flow

```mermaid
graph TB
    START[New Config Received]

    START --> VALIDATE[Validate New Config]
    VALIDATE --> VALID{Valid?}
    VALID -->|No| REJECT[Reject & NACK]
    VALID -->|Yes| WARM

    WARM[Warm New Config]
    WARM --> WARM_COMPLETE{Warmed?}
    WARM_COMPLETE -->|No| TIMEOUT[Timeout after 10s]
    TIMEOUT --> PARTIAL[Activate with partial endpoints]
    WARM_COMPLETE -->|Yes| ACTIVATE

    ACTIVATE[Activate New Config]
    ACTIVATE --> ATOMIC[Atomic Swap]

    ATOMIC --> NEW_ACTIVE[New Config Active]
    ATOMIC --> OLD_DRAIN[Old Config Draining]

    NEW_ACTIVE --> SERVE[Serve New Traffic]

    OLD_DRAIN --> WAIT[Wait for Connections]
    WAIT --> CHECK{Connections<br/>Complete?}
    CHECK -->|No| WAIT
    CHECK -->|Yes| DESTROY[Destroy Old Config]

    DESTROY --> COMPLETE[Update Complete]

    style NEW_ACTIVE fill:#27AE60,color:#fff
    style OLD_DRAIN fill:#F39C12,color:#fff
    style REJECT fill:#E74C3C,color:#fff
```

## Draining Old Configuration

### Connection Draining

```mermaid
graph TB
    subgraph "Old Configuration"
        OLD_L[Old Listeners]
        OLD_C[Old Clusters]
    end

    subgraph "Active Connections"
        CONN1[Connection 1]
        CONN2[Connection 2]
        CONN3[Connection 3]
    end

    subgraph "Draining Process"
        STOP_ACCEPT[Stop accepting<br/>new connections]
        CLOSE_IDLE[Close idle connections]
        WAIT_ACTIVE[Wait for active<br/>connections to complete]
        FORCE_CLOSE[Force close after<br/>drain timeout]
        CLEANUP[Cleanup resources]
    end

    OLD_L --> STOP_ACCEPT
    OLD_C --> STOP_ACCEPT

    CONN1 --> WAIT_ACTIVE
    CONN2 --> WAIT_ACTIVE
    CONN3 --> CLOSE_IDLE

    STOP_ACCEPT --> CLOSE_IDLE
    CLOSE_IDLE --> WAIT_ACTIVE

    WAIT_ACTIVE --> CHECK{Timeout?}
    CHECK -->|No, still draining| WAIT_ACTIVE
    CHECK -->|Yes, timeout| FORCE_CLOSE
    CHECK -->|All closed| CLEANUP

    FORCE_CLOSE --> CLEANUP

    style CLEANUP fill:#27AE60,color:#fff
    style FORCE_CLOSE fill:#E74C3C,color:#fff
```

### Drain Timeout Handling

```mermaid
stateDiagram-v2
    [*] --> Active: Config active

    Active --> Draining: New config activated

    state Draining {
        [*] --> StopAccept
        StopAccept --> CloseIdle
        CloseIdle --> WaitActive
        WaitActive --> WaitActive: Connections active
        WaitActive --> Complete: All closed
        WaitActive --> Timeout: Drain timeout (5min)
    }

    Draining --> Destroyed: Complete
    Draining --> ForceDestroy: Timeout

    state ForceDestroy {
        [*] --> ForceClose
        ForceClose --> LogWarning
        LogWarning --> Destroy
    }

    ForceDestroy --> Destroyed
    Destroyed --> [*]

    note right of Draining
        Graceful draining:
        - Stop accepting new connections
        - Close idle connections
        - Wait for active to complete
        Default timeout: 5 minutes
    end note

    note right of ForceDestroy
        Force close on timeout:
        - Log warning
        - Close all connections
        - Prevent resource leak
    end note
```

## Error Handling and Recovery

### Error Categories and Responses

```mermaid
graph TB
    ERROR[Configuration Error]

    ERROR --> TYPE{Error Type?}

    TYPE -->|Validation Error| VAL_ERR[Validation Error]
    TYPE -->|Runtime Error| RUN_ERR[Runtime Error]
    TYPE -->|Resource Error| RES_ERR[Resource Error]
    TYPE -->|Network Error| NET_ERR[Network Error]

    VAL_ERR --> VAL_ACTION[Action:]
    VAL_ACTION --> VAL_1[NACK with error details]
    VAL_ACTION --> VAL_2[Keep existing config]
    VAL_ACTION --> VAL_3[Log error message]
    VAL_ACTION --> VAL_4[Update metrics]

    RUN_ERR --> RUN_ACTION[Action:]
    RUN_ACTION --> RUN_1[Partial apply if possible]
    RUN_ACTION --> RUN_2[NACK affected resources]
    RUN_ACTION --> RUN_3[Log error + stack trace]

    RES_ERR --> RES_ACTION[Action:]
    RES_ACTION --> RES_1[Retry with backoff]
    RES_ACTION --> RES_2[Wait for resources]
    RES_ACTION --> RES_3[Timeout after threshold]

    NET_ERR --> NET_ACTION[Action:]
    NET_ACTION --> NET_1[Reconnect with backoff]
    NET_ACTION --> NET_2[Keep using cached config]
    NET_ACTION --> NET_3[Alert on prolonged outage]

    style VAL_ERR fill:#E74C3C,color:#fff
    style RUN_ERR fill:#F39C12,color:#fff
    style RES_ERR fill:#3498DB,color:#fff
    style NET_ERR fill:#9B59B6,color:#fff
```

### Recovery Scenarios

```mermaid
sequenceDiagram
    participant E as Envoy
    participant I as Istiod

    Note over E,I: Scenario 1: Validation Error

    I->>E: DiscoveryResponse (invalid config)
    E->>E: Validation fails
    E->>I: DiscoveryRequest (NACK)<br/>error: "Invalid cluster"
    Note over E: Keep using old config
    I->>I: Log error, alert ops
    I->>E: DiscoveryResponse (corrected)
    E->>E: Validation succeeds
    E->>I: DiscoveryRequest (ACK)

    Note over E,I: Scenario 2: Connection Loss

    Note over E,I: <Connection Lost>
    E->>E: Detect connection failure
    Note over E: Keep using cached config
    E->>E: Retry with backoff<br/>1s, 2s, 4s, ...
    E->>I: Reconnect attempt
    I->>E: Connection established
    E->>I: Re-subscribe all resources
    I->>E: Send current configs
    E->>I: ACK (already applied)

    Note over E,I: Scenario 3: Partial Update

    I->>E: DiscoveryResponse (CDS)
    E->>E: Apply new clusters
    E->>I: ACK
    I->>E: DiscoveryResponse (EDS)<br/>(for new cluster)
    E->>E: EDS validation fails
    E->>I: NACK (EDS)
    Note over E: New cluster warming incomplete
    Note over E: Old traffic routes unaffected
```

### Metrics and Observability

```mermaid
graph TB
    subgraph "Configuration Metrics"
        M1[config_reload_total<br/>Counter: Total reloads]
        M2[config_reload_success<br/>Counter: Successful reloads]
        M3[config_reload_failure<br/>Counter: Failed reloads]
        M4[config_reload_duration<br/>Histogram: Reload time]
    end

    subgraph "xDS Metrics"
        X1[xds.connected_state<br/>Gauge: Connection status]
        X2[xds.update_attempt<br/>Counter: Update attempts]
        X3[xds.update_success<br/>Counter: Successful updates]
        X4[xds.update_failure<br/>Counter: Failed updates]
        X5[xds.update_rejected<br/>Counter: Rejected configs]
        X6[xds.version<br/>Gauge: Current version]
    end

    subgraph "Warmup Metrics"
        W1[cluster.warming<br/>Gauge: Clusters warming]
        W2[cluster.warming_timeout<br/>Counter: Warming timeouts]
        W3[listener.warming<br/>Gauge: Listeners warming]
    end

    subgraph "Admin Endpoints"
        A1[/config_dump<br/>Current active config]
        A2[/clusters<br/>Cluster status + warming]
        A3[/listeners<br/>Listener status]
        A4[/stats/prometheus<br/>All metrics]
    end

    style M2 fill:#27AE60,color:#fff
    style M3 fill:#E74C3C,color:#fff
    style X1 fill:#3498DB,color:#fff
    style A1 fill:#F39C12,color:#fff
```

## Summary

This document covered how Envoy applies xDS configurations:

1. **xDS Client**: Subscription management and gRPC streaming
2. **Reception**: Message parsing and version tracking
3. **Validation**: Multi-level validation pipeline
4. **Warming**: Cluster and listener initialization
5. **Activation**: Atomic configuration swap
6. **Draining**: Graceful shutdown of old config
7. **Error Handling**: Comprehensive error recovery

### Key Takeaways

- Configuration updates are validated before application
- Warming ensures resources are ready before activation
- Atomic swaps provide zero-downtime updates
- Old configurations drain gracefully
- Errors result in NACK and config rollback
- Extensive metrics enable observability

## Next Steps

Continue to **Part 6: Complete End-to-End Flow Example** to see a full example tracing configuration from Istio CRD to active Envoy config.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code**:
- `source/common/config/` - Envoy config management
- `source/common/upstream/cluster_manager_impl.cc` - Cluster management
