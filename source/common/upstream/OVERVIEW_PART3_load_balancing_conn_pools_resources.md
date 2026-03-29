# Upstream Overview — Part 3: Load Balancing, Connection Pools, and Resources

## Load Balancing Architecture

```mermaid
flowchart TD
    subgraph Foundation["source/common/upstream (Foundation)"]
        LBC["LoadBalancerContextBase\n(default no-op context)"]
        TLBF["TypedLoadBalancerFactoryBase\n(base factory for registration)"]
        EDF["EdfScheduler\n(weighted round-robin scheduling)"]
        WRSQ["WRSQScheduler\n(weighted random selection)"]
    end

    subgraph Extensions["source/extensions/load_balancing_policies (Concrete Policies)"]
        RR["RoundRobin"]
        LR["LeastRequest"]
        RH["RingHash"]
        Maglev["Maglev"]
        Random["Random"]
        Subset["SubsetLB"]
    end

    Foundation --> Extensions
    EDF -.->|"used by"| RR & LR
    WRSQ -.->|"used by"| Random
    TLBF -.->|"extended by"| RR & LR & RH & Maglev & Random
```

## EDF vs WRSQ — When to Use Which

| Scheduler | Algorithm | Use Case | Deterministic | Fairness |
|-----------|-----------|----------|---------------|----------|
| **EdfScheduler** | Earliest Deadline First | Weighted Round Robin, Slow Start | Yes | Exact proportional over time |
| **WRSQScheduler** | Weighted Random Selection | Random LB, P2C sampling | No (random) | Statistical proportional |

### EDF Scheduling Visualization

```mermaid
flowchart TD
    subgraph Queue["Priority Queue (min-heap by deadline)"]
        E1["Host A\nweight=3\ndeadline=0.33"]
        E2["Host C\nweight=2\ndeadline=0.50"]
        E3["Host B\nweight=1\ndeadline=1.00"]
    end

    Pick["pickAndAdd()"] --> Pop["Pop A (lowest deadline)"]
    Pop --> Advance["current_time = 0.33"]
    Advance --> Readd["Re-add A with\ndeadline = 0.33 + 1/3 = 0.67"]
    Readd --> NewQueue["Queue: C(0.50), A(0.67), B(1.00)"]
    NewQueue --> Next["Next pick: C"]
```

## Connection Pool System

### ConnPoolMap — Per-Host Pool Management

```mermaid
classDiagram
    class ConnPoolMap~KEY_TYPE_POOL_TYPE~ {
        +getPool(key, factory): POOL_TYPE*
        +size(): size_t
        +clear()
        +addIdleCallback(cb)
        +drainConnections(drain_behavior)
        -active_pools_: map~KEY_TYPE, POOL_TYPE~
    }

    class PriorityConnPoolMap~KEY_TYPE_POOL_TYPE~ {
        +getPool(priority, key, factory): POOL_TYPE*
        +size(): size_t
        +clear()
        +addIdleCallback(cb)
        +drainConnections(drain_behavior)
        -pool_maps_: vector~ConnPoolMapPtr~
    }

    PriorityConnPoolMap *-- ConnPoolMap
```

### Connection Pool Lifecycle

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant CE as ClusterEntry
    participant CPM as ConnPoolMap
    participant Pool as HTTP/TCP Pool

    Router->>CE: httpConnPool(priority, protocol, context)
    CE->>CE: chooseHost(context) via LoadBalancer
    CE->>CPM: getPool(host_key, pool_factory)

    alt Pool exists and healthy
        CPM-->>CE: existing Pool*
    else Pool missing
        CPM->>Pool: factory.create(host, priority)
        CPM->>CPM: register idle callback
        CPM-->>CE: new Pool*
    end

    CE-->>Router: ConnPool::Instance*

    Note over Pool: Later, pool goes idle
    Pool->>CPM: idle callback fired
    CPM->>CPM: check recycle policy
```

### Connection Pool in ClusterEntry

```mermaid
flowchart TD
    subgraph ClusterEntry["ClusterEntry (per worker, per cluster)"]
        LB["LoadBalancer\n(chooseHost)"]
        HTTP_CPM["PriorityConnPoolMap\n(HTTP pools)"]
        TCP_CPM["PriorityConnPoolMap\n(TCP pools)"]
        AsyncClient["HttpAsyncClientImpl"]
    end

    Request["Incoming Request"] --> LB
    LB -->|"HostConstSharedPtr"| HTTP_CPM
    HTTP_CPM --> Pool1["H2 pool to 10.0.1.1"]
    HTTP_CPM --> Pool2["H2 pool to 10.0.1.2"]
    HTTP_CPM --> Pool3["H1 pool to 10.0.1.3"]
```

## Resource Management — Circuit Breakers

```mermaid
classDiagram
    class ResourceManager {
        <<interface>>
        +connections(): ResourceLimit
        +pendingRequests(): ResourceLimit
        +requests(): ResourceLimit
        +retries(): ResourceLimit
        +connectionPools(): ResourceLimit
        +maxConnectionsPerHost(): ResourceLimit
    }

    class ResourceManagerImpl {
        -connections_: ManagedResourceImpl
        -pending_requests_: ManagedResourceImpl
        -requests_: ManagedResourceImpl
        -retries_: ManagedResourceImpl
        -connection_pools_: ManagedResourceImpl
        -max_connections_per_host_: ManagedResourceImpl
    }

    class ManagedResourceImpl {
        +canCreate(): bool
        +inc()
        +dec()
        +count(): uint64_t
        +max(): uint64_t
        -current_: Gauge
        -max_: uint64_t
    }

    class RetryBudgetImpl {
        +canCreate(): bool
        +inc()
        +dec()
        -budget_percent_: double
        -min_retry_concurrency_: uint32_t
        -active_requests_: ResourceLimit
    }

    ResourceManager <|-- ResourceManagerImpl
    ResourceManagerImpl *-- ManagedResourceImpl
    ResourceManagerImpl *-- RetryBudgetImpl
```

### Circuit Breaker Flow

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant RM as ResourceManagerImpl
    participant Pool as Connection Pool

    Router->>RM: requests().canCreate()
    alt Under limit
        RM-->>Router: true
        Router->>RM: requests().inc()
        Router->>Pool: newStream(callbacks)
        Note over Pool: Request completes
        Router->>RM: requests().dec()
    else At limit
        RM-->>Router: false
        Router->>Router: return 503 (circuit breaker tripped)
    end
```

### Resource Limits Per Priority

```mermaid
flowchart TD
    subgraph ClusterInfoImpl
        RM0["ResourceManager (priority 0)\nconnections=1024\npending_requests=1024\nrequests=1024\nretries=3"]
        RM1["ResourceManager (priority 1)\nconnections=1024\npending_requests=1024\nrequests=1024\nretries=3"]
    end
```

### Retry Budget

An alternative to fixed retry limits:

```mermaid
flowchart TD
    Budget["RetryBudgetImpl"] --> Check{"active_retries <\nmax(min_concurrency,\nbudget% * active_requests)?"}
    Check -->|Yes| Allow["Allow retry"]
    Check -->|No| Deny["Deny retry\n(budget exhausted)"]
```

## Transport Socket Matching

`TransportSocketMatcherImpl` selects the correct transport socket (TLS config) for a given upstream host based on endpoint metadata:

```mermaid
sequenceDiagram
    participant Conn as Connection Creation
    participant TSM as TransportSocketMatcherImpl
    participant Host as HostImpl

    Conn->>TSM: resolve(host.metadata(), match_criteria)
    TSM->>TSM: iterate match_configs_
    loop for each TransportSocketMatch
        TSM->>TSM: check metadata labels
        alt all labels match
            TSM-->>Conn: matched TransportSocketFactory
        end
    end
    alt no match found
        TSM-->>Conn: default TransportSocketFactory
    end
```

### Transport Socket Match Config

```mermaid
flowchart TD
    subgraph Matches["TransportSocketMatcherImpl"]
        M1["Match 1: {env: prod}\n→ TLS with prod certs"]
        M2["Match 2: {env: staging}\n→ TLS with staging certs"]
        Default["Default: plain TCP\n(no TLS)"]
    end

    Host1["Host metadata: {env: prod}"] --> M1
    Host2["Host metadata: {env: staging}"] --> M2
    Host3["Host metadata: {}"] --> Default
```

## Load Stats Reporter (LRS)

```mermaid
sequenceDiagram
    participant CM as ClusterManagerImpl
    participant LRS as LoadStatsReporter
    participant MS as Management Server

    CM->>LRS: start()
    LRS->>MS: LoadStatsRequest (gRPC stream)
    MS-->>LRS: LoadStatsResponse (interval, clusters to report)

    loop every reporting interval
        LRS->>CM: collect load stats for requested clusters
        CM-->>LRS: per-locality request counts, error counts
        LRS->>MS: LoadStatsRequest (with load report)
    end
```

### Reported Metrics

| Metric | Description |
|--------|-------------|
| `total_successful_requests` | Requests with 2xx/3xx |
| `total_requests_in_progress` | Currently active requests |
| `total_error_requests` | Requests with 5xx |
| `total_issued_requests` | All requests attempted |
| `upstream_endpoint_stats` | Per-endpoint load metrics |
| `dropped_requests` | Requests dropped by circuit breaker |
