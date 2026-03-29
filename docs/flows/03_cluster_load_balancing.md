# Cluster Management and Load Balancing Flow

## Overview

This document details how Envoy manages clusters, performs load balancing across upstream hosts, and handles host health and availability.

## Cluster Manager Architecture

```mermaid
classDiagram
    class ClusterManager {
        +map~string,Cluster~ clusters_
        +ThreadLocalCluster tls_clusters_
        +ClusterUpdateCallbacks callbacks_
        +getCluster() ClusterInfoConstSharedPtr
        +addOrUpdateCluster() void
        +removeCluster() void
    }

    class Cluster {
        +ClusterInfo info_
        +HostSet host_set_
        +LoadBalancer load_balancer_
        +HealthChecker health_checker_
        +OutlierDetector outlier_detector_
        +initialize() void
    }

    class ClusterInfo {
        +string name_
        +ConnPoolPtr conn_pool_
        +CircuitBreakers circuit_breakers_
        +Stats stats_
        +ResourceManager resource_manager_
    }

    class HostSet {
        +vector~Host~ hosts_
        +vector~Host~ healthy_hosts_
        +HostsPerLocality hosts_per_locality_
        +updateHosts() void
    }

    class LoadBalancer {
        <<interface>>
        +chooseHost() HostConstSharedPtr
        +peekAnotherHost() HostConstSharedPtr
    }

    class Host {
        +Address address_
        +HealthStatus health_status_
        +Stats stats_
        +Metadata metadata_
        +weight() uint32_t
    }

    ClusterManager --> Cluster
    Cluster --> ClusterInfo
    Cluster --> HostSet
    Cluster --> LoadBalancer
    HostSet --> Host
```

## Cluster Initialization Flow

```mermaid
sequenceDiagram
    participant Config
    participant CM as Cluster Manager
    participant Cluster
    participant DNS
    participant HealthChecker
    participant HostSet

    Config->>CM: addOrUpdateCluster()
    CM->>Cluster: Create cluster

    alt Static Cluster
        Cluster->>HostSet: Set static hosts
        HostSet-->>Cluster: Hosts configured
    else DNS Cluster
        Cluster->>DNS: resolve(hostname)
        DNS-->>Cluster: IP addresses
        Cluster->>HostSet: Update hosts
    else EDS Cluster
        Cluster->>Cluster: Subscribe to EDS
        Note over Cluster: Wait for endpoint update
    end

    Cluster->>HealthChecker: Initialize health checker
    HealthChecker->>HealthChecker: Start health checks

    loop For each host
        HealthChecker->>Host: Health check request
        Host-->>HealthChecker: Health check response
        HealthChecker->>HostSet: Update host health
    end

    Cluster->>CM: Cluster ready
    CM->>CM: Notify callbacks
```

## Load Balancing Decision Flow

```mermaid
flowchart TD
    A[Router: chooseHost] --> B[Get Cluster]
    B --> C[Get Load Balancer]

    C --> D{Load Balancing<br/>Policy?}

    D -->|Round Robin| E[Round Robin LB]
    D -->|Least Request| F[Least Request LB]
    D -->|Random| G[Random LB]
    D -->|Ring Hash| H[Ring Hash LB]
    D -->|Maglev| I[Maglev LB]

    E --> J[Select next host<br/>in rotation]
    F --> K[Select host with<br/>fewest active requests]
    G --> L[Select random host]
    H --> M[Hash key → ring position]
    I --> N[Hash key → Maglev table]

    J --> O[Check Host Health]
    K --> O
    L --> O
    M --> O
    N --> O

    O --> P{Healthy?}
    P -->|Yes| Q{Circuit Breaker<br/>OK?}
    P -->|No| R[Try Next Host]

    Q -->|Yes| S[Return Host]
    Q -->|No| R

    R --> T{Retry Count<br/>< Max?}
    T -->|Yes| O
    T -->|No| U[No Host Available]

    S --> V[Create Connection]
    U --> W[Return Error]
```

## Round Robin Load Balancer

```mermaid
sequenceDiagram
    participant LB as Round Robin LB
    participant HostSet
    participant Hosts

    Note over LB: Current index: 0

    LB->>HostSet: Get healthy hosts
    HostSet-->>LB: [Host1, Host2, Host3, Host4]

    LB->>LB: index = 0
    LB->>Hosts: Get hosts[0]
    Hosts-->>LB: Host1
    LB->>LB: index++

    Note over LB: Request 2

    LB->>LB: index = 1
    LB->>Hosts: Get hosts[1]
    Hosts-->>LB: Host2
    LB->>LB: index++

    Note over LB: Request 3

    LB->>LB: index = 2
    LB->>Hosts: Get hosts[2]
    Hosts-->>LB: Host3
    LB->>LB: index++

    Note over LB: Request 4

    LB->>LB: index = 3
    LB->>Hosts: Get hosts[3]
    Hosts-->>LB: Host4
    LB->>LB: index = 0 (wrap around)
```

## Least Request Load Balancer

```mermaid
flowchart TD
    A[Least Request LB] --> B[Get healthy hosts]
    B --> C[Select random subset<br/>choice_count = 2]

    C --> D[Host1: 5 active requests]
    C --> E[Host2: 3 active requests]

    D --> F{Compare active<br/>requests}
    E --> F

    F --> G[Host2 has fewer<br/>active requests]
    G --> H[Return Host2]

    H --> I[Increment Host2<br/>active request count]

    style E fill:#90EE90
```

## Ring Hash Load Balancer

```mermaid
flowchart TD
    A[Request with hash key] --> B[Compute hash]
    B --> C[Hash: 0x7A3B5C2F]

    C --> D[Ring Hash Table]
    D --> E{Find position on ring}

    E --> F[Position: 1982]
    F --> G[Search clockwise for<br/>first healthy host]

    G --> H{Host at position<br/>1982 healthy?}
    H -->|Yes| I[Return host]
    H -->|No| J[Continue to next position]

    J --> K{Host at position<br/>2105 healthy?}
    K -->|Yes| I
    K -->|No| L[Continue search]

    L --> M{Searched full ring?}
    M -->|No| J
    M -->|Yes| N[No healthy host]

    subgraph "Ring Hash Table"
        R1["Position 0: Host1"]
        R2["Position 512: Host2"]
        R3["Position 1024: Host3"]
        R4["Position 1536: Host4"]
        R5["Position 2048: Host1"]
        R6["Position 2560: Host2"]
    end
```

## Maglev Load Balancer

```mermaid
flowchart LR
    A[Request] --> B[Extract Hash Key]
    B --> C[Compute Maglev Hash]
    C --> D[Maglev Table Lookup]

    D --> E[Table Index: 42]
    E --> F[Table[42] = Host3]

    F --> G{Host3 Healthy?}
    G -->|Yes| H[Return Host3]
    G -->|No| I[Rehash to next entry]

    subgraph "Maglev Lookup Table Size: 65537"
        T1["Index 0: Host1"]
        T2["Index 1: Host2"]
        T3["..."]
        T4["Index 42: Host3"]
        T5["Index 43: Host1"]
        T6["..."]
        T7["Index 65536: Host4"]
    end

    Note1["Provides excellent<br/>load distribution<br/>with minimal disruption<br/>on host changes"]

    D -.-> Note1
```

## Weighted Load Balancing

```mermaid
sequenceDiagram
    participant LB as Weighted Round Robin
    participant HostSet
    participant Host1
    participant Host2
    participant Host3

    Note over Host1: Weight: 3
    Note over Host2: Weight: 2
    Note over Host3: Weight: 1

    LB->>HostSet: Get hosts with weights
    HostSet-->>LB: Weighted host list

    LB->>LB: Calculate total weight = 6
    LB->>LB: Build selection array<br/>[H1,H1,H1,H2,H2,H3]

    loop 6 requests
        LB->>LB: Select from array
        Note over LB: Request 1: H1<br/>Request 2: H1<br/>Request 3: H1<br/>Request 4: H2<br/>Request 5: H2<br/>Request 6: H3
    end

    Note over LB,Host3: Distribution: 50% H1, 33% H2, 17% H3
```

## Priority and Locality Aware Load Balancing

```mermaid
flowchart TD
    A[Load Balancing Request] --> B[Get Priority Levels]

    B --> C{Priority 0<br/>Healthy Hosts?}
    C -->|>= 100%| D[Use Priority 0 only]
    C -->|< 100%| E[Calculate overflow]

    E --> F{Priority 1<br/>Available?}
    F -->|Yes| G[Distribute overflow<br/>to Priority 1]
    F -->|No| H[Overload Priority 0]

    D --> I[Select Locality]
    G --> I
    H --> I

    I --> J{Locality Aware<br/>Enabled?}
    J -->|No| K[Select any host]
    J -->|Yes| L[Get Source Locality]

    L --> M{Same Locality<br/>Hosts Available?}
    M -->|Yes| N[Prefer local hosts]
    M -->|No| O[Use locality weights]

    N --> P[Apply Load Balancer]
    O --> P
    K --> P

    P --> Q{Algorithm?}
    Q -->|Round Robin| R[RR Selection]
    Q -->|Least Request| S[LR Selection]
    Q -->|Random| T[Random Selection]

    R --> U[Selected Host]
    S --> U
    T --> U
```

## Host Selection with Retry

```mermaid
stateDiagram-v2
    [*] --> SelectHost
    SelectHost --> CheckHealth: Host selected

    state CheckHealth {
        [*] --> HealthCheck
        HealthCheck --> Healthy: Host healthy
        HealthCheck --> Degraded: Host degraded
        HealthCheck --> Unhealthy: Host unhealthy
    }

    Healthy --> CheckCircuitBreaker
    Degraded --> RetrySelection: Skip if possible
    Unhealthy --> RetrySelection

    state CheckCircuitBreaker {
        [*] --> CheckLimits
        CheckLimits --> WithinLimits: Under limits
        CheckLimits --> OverLimits: Exceeded limits
    }

    WithinLimits --> Success
    OverLimits --> RetrySelection

    RetrySelection --> SelectHost: Try another host
    RetrySelection --> NoHostAvailable: Max retries

    Success --> [*]: Host selected
    NoHostAvailable --> [*]: Failure
```

## Host Health Status Transitions

```mermaid
stateDiagram-v2
    [*] --> Healthy
    Healthy --> Degraded: Consecutive failures
    Healthy --> Unhealthy: Health check failed
    Degraded --> Healthy: Recovery
    Degraded --> Unhealthy: More failures
    Unhealthy --> Healthy: Health check passed
    Unhealthy --> Removed: Admin drain

    note right of Healthy
        Eligible for load balancing
        Full weight applied
    end note

    note right of Degraded
        Still used if needed
        Reduced priority
    end note

    note right of Unhealthy
        Excluded from load balancing
        Health checks continue
    end note
```

## Subset Load Balancing

```mermaid
flowchart TD
    A[Request with Metadata] --> B[Extract Metadata Keys]
    B --> C[version: v2<br/>stage: canary]

    C --> D[Subset Selector]
    D --> E{Match Subset<br/>Definition?}

    E -->|Yes| F[Get Subset Hosts]
    F --> G[Hosts with matching<br/>metadata]

    G --> H[subset_hosts =<br/>[Host3, Host5, Host7]]

    E -->|No| I{Fallback Policy?}

    I -->|ANY_ENDPOINT| J[Use all hosts]
    I -->|NO_FALLBACK| K[Return no host]
    I -->|DEFAULT_SUBSET| L[Use default subset]

    H --> M[Apply Load Balancer<br/>to Subset]
    J --> M
    L --> M

    M --> N[Select Host]

    style H fill:#90EE90
    style K fill:#ff6b6b
```

## Active Health Checking Flow

```mermaid
sequenceDiagram
    participant Cluster
    participant HealthChecker
    participant Host1
    participant Host2
    participant Host3
    participant HostSet

    Cluster->>HealthChecker: Start health checks
    HealthChecker->>HealthChecker: Set interval timer

    loop Every health_check_interval
        par Concurrent Health Checks
            HealthChecker->>Host1: HTTP GET /health
            HealthChecker->>Host2: HTTP GET /health
            HealthChecker->>Host3: HTTP GET /health
        end

        alt Host1: Success
            Host1-->>HealthChecker: 200 OK
            HealthChecker->>HealthChecker: Reset unhealthy_threshold
        else Host1: Failure
            Host1-->>HealthChecker: Timeout / Error
            HealthChecker->>HealthChecker: Increment unhealthy_count
            alt unhealthy_count >= unhealthy_threshold
                HealthChecker->>HostSet: Mark Host1 unhealthy
            end
        end

        Host2-->>HealthChecker: 200 OK
        Host3-->>HealthChecker: 503 Unavailable
        HealthChecker->>HealthChecker: Update host statuses
    end
```

## Outlier Detection Flow

```mermaid
flowchart TD
    A[Request Result] --> B[Record Result]
    B --> C{Success or<br/>Failure?}

    C -->|Success| D[Reset consecutive<br/>failure count]
    C -->|Failure| E[Increment failure count]

    E --> F{Consecutive Failures<br/>>= Threshold?}
    F -->|No| G[Continue monitoring]
    F -->|Yes| H[Mark host as outlier]

    H --> I[Eject host from<br/>load balancing]
    I --> J[Start ejection timer]

    J --> K{Base Ejection Time<br/>Elapsed?}
    K -->|No| L[Host remains ejected]
    K -->|Yes| M{Max Ejection<br/>Time Reached?}

    M -->|No| N[Increase ejection time<br/>exponentially]
    N --> O[Uneject host]
    M -->|Yes| O

    O --> P[Monitor host again]

    G --> Q{Interval Check<br/>Time?}
    Q -->|Yes| R[Analyze all hosts]
    R --> S{Outliers Detected?}
    S -->|Yes| H
    S -->|No| G
    Q -->|No| G
```

## Panic Threshold Handling

```mermaid
flowchart TD
    A[Load Balancing Request] --> B[Get Healthy Hosts]
    B --> C[Count healthy hosts]

    C --> D{healthy_hosts /<br/>total_hosts}

    D -->|>= panic_threshold| E[Normal Operation]
    E --> F[Use only healthy hosts]

    D -->|< panic_threshold| G[Panic Mode Activated]
    G --> H[Use ALL hosts<br/>including unhealthy]

    H --> I[Log Warning]
    I --> J[Update panic mode stats]

    F --> K[Select Host]
    H --> K

    K --> L[Distribute Load]

    Note1["Panic threshold typically 50%<br/>Prevents complete service<br/>unavailability"]
    Note2["Better to overload<br/>degraded hosts than<br/>have no service"]

    G -.-> Note1
    H -.-> Note2
```

## Zone Aware Routing

```mermaid
sequenceDiagram
    participant Request
    participant LB as Load Balancer
    participant LocalZone as Local Zone Hosts
    participant RemoteZone as Remote Zone Hosts

    Request->>LB: Route request from zone: us-east-1a
    LB->>LB: Get source locality

    alt Local Zone Healthy
        LB->>LocalZone: Get hosts in us-east-1a
        LocalZone-->>LB: 10 hosts (80% healthy)

        LB->>LB: Calculate local routing %
        Note over LB: local_weight = 80%<br/>route locally

        alt Route Locally
            LB->>LocalZone: Select host
            LocalZone-->>LB: Local host
        else Route Remotely
            LB->>RemoteZone: Select host
            RemoteZone-->>LB: Remote host
        end

    else Local Zone Degraded
        LB->>LocalZone: Get hosts
        LocalZone-->>LB: 10 hosts (20% healthy)

        LB->>LB: Calculate spillover
        Note over LB: Insufficient local capacity<br/>route to remote zones

        LB->>RemoteZone: Select host
        RemoteZone-->>LB: Remote host
    end
```

## Connection Pool per Host

```mermaid
classDiagram
    class Cluster {
        +map~Host,ConnPool~ conn_pools_
        +getConnPool() ConnPoolPtr
    }

    class ConnPool {
        +Host host_
        +deque~Connection~ ready_connections_
        +deque~PendingRequest~ pending_requests_
        +Stats stats_
        +newStream() StreamEncoder
        +onConnectionReady() void
    }

    class Connection {
        +State state_
        +uint32_t active_streams_
        +uint32_t max_streams_
        +canCreateStream() bool
    }

    class PendingRequest {
        +Callbacks callbacks_
        +Timeout timeout_
        +cancel() void
    }

    class CircuitBreaker {
        +uint32_t max_connections_
        +uint32_t max_pending_requests_
        +uint32_t max_requests_
        +checkLimits() bool
    }

    Cluster --> ConnPool
    ConnPool --> Connection
    ConnPool --> PendingRequest
    ConnPool --> CircuitBreaker
```

## Cluster Statistics

```yaml
# Cluster membership stats
cluster.backend.membership_total          # Total hosts
cluster.backend.membership_healthy        # Healthy hosts
cluster.backend.membership_degraded       # Degraded hosts

# Load balancing stats
cluster.backend.lb_healthy_panic          # Panic mode activations
cluster.backend.lb_zone_routing_all_directly_remote
cluster.backend.lb_zone_routing_sampled
cluster.backend.lb_zone_routing_cross_zone

# Host selection stats
cluster.backend.lb_local_cluster_not_ok   # Local zone not healthy
cluster.backend.lb_zone_no_capacity_left  # Zone at capacity

# Health check stats
cluster.backend.health_check.attempt      # Health check attempts
cluster.backend.health_check.success      # Successful checks
cluster.backend.health_check.failure      # Failed checks
cluster.backend.health_check.network_failure

# Outlier detection stats
cluster.backend.outlier_detection.ejections_active
cluster.backend.outlier_detection.ejections_total
cluster.backend.outlier_detection.ejections_consecutive_5xx
```

## Load Balancer Selection Algorithm

```mermaid
flowchart TD
    A[Choose Load Balancer] --> B{Configuration}

    B -->|Simple, Stateless| C[Round Robin]
    C --> C1[Best for:<br/>- Homogeneous hosts<br/>- Equal capacity<br/>- Simple setup]

    B -->|Connection-aware| D[Least Request]
    D --> D1[Best for:<br/>- Varying request times<br/>- Long-lived connections<br/>- HTTP/2 multiplexing]

    B -->|Session Affinity| E[Ring Hash / Maglev]
    E --> E1[Best for:<br/>- Sticky sessions<br/>- Cache affinity<br/>- Stateful services]

    B -->|Weighted Distribution| F[Weighted Round Robin]
    F --> F1[Best for:<br/>- Canary deployments<br/>- Unequal host capacity<br/>- Gradual rollouts]

    B -->|Random Distribution| G[Random]
    G --> G1[Best for:<br/>- Simplest algorithm<br/>- Stateless services<br/>- Minimal overhead]
```

## Key Takeaways

### Cluster Management
1. **Dynamic Updates**: Clusters can be added/updated/removed dynamically
2. **Multiple Discovery Types**: Static, DNS, EDS (xDS)
3. **Health Tracking**: Active and passive health checking
4. **Outlier Detection**: Automatic problem host identification

### Load Balancing
1. **Multiple Algorithms**: Round Robin, Least Request, Ring Hash, Maglev, Random
2. **Locality Awareness**: Zone-aware routing, cross-zone failover
3. **Priority Levels**: Spillover between priority tiers
4. **Subset Selection**: Metadata-based host subset filtering

### Health Management
1. **Active Health Checks**: Periodic probes to hosts
2. **Passive Health Checks**: Outlier detection from real traffic
3. **Panic Threshold**: Use unhealthy hosts when needed
4. **Gradual Recovery**: Exponential backoff for ejected hosts

## Related Flows
- [xDS Configuration Updates](04_xds_configuration_flow.md)
- [Upstream Connection Management](06_upstream_connection_management.md)
- [Health Checking](08_health_checking.md)
- [Retry and Circuit Breaking](07_retry_circuit_breaking.md)
