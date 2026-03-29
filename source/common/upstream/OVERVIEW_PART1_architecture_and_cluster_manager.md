# Upstream Overview — Part 1: Architecture & Cluster Manager

## High-Level Architecture

The `source/common/upstream` directory implements Envoy's upstream cluster management system — the machinery that tracks upstream services, their endpoints, health, and provides load-balanced connection pools to the data path.

```mermaid
flowchart TD
    subgraph Control["Control Plane (Main Thread)"]
        CMI["ClusterManagerImpl"]
        CDS["CdsApiImpl"]
        ODCDS["OdCdsApiImpl"]
        LRS["LoadStatsReporter"]
        Clusters["Active/Warming Clusters\n(ClusterImplBase instances)"]
        HC["Health Checkers"]
        OD["Outlier Detectors"]
    end

    subgraph Data["Data Plane (Per Worker Thread)"]
        TLCM["ThreadLocalClusterManagerImpl"]
        CEs["ClusterEntry instances\n(ThreadLocalCluster)"]
        LBs["Load Balancers"]
        Pools["Connection Pools"]
    end

    subgraph External["External"]
        MS["Management Server\n(xDS)"]
        Upstreams["Upstream Services"]
    end

    MS -->|CDS/EDS/HDS| CMI
    CMI --> Clusters
    Clusters --> HC & OD
    CMI -->|TLS slot| TLCM
    TLCM --> CEs
    CEs --> LBs --> Pools
    Pools -->|connections| Upstreams
    HC -->|probes| Upstreams
    LRS -->|load reports| MS
```

## File Map

| File | Purpose | Key Classes |
|------|---------|-------------|
| `cluster_manager_impl.h/.cc` | Central cluster management, TLS, conn pools | `ClusterManagerImpl`, `ThreadLocalClusterManagerImpl`, `ClusterEntry` |
| `upstream_impl.h/.cc` | Host, HostSet, PrioritySet, ClusterInfo, ClusterBase | `HostImpl`, `HostSetImpl`, `PrioritySetImpl`, `ClusterInfoImpl`, `ClusterImplBase` |
| `cluster_factory_impl.h/.cc` | Cluster creation factory | `ClusterFactoryImplBase`, `ClusterFactoryContextImpl` |
| `cds_api_impl.h/.cc` | CDS subscription client | `CdsApiImpl` |
| `cds_api_helper.h/.cc` | CDS config application | `CdsApiHelper` |
| `od_cds_api_impl.h/.cc` | On-demand CDS | `OdCdsApiImpl` |
| `outlier_detection_impl.h/.cc` | Outlier detection | `DetectorImpl`, `DetectorHostMonitorImpl` |
| `health_checker_impl.h/.cc` | Health checker factory | `HealthCheckerFactory`, `HealthCheckerFactoryContextImpl` |
| `health_checker_event_logger.h/.cc` | HC event logging | `HealthCheckEventLoggerImpl` |
| `health_discovery_service.h/.cc` | HDS client | `HdsDelegate`, `HdsCluster` |
| `edf_scheduler.h` | Earliest deadline first scheduler | `EdfScheduler<C>` |
| `wrsq_scheduler.h` | Weighted random selection queue | `WRSQScheduler<C>` |
| `load_balancer_context_base.h` | Default LB context | `LoadBalancerContextBase` |
| `load_balancer_factory_base.h` | Base LB factory | `TypedLoadBalancerFactoryBase<P>` |
| `resource_manager_impl.h` | Circuit breaker resources | `ResourceManagerImpl` |
| `conn_pool_map.h` | Generic conn pool map | `ConnPoolMap<K,V>` |
| `priority_conn_pool_map.h` | Per-priority conn pool map | `PriorityConnPoolMap<K,V>` |
| `transport_socket_match_impl.h/.cc` | Transport socket selection | `TransportSocketMatcherImpl` |
| `load_stats_reporter.h/.cc` | LRS client | `LoadStatsReporter` |
| `host_utility.h/.cc` | Host helper functions | `HostUtility` |
| `cluster_discovery_manager.h/.cc` | Per-worker OD-CDS callbacks | `ClusterDiscoveryManager` |
| `cluster_update_tracker.h/.cc` | Cached cluster reference | `ClusterUpdateTracker` |
| `default_local_address_selector.h/.cc` | Source address selection | `DefaultUpstreamLocalAddressSelector` |

## ClusterManagerImpl Deep Dive

### Responsibilities

```mermaid
mindmap
  root((ClusterManagerImpl))
    Cluster Lifecycle
      addOrUpdateCluster
      removeCluster
      warming management
      initialization ordering
    xDS Integration
      CDS subscription
      OD-CDS subscription
      LRS reporting
    Thread-Local Distribution
      TLS slot management
      Deferred cluster instantiation
      ClusterEntry creation
    Connection Pooling
      HTTP conn pool allocation
      TCP conn pool allocation
      Async HTTP client access
    Init Coordination
      Multi-phase initialization
      ClusterManagerInitHelper
      Init targets
```

### Thread Model

```mermaid
flowchart TD
    subgraph Main["Main Thread — Owns Authoritative State"]
        direction TB
        CMI["ClusterManagerImpl"]
        ACM["active_clusters_\n(name → ClusterManagerCluster)"]
        WCM["warming_clusters_\n(name → ClusterManagerCluster)"]
        Init["ClusterManagerInitHelper"]
    end

    subgraph W1["Worker Thread 1"]
        direction TB
        TLC1["ThreadLocalClusterManagerImpl"]
        CE1a["ClusterEntry: cluster-a"]
        CE1b["ClusterEntry: cluster-b"]
    end

    subgraph W2["Worker Thread 2"]
        direction TB
        TLC2["ThreadLocalClusterManagerImpl"]
        CE2a["ClusterEntry: cluster-a"]
        CE2b["ClusterEntry: cluster-b"]
    end

    CMI --> ACM & WCM & Init
    CMI -->|"TLS::Slot::set()"| TLC1 & TLC2
    TLC1 --> CE1a & CE1b
    TLC2 --> CE2a & CE2b
```

### Warming Pipeline

When a cluster is created or updated, it goes through a warming phase:

```mermaid
sequenceDiagram
    participant Config as CDS / Bootstrap
    participant CMI as ClusterManagerImpl
    participant Cluster as ClusterImplBase
    participant Workers as Worker Threads

    Config->>CMI: addOrUpdateCluster(config)
    CMI->>CMI: create ClusterManagerCluster
    CMI->>CMI: add to warming_clusters_
    CMI->>Cluster: initialize(warmingCallback)

    Note over Cluster: DNS resolution, EDS fetch,\ninitial health checks...

    Cluster-->>CMI: warmingCallback() - init complete
    CMI->>CMI: move warming → active
    CMI->>Workers: update TLS with new cluster
```

### `ProdClusterManagerFactory`

Creates the `ClusterManagerImpl` from bootstrap config:

```mermaid
sequenceDiagram
    participant Bootstrap as Bootstrap Config
    participant Factory as ProdClusterManagerFactory
    participant CMI as ClusterManagerImpl

    Bootstrap->>Factory: clusterManagerFromProto(bootstrap)
    Factory->>Factory: allocate conn pool factories
    Factory->>CMI: create(bootstrap, factory, stats, tls, dns, ssl, runtime)
    CMI->>CMI: load static clusters
    CMI->>CMI: create CDS if configured
    CMI-->>Bootstrap: ClusterManagerPtr
```

## Key Design Patterns

### 1. Thread-Local Caching

All cluster state accessed by the data path goes through `ThreadLocalClusterManagerImpl`. Workers never lock on the main-thread cluster map. Updates flow one-way: main → workers via `TLS::Slot::runOnAllThreads()`.

### 2. Deferred Instantiation

`ClusterEntry` (with its load balancer and connection pools) is created lazily on the first access per worker thread, minimizing memory when clusters are configured but not actively used.

### 3. Two-Phase Cluster Lifecycle

Clusters exist in either `warming_clusters_` (initializing) or `active_clusters_` (ready for traffic). A cluster must fully initialize (DNS, EDS, initial HC) before it becomes active.

### 4. Priority-Based Resource Management

Each cluster has multiple priority levels (0 = highest). Hosts, host sets, and resource managers are all organized per-priority, enabling spillover to lower-priority endpoints when higher-priority ones are exhausted.

### 5. Pluggable Everything

Cluster factories, load balancer factories, health checker factories, and transport socket factories are all registered via the extension system, enabling custom implementations without modifying core code.
