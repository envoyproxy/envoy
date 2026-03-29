# Part 9: `source/common/upstream/` — Hosts, Health Checking, and Outlier Detection

## Overview

This document covers the host abstraction (how Envoy represents upstream endpoints), health checking (active probing), and outlier detection (passive monitoring). These systems work together to ensure traffic is only sent to healthy upstream hosts.

## Host Architecture

### Host Class Hierarchy

```mermaid
classDiagram
    class HostDescription {
        <<interface>>
        +address() Address
        +hostname() string
        +cluster() ClusterInfo
        +metadata() Metadata
        +locality() Locality
        +stats() HostStats
    }
    class Host {
        <<interface>>
        +health() Health
        +weight() uint32_t
        +healthFlagGet(flag) bool
        +healthFlagSet(flag)
        +healthFlagClear(flag)
        +createConnection(dispatcher, options) ConnectionData
        +setEdsHealthStatus(status)
    }
    class HostDescriptionImplBase {
        -cluster_ : ClusterInfoConstSharedPtr
        -hostname_ : string
        -address_ : Address
        -metadata_ : MetadataConstSharedPtr
        -locality_ : LocalityConstSharedPtr
    }
    class HostImplBase {
        -health_flags_ : atomic~uint32_t~
        -weight_ : uint32_t
        -priority_ : uint32_t
        -eds_health_status_ : HealthStatus
        +health() Health
        +createConnection() ConnectionData
    }
    class HostImpl {
        +HostImpl(cluster, hostname, address, metadata, weight, locality, ...)
    }

    Host --|> HostDescription
    HostDescriptionImplBase ..|> HostDescription
    HostImplBase --|> HostDescriptionImplBase
    HostImplBase ..|> Host
    HostImpl --|> HostImplBase
```

### Host Health Flags

```mermaid
graph TD
    subgraph "Health Flag Bits (atomic uint32_t)"
        F1["FAILED_ACTIVE_HC\n— active health check failed"]
        F2["FAILED_OUTLIER_CHECK\n— outlier detection ejected"]
        F3["FAILED_EDS_HEALTH\n— EDS reports unhealthy"]
        F4["DEGRADED_ACTIVE_HC\n— active HC reports degraded"]
        F5["DEGRADED_EDS_HEALTH\n— EDS reports degraded"]
        F6["PENDING_DYNAMIC_REMOVAL\n— host being removed"]
        F7["PENDING_ACTIVE_HC\n— waiting for first HC"]
        F8["EXCLUDED_VIA_IMMEDIATE_HC_FAIL\n— excluded via HC"]
        F9["ACTIVE_HC_TIMEOUT\n— HC timed out"]
    end
    
    subgraph "Computed Health"
        Healthy["HEALTHY\n— no failure flags"]
        Degraded["DEGRADED\n— degraded flags only"]
        Unhealthy["UNHEALTHY\n— any failure flag set"]
    end
    
    F1 --> Unhealthy
    F2 --> Unhealthy
    F3 --> Unhealthy
    F4 --> Degraded
    F5 --> Degraded
```

### Host Connection Creation

```mermaid
sequenceDiagram
    participant Pool as Connection Pool
    participant Host as HostImplBase
    participant TSF as TransportSocketFactory
    participant Disp as Dispatcher

    Pool->>Host: createConnection(dispatcher, options, transport_options)
    Host->>Host: Resolve transport socket factory
    Host->>TSF: createTransportSocket(options, host)
    TSF-->>Host: TransportSocket (e.g., TLS)
    Host->>Disp: createClientConnection(address, source, transport_socket, ...)
    Disp-->>Host: ClientConnectionPtr
    Host-->>Pool: CreateConnectionData{connection, host_description}
```

## Host Sets and Priority Sets

```mermaid
classDiagram
    class HostSet {
        <<interface>>
        +hosts() HostVector
        +healthyHosts() HostVector
        +degradedHosts() HostVector
        +excludedHosts() HostVector
        +hostsPerLocality() HostsPerLocality
        +healthyHostsPerLocality() HostsPerLocality
        +priority() uint32_t
        +overprovisioningFactor() uint32_t
    }
    class HostSetImpl {
        -hosts_ : HostVector
        -healthy_hosts_ : HostVector
        -degraded_hosts_ : HostVector
        -excluded_hosts_ : HostVector
        -hosts_per_locality_ : HostsPerLocalityImpl
        -priority_ : uint32_t
    }
    class PrioritySet {
        <<interface>>
        +hostSetsPerPriority() vector~HostSetPtr~
        +addMemberUpdateCb(callback)
    }
    class PrioritySetImpl {
        -host_sets_ : vector~HostSetImplPtr~
        +getOrCreateHostSet(priority) HostSetImpl
        +updateHosts(priority, update_hosts_params, ...)
    }

    HostSetImpl ..|> HostSet
    PrioritySetImpl ..|> PrioritySet
    PrioritySetImpl --> HostSetImpl : "per priority"
```

### Priority-Based Host Organization

```mermaid
graph TD
    subgraph "PrioritySetImpl"
        P0["Priority 0 (DEFAULT)\nHostSetImpl"]
        P1["Priority 1\nHostSetImpl"]
        P2["Priority 2\nHostSetImpl"]
    end
    
    subgraph "HostSetImpl (Priority 0)"
        AllHosts["hosts(): [A, B, C, D, E]"]
        HealthyHosts["healthyHosts(): [A, B, C]"]
        DegradedHosts["degradedHosts(): [D]"]
        ExcludedHosts["excludedHosts(): [E]"]
        
        subgraph "Per Locality"
            L1["us-east-1a: [A, B]"]
            L2["us-east-1b: [C, D]"]
            L3["us-west-2a: [E]"]
        end
    end
```

## Health Checking

### Health Checker Architecture

```mermaid
classDiagram
    class HealthChecker {
        <<interface>>
        +start()
        +addHostCheckCompleteCb(callback)
    }
    class HealthCheckerImpl {
        -cluster_ : Cluster
        -dispatcher_ : Dispatcher
        -interval_ : Duration
        -timeout_ : Duration
        -unhealthy_threshold_ : uint32_t
        -healthy_threshold_ : uint32_t
    }
    class HttpHealthCheckerImpl {
        -path_ : string
        -host_ : string
        -expected_statuses_ : set
    }
    class TcpHealthCheckerImpl {
        -send_bytes_ : vector
        -receive_bytes_ : vector
    }
    class GrpcHealthCheckerImpl {
        -service_name_ : string
    }

    HealthCheckerImpl ..|> HealthChecker
    HttpHealthCheckerImpl --|> HealthCheckerImpl
    TcpHealthCheckerImpl --|> HealthCheckerImpl
    GrpcHealthCheckerImpl --|> HealthCheckerImpl
```

### Health Check Flow

```mermaid
sequenceDiagram
    participant HC as HealthChecker
    participant Timer as Interval Timer
    participant Session as HC Session
    participant Host as Host
    participant Backend as Backend

    HC->>Timer: Schedule check (interval)
    Timer->>HC: Timer fires
    HC->>Session: Start health check
    Session->>Host: createConnection(dispatcher)
    Session->>Backend: Send probe (HTTP GET /health)
    
    alt Healthy response (200)
        Backend-->>Session: 200 OK
        Session->>Session: Increment healthy_count
        alt healthy_count >= healthy_threshold
            Session->>Host: healthFlagClear(FAILED_ACTIVE_HC)
            Note over Host: Host becomes healthy
        end
    else Unhealthy response (503)
        Backend-->>Session: 503
        Session->>Session: Increment unhealthy_count
        alt unhealthy_count >= unhealthy_threshold
            Session->>Host: healthFlagSet(FAILED_ACTIVE_HC)
            Note over Host: Host becomes unhealthy
        end
    else Timeout
        Session->>Session: Increment unhealthy_count
        Session->>Host: healthFlagSet(ACTIVE_HC_TIMEOUT)
    end
    
    Session->>Timer: Schedule next check
```

### Health State Transitions

```mermaid
stateDiagram-v2
    [*] --> Healthy : initial (no PENDING_ACTIVE_HC)
    [*] --> PendingFirstHC : initial (with PENDING_ACTIVE_HC)
    
    PendingFirstHC --> Healthy : healthy_threshold met
    PendingFirstHC --> Unhealthy : unhealthy_threshold met
    
    Healthy --> Unhealthy : unhealthy_threshold consecutive failures
    Unhealthy --> Healthy : healthy_threshold consecutive successes
    
    Healthy --> Degraded : degraded response
    Degraded --> Healthy : healthy response
    Degraded --> Unhealthy : unhealthy_threshold failures
```

## Outlier Detection

### How It Works

Outlier detection is **passive** — it monitors actual request outcomes to detect misbehaving hosts and temporarily ejects them:

```mermaid
graph TD
    subgraph "Outlier Detection Algorithms"
        Consec5xx["Consecutive 5xx\n— N consecutive server errors"]
        ConsecGW["Consecutive Gateway Failures\n— N consecutive 502/503/504"]
        SuccessRate["Success Rate\n— host below average minus stdev"]
        FailurePercent["Failure Percentage\n— host above X% failure rate"]
        ConsecLocal["Consecutive Local Origin\n— N consecutive local failures"]
    end
    
    subgraph "Actions"
        Eject["Eject host\n(mark FAILED_OUTLIER_CHECK)"]
        Uneject["Un-eject after\nejection duration"]
    end
    
    Consec5xx --> Eject
    ConsecGW --> Eject
    SuccessRate --> Eject
    FailurePercent --> Eject
    ConsecLocal --> Eject
    Eject --> Uneject
```

### Outlier Detector Class Diagram

```mermaid
classDiagram
    class OutlierDetector {
        <<interface>>
        +addChangedStateCb(callback)
    }
    class DetectorImpl {
        -config_ : DetectorConfig
        -hosts_ : map~Host, DetectorHostMonitorImpl~
        -interval_timer_ : Timer
        -success_rate_accumulator_ : map
        +checkHostForUneject(host)
        +onIntervalTimer()
    }
    class DetectorHostMonitorImpl {
        -num_ejections_ : uint32_t
        -last_ejection_time_ : MonotonicTime
        -success_rate_ : SuccessRateMonitor
        -consecutive_5xx_ : atomic
        -consecutive_gateway_failure_ : atomic
        +putResult(result)
        +eject(time)
        +uneject(time)
    }
    class SuccessRateAccumulator {
        -buckets_ : SuccessRateAccumulatorBucket
        +getSuccessRate() double
    }
    class DetectorConfig {
        -interval_ : Duration
        -base_ejection_time_ : Duration
        -consecutive_5xx_ : uint32_t
        -success_rate_minimum_hosts_ : uint32_t
        -success_rate_stdev_factor_ : double
        -failure_percentage_threshold_ : uint32_t
        -max_ejection_percent_ : uint32_t
    }

    DetectorImpl ..|> OutlierDetector
    DetectorImpl --> DetectorHostMonitorImpl
    DetectorHostMonitorImpl --> SuccessRateAccumulator
    DetectorImpl --> DetectorConfig
```

### Ejection and Un-ejection

```mermaid
sequenceDiagram
    participant Req as Request
    participant Monitor as DetectorHostMonitorImpl
    participant Detector as DetectorImpl
    participant Host as Host

    Req->>Monitor: putResult(Result::LocalOriginConnectFailed)
    Monitor->>Monitor: consecutive_5xx_++
    
    alt consecutive_5xx_ >= threshold
        Monitor->>Detector: Notify ejection candidate
        Detector->>Detector: Check max_ejection_percent
        alt Under max ejection %
            Detector->>Host: healthFlagSet(FAILED_OUTLIER_CHECK)
            Detector->>Monitor: eject(now)
            Note over Host: Host ejected from LB
        end
    end
    
    Note over Detector: Interval timer fires
    Detector->>Detector: onIntervalTimer()
    Detector->>Monitor: Check ejection duration
    
    alt Ejection duration expired
        Detector->>Host: healthFlagClear(FAILED_OUTLIER_CHECK)
        Detector->>Monitor: uneject(now)
        Note over Host: Host returned to LB
        Note over Monitor: Ejection time doubles\nfor next ejection (backoff)
    end
```

### Ejection Duration Backoff

```mermaid
graph LR
    E1["1st ejection: 30s"] --> E2["2nd ejection: 60s"]
    E2 --> E3["3rd ejection: 120s"]
    E3 --> E4["4th ejection: 240s"]
    E4 --> En["... up to max_ejection_time"]
    
    Note["base_ejection_time * 2^(num_ejections - 1)"]
```

## How Health Check and Outlier Detection Interact

```mermaid
graph TD
    subgraph "Active Health Check"
        AHC["Periodic probes\n(HTTP, TCP, gRPC)"]
        AHC --> FAHC["FAILED_ACTIVE_HC flag"]
    end
    
    subgraph "Outlier Detection"
        OD["Monitor request outcomes\n(passive)"]
        OD --> FOC["FAILED_OUTLIER_CHECK flag"]
    end
    
    subgraph "EDS Health Status"
        EDS["Management server reports"]
        EDS --> FEDS["FAILED_EDS_HEALTH flag"]
    end
    
    FAHC --> Health["Host::health()\n= worst of all flags"]
    FOC --> Health
    FEDS --> Health
    
    Health --> LB["LoadBalancer\nskips unhealthy hosts"]
```

## File Catalog

| File | Key Classes | Purpose |
|------|-------------|---------|
| `upstream_impl.h/cc` | `HostImpl`, `HostImplBase`, `HostDescriptionImplBase`, `HostSetImpl`, `PrioritySetImpl`, `ClusterInfoImpl`, `ClusterImplBase` | Host and cluster implementations |
| `health_checker_impl.h/cc` | `HealthCheckerFactory`, `PayloadMatcher` | Health checker factory |
| `health_checker_event_logger.h/cc` | `HealthCheckEventLoggerImpl` | HC event logging |
| `health_discovery_service.h/cc` | `HdsDelegate`, `HdsCluster` | Health Discovery Service |
| `outlier_detection_impl.h/cc` | `DetectorImpl`, `DetectorHostMonitorImpl`, `DetectorConfig`, `SuccessRateAccumulator` | Outlier detection |
| `host_utility.h/cc` | `HostUtility` | Host helpers |
| `load_stats_reporter.h/cc` | `LoadStatsReporter` | Load Reporting Service |
| `default_local_address_selector.h/cc` | `DefaultUpstreamLocalAddressSelector` | Upstream source address |
| `upstream_factory_context_impl.h` | `UpstreamFactoryContextImpl` | Upstream filter factory context |
| `retry_factory.h` | `RetryExtensionFactoryContextImpl` | Retry extension context |

---

**Previous:** [Part 8 — Cluster Manager](08-upstream-cluster-manager.md)  
**Next:** [Part 10 — Supporting Subsystems](10-supporting-subsystems.md)
