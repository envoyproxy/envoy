# ClusterManagerImpl

**Files:** `source/common/upstream/cluster_manager_impl.h` / `.cc`  
**Size:** ~47 KB header, ~112 KB implementation  
**Namespace:** `Envoy::Upstream`

## Overview

`ClusterManagerImpl` is the **central hub** for all upstream cluster management in Envoy. Running on the main thread, it owns the authoritative cluster list, coordinates CDS/OD-CDS subscriptions, dispatches cluster state to worker threads via thread-local slots, and manages connection pool lifecycle.

## Class Hierarchy

```mermaid
classDiagram
    class ClusterManagerImpl {
        +addOrUpdateCluster(config, version): bool
        +removeCluster(name): bool
        +getThreadLocalCluster(name): ThreadLocalCluster*
        +httpConnPoolForCluster(name, priority, protocol, context): ConnPool
        +tcpConnPoolForCluster(name, priority, context): ConnPool
        +httpAsyncClientForCluster(name): AsyncClient
        +clusters(): ClusterInfoMappedConstSharedPtr
        -active_clusters_: map~name, ClusterManagerCluster~
        -warming_clusters_: map~name, ClusterManagerCluster~
        -tls_: ThreadLocal::TypedSlot~ThreadLocalClusterManagerImpl~
    }

    class ClusterManager {
        <<interface>>
    }

    class ThreadLocalClusterManagerImpl {
        +getClusterEntry(name): ClusterEntry*
        -thread_local_clusters_: map~name, ClusterEntryPtr~
        -thread_local_deferred_clusters_: map~name, CIO~
    }

    class ClusterEntry {
        +connPool(priority, protocol, context): ConnPool
        +tcpConnPool(priority, context): ConnPool
        +tcpConn(context): Host::CreateConnectionData
        +loadBalancer(): LoadBalancer
        +httpAsyncClient(): AsyncClient
        -priority_set_: PrioritySet
        -lb_: LoadBalancerPtr
        -http_async_client_: AsyncClientImpl
    }

    class ClusterManagerCluster {
        +cluster(): ClusterConstSharedPtr
        +loadBalancerFactory(): TypedLoadBalancerFactory
        +addedOrUpdated(): bool
    }

    ClusterManager <|-- ClusterManagerImpl
    ClusterManagerImpl *-- ThreadLocalClusterManagerImpl
    ThreadLocalClusterManagerImpl *-- ClusterEntry
    ClusterManagerImpl *-- ClusterManagerCluster
```

## Main Thread vs Worker Thread

```mermaid
flowchart TD
    subgraph MainThread["Main Thread"]
        CMI["ClusterManagerImpl"]
        Active["active_clusters_\n(map: name → ClusterManagerCluster)"]
        Warming["warming_clusters_\n(map: name → ClusterManagerCluster)"]
        CDS["CdsApiImpl"]
        ODCDS["OdCdsApiImpl"]
        LRS["LoadStatsReporter"]
    end

    subgraph WorkerN["Worker Thread N"]
        TLCMI["ThreadLocalClusterManagerImpl"]
        CE1["ClusterEntry (cluster-a)\n- LoadBalancer\n- ConnPoolMap\n- AsyncClient"]
        CE2["ClusterEntry (cluster-b)"]
    end

    CMI --> Active & Warming
    CMI --> CDS & ODCDS & LRS
    CMI -->|TLS slot update| TLCMI
    TLCMI --> CE1 & CE2
```

## Initialization Phases

```mermaid
stateDiagram-v2
    [*] --> Loading : bootstrap clusters parsed
    Loading --> WaitPrimary : static clusters created
    WaitPrimary --> WaitSecondary : primary clusters initialized
    WaitSecondary --> WaitCDS : secondary clusters started
    WaitCDS --> CDSInitialized : first CDS response
    CDSInitialized --> AllReady : all CDS clusters initialized
    AllReady --> [*] : ClusterManager ready
```

## Add/Update Cluster Flow

```mermaid
sequenceDiagram
    participant CDS as CDS API
    participant CMI as ClusterManagerImpl
    participant Factory as ClusterFactory
    participant TLS as ThreadLocal Slot
    participant Workers as Worker Threads

    CDS->>CMI: addOrUpdateCluster(proto_config, version)
    CMI->>CMI: check if cluster name exists

    alt New cluster
        CMI->>Factory: create(config, context)
        Factory-->>CMI: ClusterSharedPtr
        CMI->>CMI: add to warming_clusters_
        CMI->>CMI: cluster.initialize(callback)
        Note over CMI: Wait for EDS/DNS/health check init
        CMI->>CMI: move warming → active
        CMI->>TLS: update thread-local slot
        TLS->>Workers: create ClusterEntry per worker
    else Update existing
        CMI->>CMI: compare config
        CMI->>CMI: update cluster info
        CMI->>TLS: update thread-local slot
    end
```

## Connection Pool Access

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant CE as ClusterEntry (worker thread)
    participant LB as LoadBalancer
    participant CPM as ConnPoolMap
    participant Pool as HttpConnPool

    Router->>CE: connPool(priority, protocol, context)
    CE->>LB: chooseHost(context)
    LB-->>CE: HostConstSharedPtr
    CE->>CPM: getPool(host, protocol)
    alt Pool exists
        CPM-->>CE: existing pool
    else No pool
        CPM->>Pool: create new pool for host
        CPM-->>CE: new pool
    end
    CE-->>Router: ConnPool::Instance*
```

## `ClusterManagerInitHelper`

Coordinates multi-phase initialization at startup:

```mermaid
flowchart TD
    Phase1["Phase 1: Loading\nParse bootstrap clusters"] --> Phase2["Phase 2: Primary Init\nStatic + EDS clusters initialize"]
    Phase2 --> Phase3["Phase 3: Secondary Init\nADS-dependent clusters"]
    Phase3 --> Phase4["Phase 4: CDS Init\nFirst CDS response received"]
    Phase4 --> Phase5["Phase 5: CDS Clusters Init\nCDS clusters initialize"]
    Phase5 --> Phase6["Phase 6: All Ready\nServer can accept traffic"]
```

## Deferred Cluster Instantiation

Worker threads lazily create `ClusterEntry` objects. When a cluster is added, only a lightweight `ClusterInitializationObject` (CIO) is dispatched; the full `ClusterEntry` (with LB, conn pools) is created on first access:

```mermaid
sequenceDiagram
    participant CMI as ClusterManagerImpl (main)
    participant TLS as ThreadLocal Slot
    participant TLCMI as ThreadLocalClusterManagerImpl (worker)

    CMI->>TLS: post CIO for "cluster-a"
    TLS->>TLCMI: store in thread_local_deferred_clusters_

    Note over TLCMI: Later, first request for "cluster-a"
    TLCMI->>TLCMI: getClusterEntry("cluster-a")
    TLCMI->>TLCMI: found in deferred, create ClusterEntry
    TLCMI->>TLCMI: move to thread_local_clusters_
    TLCMI-->>Caller: ClusterEntry*
```

## Stats

```mermaid
mindmap
  root((ClusterManager Stats))
    Clusters
      cluster_added
      cluster_modified
      cluster_removed
      active_clusters
      warming_clusters
    Updates
      cluster_updated
      cluster_updated_via_merge
      update_out_of_merge_window
    Init
      warming_state
```
