# Upstream Overview — Part 4: Health, Outlier Detection, CDS, and Advanced Topics

## Health Checking System

### Architecture

```mermaid
flowchart TD
    subgraph HealthCheckSystem["Health Checking"]
        Factory["HealthCheckerFactory\n(creates HC from proto)"]
        Context["HealthCheckerFactoryContextImpl\n(cluster, dispatcher, random)"]
        Logger["HealthCheckEventLoggerImpl\n(event logging to file)"]
    end

    subgraph Implementations["HC Implementations (extensions)"]
        HTTP["HttpHealthChecker"]
        TCP["TcpHealthChecker"]
        GRPC["GrpcHealthChecker"]
        Custom["Custom Health Checker"]
    end

    subgraph HDS["Health Discovery Service"]
        HdsDelegate["HdsDelegate\n(gRPC client)"]
        HdsCluster["HdsCluster\n(managed endpoints)"]
    end

    Factory --> Implementations
    Factory --> Context
    Context --> Logger
    HdsDelegate --> HdsCluster
    HdsCluster --> Implementations
```

### Health Check Integration with Host

```mermaid
sequenceDiagram
    participant HC as HealthChecker
    participant Host as HostImpl
    participant HCHM as HealthCheckHostMonitor
    participant PS as PrioritySet
    participant LB as LoadBalancer

    HC->>Host: probe (HTTP/TCP/gRPC)
    Host-->>HC: response

    alt Healthy response
        HC->>HCHM: setHealthy()
        HCHM->>Host: healthFlagClear(FAILED_ACTIVE_HC)
    else Unhealthy response
        HC->>HCHM: setUnhealthy()
        HCHM->>Host: healthFlagSet(FAILED_ACTIVE_HC)
    end

    HCHM->>PS: updateHosts (rebuild healthy/degraded lists)
    PS->>LB: onHostSetUpdate (via callback)
    LB->>LB: rebuild scheduling structure
```

### HDS Flow — Management Server Driven Health Checks

```mermaid
flowchart TD
    MS["Management Server"] -->|"HealthCheckSpecifier\n(gRPC stream)"| HDS["HdsDelegate"]
    HDS --> Create["Create HdsCluster\nfor each spec"]
    Create --> HC["Run HealthCheckers\non managed hosts"]
    HC --> Collect["Collect\nEndpointHealthResponse"]
    Collect --> Report["Report results\nback to management server"]
    Report --> MS
```

## Outlier Detection System

### Detection Algorithms

```mermaid
flowchart TD
    subgraph Immediate["Immediate Detection (per request)"]
        C5xx["Consecutive 5xx\n(any server error)"]
        CGW["Consecutive Gateway Failure\n(502, 503, 504)"]
        CLO["Consecutive Local Origin Failure\n(connect timeout, reset)"]
    end

    subgraph Periodic["Periodic Detection (every interval)"]
        SR["Success Rate\n(statistical outlier across hosts)"]
        FP["Failure Percentage\n(absolute error rate)"]
    end

    Response["Request Response"] --> Immediate
    Timer["Interval Timer"] --> Periodic

    Immediate --> Eject{"Eject host?"}
    Periodic --> Eject
    Eject -->|Yes| Ejection["Remove from LB pool\nExponential backoff uneject"]
    Eject -->|No| Continue["Keep in pool"]
```

### Success Rate Outlier Detection

```mermaid
flowchart TD
    Collect["Collect success rates\nfor all hosts in cluster"] --> Compute["Compute:\navg = mean of all rates\nstdev = standard deviation"]
    Compute --> Threshold["threshold = avg - stdev * factor"]
    Threshold --> Compare["For each host:\nis host_rate < threshold?"]
    Compare -->|Yes| Eject["Eject host"]
    Compare -->|No| Keep["Keep host"]
```

### Ejection Timeline

```mermaid
flowchart LR
    subgraph Timeline["Host Ejection/Unejection Timeline"]
        T0["t=0\nEjected\n(30s backoff)"]
        T1["t=30s\nUnejected"]
        T2["t=45s\nEjected again\n(60s backoff)"]
        T3["t=105s\nUnejected"]
        T4["t=110s\nEjected again\n(120s backoff)"]
    end

    T0 --> T1 --> T2 --> T3 --> T4
```

## CDS and OD-CDS

### CDS Subscription

```mermaid
flowchart TD
    subgraph CDS["CdsApiImpl"]
        Sub["Config::Subscription\n(SotW or Delta)"]
        Helper["CdsApiHelper"]
    end

    MS["Management Server"] -->|"DiscoveryResponse\n(cluster configs)"| Sub
    Sub --> CDS
    CDS --> Helper
    Helper -->|"addOrUpdateCluster()"| CM["ClusterManagerImpl"]
    Helper -->|"removeCluster()"| CM
```

### OD-CDS — Lazy Cluster Loading

```mermaid
flowchart TD
    Router["Router: route requires\nunknown cluster"] --> CM["ClusterManagerImpl\nreturns nullptr"]
    CM --> ODCDS["OdCdsApiImpl\nrequestOnDemandClusterDiscovery()"]
    ODCDS --> MS["Management Server"]
    MS -->|"cluster config"| ODCDS
    ODCDS --> CM2["ClusterManagerImpl\naddOrUpdateCluster()"]
    CM2 -->|"cluster now available"| Router2["Router: retry\nwith cluster ready"]
```

### CDS vs OD-CDS Comparison

| Feature | CDS | OD-CDS |
|---------|-----|--------|
| Subscription | Wildcard (all clusters) | Specific cluster names |
| Timing | At startup + streaming | On-demand when needed |
| Cluster removal | Via SotW diff or delta remove | Handle-based cancellation |
| Use case | Standard cluster config | Large cluster sets, lazy loading |

## Advanced Topics

### Cluster Update Tracker

`ClusterUpdateTracker` caches a `ThreadLocalCluster*` reference, avoiding `ClusterManager::get()` hash lookups on the hot path:

```mermaid
sequenceDiagram
    participant Filter as Network Filter
    participant CUT as ClusterUpdateTracker
    participant CM as ClusterManager

    Note over CUT: First access
    Filter->>CUT: getCluster()
    CUT->>CM: getThreadLocalCluster(name)
    CM-->>CUT: ThreadLocalCluster*
    CUT->>CUT: cache reference

    Note over CUT: Subsequent accesses (hot path)
    Filter->>CUT: getCluster()
    CUT-->>Filter: cached ThreadLocalCluster*

    Note over CM: Cluster updated
    CM->>CUT: onClusterAddOrUpdate(cluster)
    CUT->>CUT: update cached reference

    Note over CM: Cluster removed
    CM->>CUT: onClusterRemoval(name)
    CUT->>CUT: clear cached reference
```

### Cluster Discovery Manager

Per-worker component that manages on-demand cluster discovery callbacks:

```mermaid
sequenceDiagram
    participant Router as Router (worker thread)
    participant CDM as ClusterDiscoveryManager (worker)
    participant CM as ClusterManagerImpl (main)
    participant ODCDS as OdCdsApiImpl (main)

    Router->>CDM: requestClusterDiscovery(name, callback, timeout)
    CDM->>CM: requestOnDemandClusterDiscovery(name, ...)
    CM->>ODCDS: subscribe to cluster name

    ODCDS-->>CM: cluster config received
    CM->>CM: addOrUpdateCluster(config)
    CM->>CDM: onClusterDiscovered(name)
    CDM->>Router: invoke callback(Available)
```

### Default Local Address Selector

Selects the source IP for upstream connections:

```mermaid
flowchart TD
    Request["Create upstream connection"] --> Selector["DefaultUpstreamLocalAddressSelector"]
    Selector --> Check{"Cluster has\nbind_config?"}
    Check -->|Yes| Bind["Use configured bind address"]
    Check -->|No| Auto["Use connection's local address"]
```

### Host Utility Functions

```mermaid
mindmap
  root((HostUtility))
    healthFlagsToString
      Convert health flag bitfield to human-readable string
    createOverrideHostStatus
      Build override host status from config
    selectOverrideHost
      Pick specific host from override set
    forEachHostMetric
      Iterate all host metrics for stats collection
    requestsOrPendingForHost
      Get active request count for a host
```

## Full Component Interaction Map

```mermaid
flowchart TD
    subgraph MainThread["Main Thread"]
        CMI["ClusterManagerImpl"]
        CDS["CdsApiImpl"]
        ODCDS["OdCdsApiImpl"]
        LRS["LoadStatsReporter"]

        CMI --> CDS & ODCDS & LRS

        subgraph ClusterN["Per Cluster"]
            Cluster["ClusterImplBase"]
            CI["ClusterInfoImpl"]
            MPS["MainPrioritySetImpl"]
            HC["HealthChecker"]
            OD["Outlier::Detector"]
            RM["ResourceManagerImpl"]
            TSM["TransportSocketMatcherImpl"]

            Cluster --> CI & MPS & HC & OD
            CI --> RM & TSM
        end

        CMI --> ClusterN
    end

    subgraph WorkerThread["Worker Thread"]
        TLCM["ThreadLocalClusterManagerImpl"]
        CDM["ClusterDiscoveryManager"]

        subgraph PerCluster["Per Cluster (ClusterEntry)"]
            LB["LoadBalancer"]
            HCPM["HTTP ConnPoolMap"]
            TCPM["TCP ConnPoolMap"]
            AC["AsyncClient"]
        end

        TLCM --> PerCluster
        TLCM --> CDM
    end

    CMI -->|"TLS slot"| TLCM
    ODCDS -->|"callback"| CDM

    subgraph External["External"]
        MS["Management Server"]
        US["Upstream Services"]
    end

    CDS <-->|gRPC| MS
    ODCDS <-->|gRPC| MS
    LRS <-->|gRPC| MS
    HC -->|probes| US
    HCPM -->|connections| US
    TCPM -->|connections| US
```

## Troubleshooting Scenarios

### Scenario 1: All Hosts Unhealthy (Panic Mode)

```mermaid
flowchart TD
    A["All hosts in priority 0\nfailing health checks"] --> B{"panic_threshold\nexceeded?"}
    B -->|"Yes (default: 0 = enabled)"| C["LB enters panic mode\nRoute to ALL hosts\n(ignore health status)"]
    B -->|"No (panic disabled)"| D["Return 503\n(no healthy upstream)"]
```

### Scenario 2: Circuit Breaker Trip

```mermaid
flowchart TD
    A["Request arrives"] --> B{"ResourceManager\nrequests().canCreate()?"}
    B -->|Yes| C["Forward to upstream"]
    B -->|No| D["Return 503\nupstream_rq_pending_overflow++"]
    D --> E["Check: upstream_cx_active\nvs connections limit"]
```

### Scenario 3: Host Flapping

```mermaid
flowchart TD
    A["Host alternates\nhealthy/unhealthy rapidly"] --> B["Outlier detector\ntracks consecutive failures"]
    B --> C["Ejection with\nexponential backoff"]
    C --> D["Each re-ejection\ndoubles the backoff time"]
    D --> E["Eventually: host stays ejected\nfor max_ejection_time"]
```
