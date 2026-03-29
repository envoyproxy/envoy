# Part 8: `source/common/upstream/` — Cluster Manager and Clusters

## Overview

The `upstream/` folder manages everything about upstream clusters: discovery (CDS/EDS), host management, health checking, load balancing, connection pooling, and outlier detection. The `ClusterManagerImpl` is the central orchestrator.

## Cluster Manager Architecture

```mermaid
graph TD
    subgraph "ClusterManagerImpl (Main Thread)"
        CM["ClusterManagerImpl"]
        CM --> CDS["CdsApiImpl\n(Cluster Discovery)"]
        CM --> ClusterMap["cluster_map_\n(name → ClusterData)"]
        CM --> InitHelper["ClusterManagerInitHelper"]
        
        ClusterMap --> CD1["ClusterData\n(production-api)"]
        ClusterMap --> CD2["ClusterData\n(auth-service)"]
        ClusterMap --> CD3["ClusterData\n(cache-cluster)"]
    end
    
    subgraph "Per-Worker (ThreadLocalClusterManagerImpl)"
        TLCM["ThreadLocalClusterManagerImpl"]
        TLCM --> TLC1["ClusterEntry\n(production-api)"]
        TLCM --> TLC2["ClusterEntry\n(auth-service)"]
        TLCM --> TLC3["ClusterEntry\n(cache-cluster)"]
        
        TLC1 --> LB1["LoadBalancer"]
        TLC1 --> CP1["ConnPoolMap"]
        TLC1 --> AC1["AsyncClient"]
    end
    
    CM -->|"post to workers"| TLCM
```

## Class Hierarchy

```mermaid
classDiagram
    class ClusterManager {
        <<interface>>
        +addOrUpdateCluster(config) bool
        +removeCluster(name)
        +getThreadLocalCluster(name) ThreadLocalCluster*
        +clusters() ClusterInfoMappedConstSharedPtr
    }
    class ClusterManagerImpl {
        -cluster_map_ : ClusterMap
        -init_helper_ : ClusterManagerInitHelper
        -cds_api_ : CdsApiPtr
        -tls_ : ThreadLocal::TypedSlot~ThreadLocalClusterManagerImpl~
        +addOrUpdateCluster(config) bool
        +getThreadLocalCluster(name) ThreadLocalCluster*
    }
    class ThreadLocalClusterManagerImpl {
        -thread_local_clusters_ : map~string, ClusterEntryPtr~
        -thread_local_deferred_clusters_ : map
        +getClusterEntry(name) ClusterEntry*
    }
    class ClusterEntry {
        -cluster_info_ : ClusterInfoConstSharedPtr
        -lb_ : LoadBalancerPtr
        -conn_pool_map_ : PriorityConnPoolMap
        -http_async_client_ : AsyncClientImpl
        +connPool(priority, protocol, context) ConnectionPool
        +httpAsyncClient() AsyncClient
        +info() ClusterInfo
        +loadBalancer() LoadBalancer
    }

    ClusterManagerImpl ..|> ClusterManager
    ClusterManagerImpl --> ThreadLocalClusterManagerImpl : "per worker"
    ThreadLocalClusterManagerImpl --> ClusterEntry : "per cluster"
```

## Cluster Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Added : addOrUpdateCluster()
    Added --> Warming : init health check, DNS
    Warming --> Active : initialization complete
    Active --> Updated : config update
    Updated --> Active
    Active --> Removing : removeCluster()
    Removing --> [*] : drain complete
    
    note right of Warming : ClusterData created\nHealth checkers attached\nEDS subscription started
    note right of Active : Distributed to workers\nLoad balancing active
```

## ClusterData — Central Cluster State

```mermaid
graph TD
    subgraph "ClusterData"
        CI["ClusterInfoImpl\n(immutable cluster config)"]
        Cluster["ClusterImplBase\n(host set, health check, outlier)"]
        LBF["LoadBalancerFactory\n(creates per-worker LBs)"]
        
        Cluster --> PS["PrioritySetImpl"]
        PS --> HS0["HostSetImpl (priority 0)"]
        PS --> HS1["HostSetImpl (priority 1)"]
        
        HS0 --> Hosts["hosts()\nhealthy_hosts()\ndegraded_hosts()"]
        HS0 --> Locality["hosts_per_locality()\nhealthy_hosts_per_locality()"]
    end
```

## Thread-Local Cluster (ClusterEntry)

Each worker thread has its own `ClusterEntry` with a private load balancer and connection pools:

```mermaid
graph TD
    subgraph "ClusterEntry (per worker)"
        LB["LoadBalancer\n(round_robin, least_request, etc.)"]
        CPM["PriorityConnPoolMap"]
        
        CPM --> CPDefault["Default Priority Pools"]
        CPM --> CPHigh["High Priority Pools"]
        
        CPDefault --> Pool1["HTTP/2 Pool → Host A"]
        CPDefault --> Pool2["HTTP/2 Pool → Host B"]
        CPDefault --> Pool3["HTTP/1 Pool → Host C"]
        
        HAC["Http::AsyncClientImpl\n(internal HTTP calls)"]
    end
    
    LB -->|"chooseHost()"| Host["Selected Host"]
    Host --> Pool1
```

## Cluster Discovery (CDS)

```mermaid
sequenceDiagram
    participant MgmtServer as Management Server
    participant CDS as CdsApiImpl
    participant CM as ClusterManagerImpl
    participant Worker as ThreadLocalClusterManagerImpl

    MgmtServer->>CDS: DiscoveryResponse (clusters)
    CDS->>CM: onConfigUpdate(added_clusters, removed_clusters)
    
    loop For each added/updated cluster
        CM->>CM: addOrUpdateCluster(config)
        CM->>CM: Create ClusterData + ClusterImplBase
        CM->>CM: Start health check, outlier detector
    end
    
    loop For each removed cluster
        CM->>CM: removeCluster(name)
    end
    
    CM->>Worker: Post cluster update to all workers
    Worker->>Worker: Create/update ClusterEntry + LoadBalancer
```

## On-Demand CDS (ODCDS)

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant CM as ClusterManagerImpl
    participant ODCDS as OdCdsApiImpl
    participant MgmtServer as Management Server

    Router->>CM: getThreadLocalCluster("dynamic-cluster")
    CM-->>Router: nullptr (not found)
    
    Router->>CM: requestOnDemandClusterDiscovery("dynamic-cluster", callback)
    CM->>ODCDS: requestOnDemandClusterDiscovery(name)
    ODCDS->>MgmtServer: DiscoveryRequest (cluster name)
    MgmtServer-->>ODCDS: DiscoveryResponse (cluster config)
    ODCDS->>CM: onConfigUpdate(cluster)
    CM->>CM: addOrUpdateCluster(config)
    CM->>Router: callback → cluster ready
```

## ClusterInfoImpl — Immutable Cluster Config

```mermaid
graph TD
    CII["ClusterInfoImpl"]
    
    CII --> Name["name()\n→ cluster identifier"]
    CII --> LBType["lbType()\n→ ROUND_ROBIN, LEAST_REQUEST, etc."]
    CII --> CB["resourceManager()\n→ circuit breakers"]
    CII --> Timeout["connectTimeout()\nidleTimeout()"]
    CII --> Features["features()\n→ HTTP2, close_connections_on_host_health_failure"]
    CII --> TSM["transportSocketMatcher()\n→ TLS config selection"]
    CII --> Stats["stats()\n→ cluster-level statistics"]
    CII --> LBConfig["lbConfig()\n→ LB-specific settings"]
    CII --> Metadata["metadata()\n→ cluster metadata"]
    CII --> UpstreamHTTPFilters["http_filter_config_provider_manager()\n→ upstream HTTP filters"]
```

## Cluster Types

```mermaid
classDiagram
    class ClusterImplBase {
        <<abstract>>
        -priority_set_ : PrioritySetImpl
        -health_checker_ : HealthChecker
        -outlier_detector_ : OutlierDetector
        #onConfigUpdate()
    }
    class StaticCluster {
        +StaticCluster(config, context)
    }
    class StrictDnsCluster {
        -resolve_targets_ : list
        +startResolve()
    }
    class LogicalDnsCluster {
        -dns_address_ : Address
        +startResolve()
    }
    class EdsCluster {
        -subscription_ : Subscription
        +onConfigUpdate(endpoints)
    }
    class OriginalDstCluster {
        +addHost(host)
    }

    StaticCluster --|> ClusterImplBase
    StrictDnsCluster --|> ClusterImplBase
    LogicalDnsCluster --|> ClusterImplBase
    EdsCluster --|> ClusterImplBase
    OriginalDstCluster --|> ClusterImplBase
```

## Circuit Breakers (Resource Manager)

```mermaid
graph TD
    subgraph "ResourceManagerImpl"
        MC["max_connections\n(active connections limit)"]
        MPR["max_pending_requests\n(pending queue limit)"]
        MR["max_requests\n(active streams limit)"]
        MRT["max_retries\n(concurrent retries limit)"]
        MCP["max_connection_pools\n(pool count limit)"]
    end
    
    subgraph "Per Priority"
        Default["DEFAULT priority\n→ ResourceManagerImpl"]
        High["HIGH priority\n→ ResourceManagerImpl"]
    end
    
    Check["Router checks\nbefore creating upstream"] --> |"canOpen?"| Default
    Default -->|"limit exceeded"| Reject["503 - circuit breaker open"]
    Default -->|"within limits"| Allow["Create upstream request"]
```

## File Catalog

| File | Key Classes | Purpose |
|------|-------------|---------|
| `cluster_manager_impl.h/cc` | `ClusterManagerImpl`, `ThreadLocalClusterManagerImpl`, `ClusterEntry`, `ClusterData` | Central cluster management |
| `upstream_impl.h/cc` | `HostImpl`, `HostSetImpl`, `PrioritySetImpl`, `ClusterInfoImpl`, `ClusterImplBase` | Host/cluster implementations |
| `cds_api_impl.h/cc` | `CdsApiImpl` | Cluster Discovery Service |
| `cds_api_helper.h/cc` | `CdsApiHelper` | CDS update helper |
| `od_cds_api_impl.h/cc` | `OdCdsApiImpl` | On-demand CDS |
| `cluster_factory_impl.h/cc` | `ClusterFactoryImplBase` | Cluster creation factory |
| `cluster_discovery_manager.h/cc` | `ClusterDiscoveryManager` | On-demand cluster callbacks |
| `cluster_update_tracker.h/cc` | `ClusterUpdateTracker` | Cluster add/remove tracking |
| `resource_manager_impl.h` | `ResourceManagerImpl`, `RetryBudgetImpl` | Circuit breakers |
| `conn_pool_map.h` | `ConnPoolMap` | Per-host connection pool map |
| `priority_conn_pool_map.h` | `PriorityConnPoolMap` | Priority-aware pool map |
| `transport_socket_match_impl.h/cc` | `TransportSocketMatcherImpl` | Transport socket selection |
| `prod_cluster_info_factory.h/cc` | `ProdClusterInfoFactory` | ClusterInfo creation |
| `load_balancer_factory_base.h` | `TypedLoadBalancerFactoryBase` | LB factory base |
| `load_balancer_context_base.h` | `LoadBalancerContextBase` | LB context base |
| `edf_scheduler.h` | `EdfScheduler` | Weighted round-robin scheduler |
| `wrsq_scheduler.h` | `WRSQScheduler` | Weighted random scheduler |
| `locality_pool.h/cc` | `LocalityPool` | Locality object pool |

---

**Previous:** [Part 7 — Router Upstream and Retry](07-router-upstream-retry.md)  
**Next:** [Part 9 — Hosts, Health Checking, and Outlier Detection](09-upstream-hosts-health.md)
