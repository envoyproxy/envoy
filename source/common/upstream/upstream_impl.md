# Upstream Implementation — Hosts, Clusters, Priority Sets

**Files:** `source/common/upstream/upstream_impl.h` / `.cc`  
**Size:** ~60 KB header, ~121 KB implementation  
**Namespace:** `Envoy::Upstream`

## Overview

`upstream_impl.h` contains the core upstream data model: hosts, host sets, priority sets, cluster info, and cluster base implementations. This is the largest file in the upstream directory and defines how Envoy represents upstream endpoints and clusters.

## Class Hierarchy

```mermaid
classDiagram
    class HostDescriptionImplBase {
        +address(): Address::InstanceConstSharedPtr
        +healthChecker(): HealthCheckHostMonitor
        +outlierDetector(): DetectorHostMonitor
        +metadata(): MetadataConstSharedPtr
        +cluster(): ClusterInfo
        +locality(): Locality
    }

    class HostImplBase {
        +health(): Host::Health
        +coarseHealth(): Host::Health
        +weight(): uint32_t
        +setWeight(weight)
        +healthFlagGet(flag): bool
        +healthFlagSet(flag)
        +healthFlagClear(flag)
        +createConnection(dispatcher, options): CreateConnectionData
        +setEdsHealthStatus(status)
    }

    class HostImpl {
    }

    class HostSetImpl {
        +hosts(): HostVector
        +healthyHosts(): HostVector
        +degradedHosts(): HostVector
        +hostsPerLocality(): HostsPerLocality
        +updateHosts(update_hosts_params, ...)
        +priority(): uint32_t
        -hosts_: HostVector
        -healthy_hosts_: HostVector
    }

    class PrioritySetImpl {
        +hostSetsPerPriority(): vector~HostSetPtr~
        +getOrCreateHostSet(priority): HostSet
        +updateHosts(priority, update_params, ...)
        +batchHostUpdate(callback)
    }

    class ClusterInfoImpl {
        +name(): string
        +type(): ClusterType
        +lbConfig(): LbConfig
        +resourceManager(priority): ResourceManager
        +stats(): ClusterStats
        +transportSocketMatcher(): TransportSocketMatcher
        +loadBalancerFactory(): TypedLoadBalancerFactory
    }

    class ClusterImplBase {
        +prioritySet(): PrioritySet
        +info(): ClusterInfoConstSharedPtr
        +initialize(callback)
        +healthChecker(): HealthCheckerSharedPtr
        +outlierDetector(): Outlier::DetectorSharedPtr
    }

    HostDescriptionImplBase <|-- HostImplBase
    HostImplBase <|-- HostImpl
    PrioritySetImpl *-- HostSetImpl
    ClusterImplBase *-- PrioritySetImpl
    ClusterImplBase *-- ClusterInfoImpl
```

## Host Data Model

```mermaid
flowchart TD
    subgraph HostImpl
        Addr["address: 10.0.1.5:8080"]
        Meta["metadata: {version: v2}"]
        Locality["locality: us-east-1a"]
        Weight["weight: 100"]
        Health["health: HEALTHY"]
        EDS["eds_health_status: HEALTHY"]
        Flags["health_flags: bitfield"]
        HCM["health_check_monitor_"]
        ODM["outlier_detector_monitor_"]
    end

    HostImpl --> TSF["TransportSocketFactory\n(TLS config for this host)"]
    HostImpl --> Stats["Per-host stats:\ncx_total, rq_total, rq_success, etc."]
```

## Host Health Model

```mermaid
stateDiagram-v2
    [*] --> Healthy
    Healthy --> Degraded : EDS marks DEGRADED
    Healthy --> Unhealthy : Health check fails
    Healthy --> Unhealthy : Outlier detected
    Degraded --> Healthy : EDS marks HEALTHY
    Degraded --> Unhealthy : Health check fails
    Unhealthy --> Healthy : Health check passes + outlier cleared
    Unhealthy --> Unhealthy : Still failing
```

### Health Flags

| Flag | Set By | Meaning |
|------|--------|---------|
| `FAILED_ACTIVE_HC` | Health checker | Active health check failed |
| `FAILED_OUTLIER_CHECK` | Outlier detector | Ejected by outlier detection |
| `FAILED_EDS_HEALTH` | EDS | EDS reports unhealthy |
| `DEGRADED_ACTIVE_HC` | Health checker | Active HC reports degraded |
| `DEGRADED_EDS_HEALTH` | EDS | EDS reports degraded |
| `PENDING_DYNAMIC_REMOVAL` | Cluster manager | Host scheduled for removal |
| `PENDING_ACTIVE_HC` | Health checker | Awaiting first health check |
| `EXCLUDED_VIA_IMMEDIATE_HC_FAIL` | Health checker | Immediate HC fail config |

## HostSet — Per Priority

```mermaid
classDiagram
    class HostSetImpl {
        +priority(): uint32_t
        +hosts(): HostVector
        +healthyHosts(): HostVector
        +degradedHosts(): HostVector
        +excludedHosts(): HostVector
        +hostsPerLocality(): HostsPerLocality
        +healthyHostsPerLocality(): HostsPerLocality
        +localityWeights(): LocalityWeightsConstSharedPtr
        +overprovisioning_factor(): uint32_t
        +updateHosts(params, locality_weights, hosts_added, hosts_removed)
    }
```

### Locality-Aware Host Organization

```mermaid
flowchart TD
    HS["HostSetImpl (priority=0)"] --> All["hosts_: [h1, h2, h3, h4, h5]"]
    HS --> Healthy["healthyHosts_: [h1, h2, h4, h5]"]
    HS --> HPL["hostsPerLocality_"]
    HPL --> L1["us-east-1a: [h1, h2]"]
    HPL --> L2["us-east-1b: [h3, h4]"]
    HPL --> L3["us-west-2a: [h5]"]
    HS --> LW["localityWeights_: {us-east-1a: 40, us-east-1b: 40, us-west-2a: 20}"]
```

## PrioritySet — Multi-Priority

```mermaid
classDiagram
    class PrioritySetImpl {
        +hostSetsPerPriority(): vector~HostSetPtr~
        +getOrCreateHostSet(priority, overprovisioning): HostSet
        +updateHosts(priority, params, ..., hosts_added, hosts_removed)
        +batchHostUpdate(callback)
        -host_sets_: vector~HostSetImplPtr~
        -update_cb_list_: list~MemberUpdateCb~
    }

    class MainPrioritySetImpl {
        +crossPriorityHostMap(): HostMapConstSharedPtr
        -cross_priority_host_map_: HostMapSharedPtr
    }

    PrioritySetImpl <|-- MainPrioritySetImpl
    PrioritySetImpl *-- HostSetImpl
```

### Priority Levels

```
PrioritySetImpl
  ├── host_sets_[0] = HostSetImpl (priority 0 — default/primary)
  ├── host_sets_[1] = HostSetImpl (priority 1 — secondary)
  └── host_sets_[2] = HostSetImpl (priority 2 — tertiary)
```

## ClusterInfoImpl — Cluster Metadata

```mermaid
mindmap
  root((ClusterInfoImpl))
    Identity
      name
      type (STATIC/STRICT_DNS/LOGICAL_DNS/EDS/ORIGINAL_DST)
      eds_service_name
    Load Balancing
      lb_type (ROUND_ROBIN, LEAST_REQUEST, RING_HASH, etc.)
      lb_config
      loadBalancerFactory
    Circuit Breakers
      resourceManager (per priority)
    Connection
      connectTimeout
      idleTimeout
      perConnectionBufferLimitBytes
      maxRequestsPerConnection
    TLS
      transportSocketMatcher
    Metadata
      metadata
      typedFilterMetadata
    Stats
      stats (ClusterStats)
      loadReportStats (ClusterLoadReportStats)
```

## ClusterImplBase — Cluster Lifecycle

```mermaid
sequenceDiagram
    participant CM as ClusterManagerImpl
    participant Cluster as ClusterImplBase
    participant HC as HealthChecker
    participant OD as Outlier::Detector
    participant DNS as DnsResolver

    CM->>Cluster: initialize(callback)
    Cluster->>DNS: resolve endpoints (if DNS type)
    DNS-->>Cluster: resolved addresses
    Cluster->>Cluster: updateHosts(priority_set)
    Cluster->>HC: start health checking hosts
    Cluster->>OD: start outlier detection
    HC-->>Cluster: initial health check results
    Cluster-->>CM: initialization_complete_callback()
```

## `PriorityStateManager` — Batch Host Updates

Used during EDS updates to build new host sets before atomically applying them:

```mermaid
sequenceDiagram
    participant EDS as EDS Response
    participant PSM as PriorityStateManager
    participant PS as PrioritySetImpl

    EDS->>PSM: initializePriorityFor(locality, endpoints, priority)
    loop for each endpoint
        PSM->>PSM: registerHostForPriority(host, priority)
    end
    PSM->>PS: updateClusterPrioritySet(...)
    Note over PS: Atomic host set update across all priorities
```

## `BaseDynamicClusterImpl`

Extension of `ClusterImplBase` for clusters whose hosts change dynamically (EDS, DNS):

```mermaid
classDiagram
    class ClusterImplBase {
        +prioritySet(): PrioritySet
        +info(): ClusterInfoConstSharedPtr
    }

    class BaseDynamicClusterImpl {
        +updateDynamicHostList(new_hosts, current_hosts, ...): bool
    }

    ClusterImplBase <|-- BaseDynamicClusterImpl
```
