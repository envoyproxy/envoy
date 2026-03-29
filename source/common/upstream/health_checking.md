# Health Checking — HC Factory, HDS, Event Logging

**Files:** `source/common/upstream/health_checker_impl.h/.cc`, `health_checker_event_logger.h/.cc`, `health_discovery_service.h/.cc`  
**Namespace:** `Envoy::Upstream`

## Overview

Envoy's health checking system enables active probing of upstream hosts. The subsystem in `source/common/upstream` provides:
1. **HealthCheckerFactory** — creates health checker instances from proto config
2. **HealthCheckerFactoryContextImpl** — context for health checker creation
3. **HealthCheckEventLoggerImpl** — structured event logging for health check transitions
4. **HdsDelegate** — Health Discovery Service client (receives health check specs from management server)
5. **HdsCluster** — special cluster implementation for HDS-managed endpoints

## Class Hierarchy

```mermaid
classDiagram
    class HealthCheckerFactory {
        +create(config, cluster, context): HealthCheckerSharedPtr$
    }

    class HealthCheckerFactoryContextImpl {
        +cluster(): Cluster
        +mainThreadDispatcher(): Dispatcher
        +random(): Random
        +runtime(): Runtime
        +eventLogger(): HealthCheckEventLogger
        +api(): Api
    }

    class HealthCheckEventLoggerImpl {
        +logEject(host, detector, type, enforced)
        +logAddHealthy(host, first_check)
        +logUnhealthy(host, type, first_check)
        +logDegraded(host)
        +logNoLongerDegraded(host)
        -file_: AccessLog::AccessLogFileSharedPtr
    }

    class HdsDelegate {
        +fetchHealthCheckSpecifier(response)
        +onReceiveMessage(message)
        +processMessage(message)
    }

    class HdsCluster {
        +initialize(callback)
        +prioritySet(): PrioritySet
        +info(): ClusterInfoConstSharedPtr
        +healthCheckers(): vector~HealthCheckerPtr~
    }

    HealthCheckerFactory --> HealthCheckerFactoryContextImpl : creates with
    HealthCheckerFactoryContextImpl *-- HealthCheckEventLoggerImpl
    HdsDelegate *-- HdsCluster
```

## Health Checker Creation Flow

```mermaid
sequenceDiagram
    participant Config as Cluster Config
    participant Factory as HealthCheckerFactory
    participant Registry as TypedHealthCheckerFactory Registry
    participant Context as HealthCheckerFactoryContextImpl
    participant HC as HealthChecker Instance

    Config->>Factory: create(health_checks config, cluster, runtime)
    Factory->>Registry: lookup factory by type (HTTP/TCP/gRPC/custom)
    Registry-->>Factory: TypedHealthCheckerFactory
    Factory->>Context: create context (cluster, dispatcher, random)
    Factory->>Registry: factory.createCustomHealthChecker(config, context)
    Registry-->>Factory: HealthCheckerSharedPtr
    Factory-->>Config: attach HC to cluster
```

## Health Check Event Logging

```mermaid
stateDiagram-v2
    [*] --> Healthy
    Healthy --> Unhealthy : logUnhealthy(host, type, first_check)
    Healthy --> Degraded : logDegraded(host)
    Unhealthy --> Healthy : logAddHealthy(host, first_check)
    Degraded --> Healthy : logNoLongerDegraded(host)
    Unhealthy --> Ejected : logEject(host, detector, type, enforced)
    Ejected --> Healthy : logAddHealthy(host, false)
```

### Event Log Fields

| Field | Description |
|-------|-------------|
| `health_checker_type` | HTTP, TCP, gRPC, or custom |
| `host` | Address of the host |
| `cluster_name` | Name of the cluster |
| `eject_unhealthy_event` | Details of unhealthy ejection |
| `add_healthy_event` | Details of return to healthy |
| `health_check_failure_event` | Details of HC failure |
| `degraded_healthy_host` | Host moved to degraded |
| `no_longer_degraded_host` | Host no longer degraded |
| `timestamp` | Time of event |

## Health Discovery Service (HDS)

HDS allows a management server to instruct Envoy to health-check arbitrary endpoints and report results back:

```mermaid
sequenceDiagram
    participant MS as Management Server
    participant HDS as HdsDelegate (Envoy)
    participant HC as HealthCheckers
    participant Hosts as HdsCluster Hosts

    MS->>HDS: HealthCheckSpecifier (gRPC stream)
    HDS->>HDS: processMessage(specifier)

    loop for each cluster_health_check in specifier
        HDS->>HDS: createHdsCluster(spec)
        HDS->>HC: create health checkers per cluster
        HC->>Hosts: begin active health checking
    end

    Note over HDS: After check_interval elapses
    HDS->>HDS: collect EndpointHealthResponses
    HDS->>MS: sendResponse(health_check_request)

    MS->>HDS: updated HealthCheckSpecifier
    HDS->>HDS: processMessage (re-create clusters)
```

## HDS Components

```mermaid
flowchart TD
    subgraph HdsDelegate
        Stream["gRPC BiDi Stream\nto management server"]
        Timer["Response Timer\n(periodic reports)"]
        Clusters["hds_clusters_: vector of HdsCluster"]
    end

    subgraph HdsCluster["HdsCluster"]
        Info["ClusterInfoImpl\n(lightweight cluster info)"]
        PS["PrioritySetImpl\n(hosts from management server)"]
        HCs["Health Checkers\n(HTTP/TCP/gRPC)"]
    end

    Stream -->|receives spec| Clusters
    Clusters --> HdsCluster
    Timer -->|collects results| Stream
    HCs -->|probes| PS
```

## HDS Cluster Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Created : management server sends spec
    Created --> HealthChecking : health checkers started
    HealthChecking --> Reporting : results collected
    Reporting --> HealthChecking : continue checking
    HealthChecking --> Destroyed : new spec replaces cluster
    Destroyed --> [*]
```

## Integration with Cluster System

```mermaid
flowchart LR
    subgraph Normal["Normal Clusters"]
        CM["ClusterManagerImpl"]
        C["ClusterImplBase"]
        HC1["HealthChecker"]
    end

    subgraph HDS["HDS System"]
        HDSD["HdsDelegate"]
        HDSC["HdsCluster"]
        HC2["HealthChecker"]
    end

    CM --> C --> HC1
    HDSD --> HDSC --> HC2

    Note1["Normal clusters: HC results affect LB host selection"]
    Note2["HDS clusters: HC results are reported back to management server"]
```
