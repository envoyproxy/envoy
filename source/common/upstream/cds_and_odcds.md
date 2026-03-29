# CDS and On-Demand CDS

**Files:** `cds_api_impl.h/.cc`, `cds_api_helper.h/.cc`, `od_cds_api_impl.h/.cc`  
**Namespace:** `Envoy::Upstream`

## Overview

Cluster Discovery Service (CDS) and On-Demand CDS (OD-CDS) enable dynamic cluster configuration. CDS subscribes to a management server and receives cluster configs, while OD-CDS fetches clusters lazily when requested by the data path.

## Class Hierarchy

```mermaid
classDiagram
    class CdsApi {
        <<interface>>
        +initialize()
        +versionInfo(): string
    }

    class CdsApiImpl {
        +initialize()
        +versionInfo(): string
        +onConfigUpdate(resources, version)
        +onConfigUpdate(added, removed, version)
        -subscription_: Config::SubscriptionPtr
        -helper_: CdsApiHelper
    }

    class CdsApiHelper {
        +onConfigUpdate(resources, version): map~string, Resource~
        +removeResources(removed): vector~string~
        -cm_: ClusterManager
    }

    class OdCdsApi {
        <<interface>>
        +requestOnDemandClusterDiscovery(name, callback, timeout): OdCdsApiHandlePtr
    }

    class OdCdsApiImpl {
        +requestOnDemandClusterDiscovery(name, callback, timeout)
        +onConfigUpdate(resources, version)
        +onConfigUpdate(added, removed, version)
        -subscription_: Config::SubscriptionPtr
        -pending_clusters_: map~string, PendingClusterInfo~
    }

    CdsApi <|-- CdsApiImpl
    OdCdsApi <|-- OdCdsApiImpl
    CdsApiImpl *-- CdsApiHelper
    OdCdsApiImpl *-- CdsApiHelper
```

## CDS — Full Cluster Discovery

### Subscription Flow

```mermaid
sequenceDiagram
    participant CM as ClusterManagerImpl
    participant CDS as CdsApiImpl
    participant Sub as Config::Subscription
    participant MS as Management Server

    CM->>CDS: initialize()
    CDS->>Sub: start(type_urls, callbacks)
    Sub->>MS: DiscoveryRequest(type=Cluster)

    MS-->>Sub: DiscoveryResponse(clusters=[A, B, C])
    Sub->>CDS: onConfigUpdate(resources, version)
    CDS->>CDS: helper_.onConfigUpdate(resources, version)

    loop for each cluster resource
        CDS->>CM: addOrUpdateCluster(cluster_config, version)
    end

    Note over CDS: Compare with previous config
    loop for each removed cluster
        CDS->>CM: removeCluster(cluster_name)
    end
```

### SotW vs Delta

```mermaid
flowchart TD
    subgraph SotW["State of the World (SotW)"]
        Full["Full cluster list in every response"]
        SOTW1["onConfigUpdate(resources, version)"]
        SOTW2["Compare to detect removed clusters"]
    end

    subgraph Delta["Delta (Incremental)"]
        Inc["Only added/removed clusters sent"]
        Delta1["onConfigUpdate(added, removed, version)"]
        Delta2["Directly add/remove clusters"]
    end

    CDS["CdsApiImpl implements both callbacks"]
    CDS --> SotW
    CDS --> Delta
```

## CDS Helper — Config Application

`CdsApiHelper` encapsulates the logic for applying CDS updates to the ClusterManager:

```mermaid
sequenceDiagram
    participant CDS as CdsApiImpl
    participant Helper as CdsApiHelper
    participant CM as ClusterManager

    CDS->>Helper: onConfigUpdate(resources, version)

    loop for each resource
        Helper->>Helper: decode and validate cluster proto
        Helper->>CM: addOrUpdateCluster(config, version)
        alt cluster added/updated
            Helper->>Helper: track in updated_clusters
        else no change
            Helper->>Helper: skip
        end
    end

    Helper-->>CDS: map of updated_clusters
```

## OD-CDS — On-Demand Cluster Discovery

OD-CDS enables lazy loading of clusters: a cluster is fetched only when the data path needs it (e.g., a route references a cluster that doesn't exist yet).

### Request Flow

```mermaid
sequenceDiagram
    participant Router as Router Filter
    participant CM as ClusterManagerImpl
    participant ODCDS as OdCdsApiImpl
    participant Sub as Config::Subscription
    participant MS as Management Server

    Router->>CM: getThreadLocalCluster("my-cluster")
    CM-->>Router: nullptr (not found)
    Router->>CM: requestOnDemandClusterDiscovery("my-cluster", callback, timeout)
    CM->>ODCDS: requestOnDemandClusterDiscovery("my-cluster", callback, timeout)
    ODCDS->>ODCDS: add to pending_clusters_ with timeout timer
    ODCDS->>Sub: requestResourceSubscription("my-cluster")
    Sub->>MS: DiscoveryRequest(resource_names=["my-cluster"])

    MS-->>Sub: DiscoveryResponse(clusters=[my-cluster])
    Sub->>ODCDS: onConfigUpdate(added=[my-cluster], removed=[])
    ODCDS->>CM: addOrUpdateCluster(my-cluster_config, version)
    ODCDS->>ODCDS: remove from pending_clusters_
    ODCDS->>Router: invoke callback(ClusterDiscoveryStatus::Available)
```

### Timeout Handling

```mermaid
flowchart TD
    Request["requestOnDemandClusterDiscovery(name, cb, 5s)"] --> Timer["Start 5s timeout timer"]
    Timer --> Wait{"Cluster arrives\nbefore timeout?"}
    Wait -->|Yes| Found["Cancel timer\nInvoke callback(Available)"]
    Wait -->|No| Timeout["Invoke callback(Timeout)\nRemove from pending_clusters_"]
```

### Multiple Waiters

When multiple requests ask for the same cluster simultaneously:

```mermaid
sequenceDiagram
    participant R1 as Router Request 1
    participant R2 as Router Request 2
    participant ODCDS as OdCdsApiImpl

    R1->>ODCDS: requestOnDemandClusterDiscovery("svc-x", cb1, 5s)
    ODCDS->>ODCDS: create pending entry, subscribe

    R2->>ODCDS: requestOnDemandClusterDiscovery("svc-x", cb2, 3s)
    ODCDS->>ODCDS: add cb2 to existing pending entry

    Note over ODCDS: Cluster "svc-x" arrives from management server
    ODCDS->>R1: invoke cb1(Available)
    ODCDS->>R2: invoke cb2(Available)
```

## OdCdsApiHandle — Cancellation

```mermaid
classDiagram
    class OdCdsApiHandle {
        <<interface>>
    }

    class OdCdsApiHandleImpl {
        -od_cds_api_: OdCdsApiImpl
        -cluster_name_: string
        ~OdCdsApiHandleImpl()
    }

    OdCdsApiHandle <|-- OdCdsApiHandleImpl
    Note for OdCdsApiHandleImpl "Destructor cancels the pending\nrequest if cluster not yet resolved"
```

## Integration with ClusterManager

```mermaid
flowchart TD
    subgraph ClusterManagerImpl
        CDS["CdsApiImpl\n(wildcard subscription)"]
        ODCDS["OdCdsApiImpl\n(on-demand subscription)"]
        Active["active_clusters_"]
    end

    CDS -->|"addOrUpdateCluster()\nremoveCluster()"| Active
    ODCDS -->|"addOrUpdateCluster()"| Active
    Active -->|TLS update| Workers["Worker Threads"]
```
