# Upstream Overview — Part 2: Clusters, Hosts, and Priority Sets

## The Upstream Data Model

```mermaid
flowchart TD
    subgraph Cluster["ClusterImplBase"]
        CI["ClusterInfoImpl\n(name, type, LB config, stats)"]
        PS["PrioritySetImpl"]
        HC["HealthCheckerSharedPtr"]
        OD["Outlier::DetectorSharedPtr"]
    end

    subgraph PrioritySet["PrioritySetImpl"]
        HS0["HostSetImpl (priority 0)"]
        HS1["HostSetImpl (priority 1)"]
        HSN["HostSetImpl (priority N)"]
    end

    subgraph HostSet["HostSetImpl"]
        Hosts["hosts_: [h1, h2, h3, h4]"]
        Healthy["healthyHosts_: [h1, h2, h4]"]
        Degraded["degradedHosts_: [h3]"]
        HPL["hostsPerLocality_"]
        LW["localityWeights_"]
    end

    subgraph Host["HostImpl"]
        Addr["address: 10.0.1.5:8080"]
        Meta["metadata"]
        Weight["weight: 100"]
        HFlags["health_flags: bitfield"]
        TSF["transport_socket_factory"]
    end

    Cluster --> PrioritySet
    PrioritySet --> HostSet
    HostSet --> Host
```

## Cluster Type Hierarchy

```mermaid
classDiagram
    class Cluster {
        <<interface>>
        +prioritySet(): PrioritySet
        +info(): ClusterInfoConstSharedPtr
        +initialize(callback)
    }

    class ClusterImplBase {
        #priority_set_: MainPrioritySetImpl
        #info_: ClusterInfoImplPtr
        #health_checker_: HealthCheckerPtr
        #outlier_detector_: DetectorPtr
        +initialize(callback)
        #onPreInitComplete()
    }

    class BaseDynamicClusterImpl {
        +updateDynamicHostList(new, current): bool
    }

    Cluster <|-- ClusterImplBase
    ClusterImplBase <|-- BaseDynamicClusterImpl

    note for ClusterImplBase "Base for all cluster types.\nConcrete types (Static, StrictDNS,\nEDS, etc.) live in extensions."
```

## Cluster Types (registered in extensions)

| Type | Source of Endpoints | Update Mechanism |
|------|-------------------|-----------------|
| **STATIC** | Bootstrap config | Fixed at config time |
| **STRICT_DNS** | DNS resolution | Periodic DNS re-resolve |
| **LOGICAL_DNS** | DNS resolution | Single logical address, resolve on connect |
| **EDS** | xDS API (Endpoint Discovery Service) | Streaming updates from management server |
| **ORIGINAL_DST** | Original destination address | Auto-discovered from connection metadata |

## ClusterInfoImpl — The Cluster's Identity Card

```mermaid
flowchart TD
    subgraph ClusterInfoImpl
        direction TB
        Name["name: my-service"]
        Type["type: EDS"]
        LB["lb_type: ROUND_ROBIN"]
        CB["circuit_breakers (per priority)"]
        CT["connect_timeout: 5s"]
        IT["idle_timeout: 1h"]
        TSM["transport_socket_matcher"]
        LBF["load_balancer_factory"]
        Stats["stats: ClusterStats"]
        Meta["metadata"]
    end
```

### Stats Tracked Per Cluster

```mermaid
mindmap
  root((ClusterStats))
    Connections
      upstream_cx_total
      upstream_cx_active
      upstream_cx_connect_fail
      upstream_cx_connect_timeout
      upstream_cx_idle_timeout
      upstream_cx_destroy
    Requests
      upstream_rq_total
      upstream_rq_active
      upstream_rq_pending_total
      upstream_rq_pending_active
      upstream_rq_timeout
      upstream_rq_retry
      upstream_rq_rx_reset
      upstream_rq_tx_reset
    Health
      membership_total
      membership_healthy
      membership_degraded
      health_check_success
      health_check_failure
    Load Balancing
      lb_healthy_panic
      lb_local_cluster_not_ok
      lb_zone_routing_all_directly
```

## Host Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Discovered : EDS/DNS reports endpoint
    Discovered --> Initializing : added to HostSet
    Initializing --> PendingHC : health checker starts
    PendingHC --> Healthy : first HC passes
    PendingHC --> Unhealthy : first HC fails
    Healthy --> Degraded : EDS/HC marks degraded
    Healthy --> Unhealthy : HC fails or outlier ejected
    Degraded --> Healthy : HC passes and not degraded
    Degraded --> Unhealthy : HC fails
    Unhealthy --> Healthy : HC passes and outlier cleared
    Unhealthy --> PendingRemoval : endpoint removed from EDS
    Healthy --> PendingRemoval : endpoint removed from EDS
    PendingRemoval --> [*] : draining complete
```

## HostSet Update Flow

```mermaid
sequenceDiagram
    participant EDS as EDS / DNS
    participant Cluster as ClusterImplBase
    participant PS as PrioritySetImpl
    participant HS as HostSetImpl
    participant LB as LoadBalancer (worker)

    EDS->>Cluster: new endpoint list
    Cluster->>PS: updateHosts(priority=0, params, added, removed)
    PS->>HS: updateHosts(params, locality_weights, added, removed)
    HS->>HS: rebuild hosts_, healthyHosts_, hostsPerLocality_
    PS->>PS: invoke member_update_cb_list_
    Note over PS,LB: Callbacks notify LBs on all workers
    PS-->>LB: onHostSetUpdate(added, removed)
    LB->>LB: rebuild scheduling data structure
```

## Locality-Aware Endpoint Organization

```mermaid
flowchart TD
    subgraph HostSetImpl["HostSetImpl (priority=0)"]
        AllHosts["All Hosts: [h1, h2, h3, h4, h5, h6]"]

        subgraph HPL["hostsPerLocality_"]
            L1["us-east-1a\n[h1, h2] weight=40"]
            L2["us-east-1b\n[h3, h4] weight=40"]
            L3["us-west-2a\n[h5, h6] weight=20"]
        end

        subgraph HealthyHPL["healthyHostsPerLocality_"]
            HL1["us-east-1a\n[h1, h2]"]
            HL2["us-east-1b\n[h3]"]
            HL3["us-west-2a\n[h5, h6]"]
        end
    end

    AllHosts --> HPL
    HPL --> HealthyHPL
```

## Priority Spillover

When higher-priority host sets are degraded, traffic spills to lower priorities:

```mermaid
flowchart TD
    subgraph P0["Priority 0 (default)"]
        P0H["10 hosts, 3 healthy (30%)"]
    end

    subgraph P1["Priority 1"]
        P1H["5 hosts, 5 healthy (100%)"]
    end

    subgraph P2["Priority 2"]
        P2H["3 hosts, 3 healthy (100%)"]
    end

    Traffic["Incoming Request"] --> Decision{Priority 0\nhealthy%}
    Decision -->|"100% healthy"| P0
    Decision -->|"< overprovisioning factor"| Spillover["Distribute proportionally\nacross P0, P1, P2"]
    Spillover --> P0 & P1 & P2
```

### Overprovisioning Factor

The `overprovisioning_factor` (default 1.4 = 140%) determines when spillover begins:
- If `healthy_hosts / total_hosts * overprovisioning_factor >= 100%`, all traffic stays at this priority
- Otherwise, excess traffic spills to the next priority

## Batch Host Updates

`PrioritySetImpl::batchHostUpdate()` allows atomic updates across multiple priorities:

```mermaid
sequenceDiagram
    participant Caller as EDS Update Handler
    participant PS as PrioritySetImpl
    participant HS0 as HostSet P0
    participant HS1 as HostSet P1
    participant Callbacks as Update Callbacks

    Caller->>PS: batchHostUpdate(callback_fn)
    PS->>PS: defer callbacks
    PS->>HS0: updateHosts(new_hosts_p0, ...)
    PS->>HS1: updateHosts(new_hosts_p1, ...)
    PS->>PS: end batch
    PS->>Callbacks: fire all deferred callbacks at once
    Note over Callbacks: LBs see consistent view across priorities
```

## `MainPrioritySetImpl` — Cross-Priority Host Map

On the main thread, `MainPrioritySetImpl` maintains a map of all hosts across all priorities, enabling O(1) host lookup by address:

```mermaid
flowchart LR
    subgraph MainPrioritySetImpl
        Map["cross_priority_host_map_\n(address → HostConstSharedPtr)"]
    end

    HS0["HostSet P0\n[h1: 10.0.1.1, h2: 10.0.1.2]"] --> Map
    HS1["HostSet P1\n[h3: 10.0.2.1]"] --> Map
    Map --> Lookup["O(1) lookup by address"]
```

## Cluster Factory System

```mermaid
sequenceDiagram
    participant CM as ClusterManagerImpl
    participant Registry as ClusterFactory Registry
    participant Factory as ClusterFactoryImplBase
    participant Context as ClusterFactoryContextImpl
    participant Cluster as ClusterImplBase

    CM->>Registry: getFactory(cluster_type)
    Registry-->>CM: ClusterFactory
    CM->>Context: create(server_context, stats, dns, ssl)
    CM->>Factory: create(config, context)
    Factory->>Factory: createClusterImpl(config, context)
    Factory-->>CM: pair of Cluster, ThreadAwareLoadBalancer
```

### `ConfigurableClusterFactoryBase<ConfigProto>`

For cluster types with custom config (e.g., `aggregate_cluster.proto`):

```mermaid
classDiagram
    class ClusterFactoryImplBase {
        +create(config, context): pair
        #createClusterImpl(config, context)*: pair
    }

    class ConfigurableClusterFactoryBase~ConfigProto~ {
        #createClusterImpl(config, context): pair
        #createClusterWithConfig(config, typed_config, context)*: pair
    }

    ClusterFactoryImplBase <|-- ConfigurableClusterFactoryBase
```
