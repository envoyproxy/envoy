# Part 1: Core Runtime & Bootstrapping

## Series Navigation

| Part | Topic |
|------|-------|
| **Part 1** | **Core Runtime & Bootstrapping** (this document) |
| Part 2 | [xDS & Dynamic Configuration](./02-xDS-and-Dynamic-Configuration.md) |
| Part 3 | [Istio-to-Envoy Mapping](./03-Istio-to-Envoy-Mapping.md) |
| Part 4 | [EnvoyFilter, Sidecar vs Gateway](./04-EnvoyFilter-Sidecar-vs-Gateway.md) |

---

## Overview

This document covers how Envoy starts up — from the `main()` entry point through bootstrap configuration loading, multi-phase initialization, and reaching a state where the proxy is ready to serve traffic.

---

## 1. Entry Point and Startup Sequence

```mermaid
flowchart TD
    Main["main(argc, argv)\nsource/exe/main.cc"] --> MC["MainCommon::main()"]
    MC --> MCBase["MainCommonBase constructor"]
    MCBase --> SMBI["StrippedMainBase::init()"]
    SMBI --> PW["ProcessWide\n(libevent, nghttp2, protobuf)"]
    SMBI --> Options["OptionsImpl\n(parse CLI flags)"]
    SMBI --> HotRestart["HotRestarter\n(domain sockets for live upgrades)"]
    SMBI --> Stats["StatsStore\n(ThreadLocalStoreImpl)"]
    SMBI --> TLS["ThreadLocal::InstanceImpl"]
    SMBI --> CreateServer["createFunction()\n→ InstanceImpl"]

    CreateServer --> Init["server->initialize(\n  local_address,\n  component_factory)"]
    Init --> Run["server->run()"]
```

### Key Files

| File | Role |
|------|------|
| `source/exe/main.cc` | Process entry point |
| `source/exe/main_common.h/.cc` | `MainCommon`, `MainCommonBase` — glue between CLI and server |
| `source/exe/stripped_main_base.h/.cc` | Process-wide singletons, hot restart, stats store |
| `source/server/server.h/.cc` | `InstanceBase` — the actual Envoy server |
| `source/server/instance_impl.h/.cc` | `InstanceImpl` — production server with overload manager, guard dog, HDS |

---

## 2. Bootstrap Configuration Loading

### Configuration Sources

Envoy accepts configuration from three sources (merged in priority order):

```mermaid
flowchart LR
    File["--config-path\n(JSON/YAML/proto file)"] --> Merge["loadBootstrapConfig()"]
    YAML["--config-yaml\n(inline YAML string)"] --> Merge
    Proto["configProto()\n(programmatic override)"] --> Merge
    Merge --> Validate["MessageUtil::validate()\n(PGV + custom validation)"]
    Validate --> Bootstrap["envoy.config.bootstrap.v3.Bootstrap"]
```

### Bootstrap Proto Structure

```mermaid
mindmap
  root((Bootstrap))
    node
      id: local cluster info
      cluster: local cluster name
      zone: availability zone
    static_resources
      listeners: pre-configured listeners
      clusters: pre-configured clusters
      secrets: pre-configured TLS secrets
    dynamic_resources
      lds_config: LDS ConfigSource
      cds_config: CDS ConfigSource
      ads_config: ADS ApiConfigSource
    admin
      address: admin listener bind
      access_log: admin access log
    cluster_manager
      local_cluster_name: for zone-aware LB
      outlier_detection: global defaults
    stats_config
      stats_tags: custom tag extractors
      stats_matcher: inclusion/exclusion
    tracing
      provider: tracing backend config
    overload_manager
      resource_monitors: CPU, memory, etc.
      actions: per-resource overload actions
    hds_config
      Health Discovery Service config
    layered_runtime
      layers: runtime override layers
```

### `loadBootstrapConfig` Implementation

```mermaid
sequenceDiagram
    participant Main as InstanceBase::initialize
    participant Util as InstanceUtil
    participant FS as Filesystem
    participant PB as Protobuf

    Main->>Util: loadBootstrapConfig(bootstrap, options)

    alt --config-path provided
        Util->>FS: read config file
        FS-->>Util: file contents
        Util->>PB: MessageUtil::loadFromFile(path, bootstrap)
    end

    alt --config-yaml provided
        Util->>PB: MessageUtil::loadFromYaml(yaml, bootstrap)
        Note over Util: Merges into existing bootstrap
    end

    Util->>PB: MessageUtil::validate(bootstrap)
    Util-->>Main: validated Bootstrap proto
```

---

## 3. Server Initialization Order

This is the most critical sequence — it determines what is available at each phase:

```mermaid
flowchart TD
    subgraph Phase1["Phase 1: Foundation"]
        direction TB
        P1A["Load bootstrap config"]
        P1B["Create regex engine"]
        P1C["Stats: tag producer, matcher"]
        P1D["LocalInfo (node ID, cluster, zone)"]
        P1E["Configuration::InitialImpl\n(parse bootstrap static resources)"]
    end

    subgraph Phase2["Phase 2: Runtime"]
        direction TB
        P2A["Runtime loader\n(layered_runtime layers)"]
        P2B["Admin listener\n(if admin config present)"]
        P2C["SSL context manager"]
        P2D["HTTP server properties cache"]
    end

    subgraph Phase3["Phase 3: xDS & Clusters"]
        direction TB
        P3A["XdsManagerImpl\n(ADS mux, subscription factory)"]
        P3B["ClusterManager\n(ProdClusterManagerFactory)"]
        P3C["config_.initialize()\n(parse bootstrap, create primary clusters)"]
        P3D["LDS API\n(if lds_config present)"]
        P3E["RTDS init\n(runtime via xDS)"]
    end

    subgraph Phase4["Phase 4: Workers"]
        direction TB
        P4A["onClusterManagerPrimaryInitializationComplete\n→ RTDS → secondary clusters"]
        P4B["onRuntimeReady\n→ HDS (if configured)"]
        P4C["startWorkers()\n→ worker threads, bind listeners"]
    end

    Phase1 --> Phase2 --> Phase3 --> Phase4

    style Phase1 fill:#e1f5fe
    style Phase2 fill:#fff3e0
    style Phase3 fill:#e8f5e9
    style Phase4 fill:#fce4ec
```

### Detailed Initialization Sequence

```mermaid
sequenceDiagram
    participant IB as InstanceBase
    participant Config as Configuration::InitialImpl
    participant XDS as XdsManagerImpl
    participant CM as ClusterManager
    participant LDS as LdsApiImpl
    participant Workers as WorkerImpl

    IB->>IB: loadBootstrapConfig()
    IB->>IB: create regex engine
    IB->>IB: create stats (tags, matcher)
    IB->>IB: create LocalInfo
    IB->>Config: parse bootstrap (static resources)
    IB->>IB: create Runtime loader
    IB->>IB: create Admin listener
    IB->>IB: create SSL context manager
    IB->>XDS: create XdsManagerImpl
    XDS->>XDS: initialize ADS connections
    IB->>CM: ProdClusterManagerFactory.clusterManagerFromProto()
    CM->>CM: load static clusters
    IB->>Config: initialize() - create primary clusters
    IB->>LDS: createLdsApi() if lds_config
    IB->>IB: RTDS init

    Note over IB: Wait for primary clusters to initialize
    IB->>CM: onClusterManagerPrimaryInitializationComplete()
    CM->>CM: start secondary clusters
    
    Note over IB: Wait for CDS clusters to initialize
    IB->>IB: onRuntimeReady()
    IB->>Workers: startWorkers()
    Workers->>Workers: bind listeners on all workers
    
    Note over IB: Server is now ready for traffic
```

---

## 4. InstanceBase — The Server Object

```mermaid
classDiagram
    class Instance {
        <<interface>>
        +initialize(local_address, component_factory)
        +run()
        +shutdown()
        +clusterManager(): ClusterManager
        +listenerManager(): ListenerManager
        +options(): Options
        +localInfo(): LocalInfo
    }

    class InstanceBase {
        -dispatcher_: Event::DispatcherPtr
        -cluster_manager_: ClusterManagerPtr
        -listener_manager_: ListenerManagerPtr
        -xds_manager_: XdsManagerPtr
        -runtime_: Runtime::LoaderPtr
        -admin_: AdminPtr
        -overload_manager_: OverloadManagerPtr
        -access_log_manager_: AccessLogManagerImpl
        -init_manager_: Init::ManagerImpl
        +initialize(local_address, component_factory)
        +run()
    }

    class InstanceImpl {
        -heap_shrinker_: Memory::HeapShrinker
        -overload_manager_impl_: OverloadManagerImpl
        -guard_dog_: GuardDogPtr
        -hds_delegate_: HdsDelegatePtr
    }

    Instance <|-- InstanceBase
    InstanceBase <|-- InstanceImpl
```

### What `InstanceImpl` Adds Over `InstanceBase`

| Feature | Purpose |
|---------|---------|
| `OverloadManagerImpl` | Resource monitors (memory, CPU), overload actions |
| `GuardDog` | Watchdog threads, detects event loop stalls |
| `HeapShrinker` | Releases memory when overloaded |
| `HdsDelegate` | Health Discovery Service client |

---

## 5. Configuration::InitialImpl

Parses the bootstrap into runtime structures:

```mermaid
flowchart TD
    Bootstrap["Bootstrap Proto"] --> InitialImpl["Configuration::InitialImpl"]
    
    InitialImpl --> StaticClusters["Static Clusters\n(from static_resources.clusters)"]
    InitialImpl --> StaticListeners["Static Listeners\n(from static_resources.listeners)"]
    InitialImpl --> StaticSecrets["Static Secrets\n(from static_resources.secrets)"]
    InitialImpl --> CMConfig["ClusterManager Config\n(from cluster_manager)"]
    InitialImpl --> Tracing["Tracing Config\n(from tracing)"]
    InitialImpl --> StatsConfig["Stats Sinks\n(from stats_sinks)"]
```

---

## 6. Process-Wide Singletons

```mermaid
flowchart TD
    PW["ProcessWide (constructor)"] --> LE["Event::Libevent::Global::initialize()"]
    PW --> NH["nghttp2 callbacks"]
    PW --> PB["Protobuf descriptor pool"]
    PW --> RE["Random: global PRNG seed"]
    
    GG["GoogleGrpcContext"] --> GRPC["gRPC library init\n(if using Google gRPC)"]
```

---

## 7. Hot Restart

Hot restart allows zero-downtime upgrades by transferring state between old and new Envoy processes:

```mermaid
sequenceDiagram
    participant Old as Old Envoy Process
    participant New as New Envoy Process
    participant OS as Operating System

    Note over New: Starts with --restart-epoch N
    New->>Old: Request listen sockets (via domain socket)
    Old-->>New: Transfer socket FDs
    New->>New: Bind to same ports using received FDs
    New->>New: Initialize and start workers

    Note over Old,New: Both processes serve traffic briefly
    Old->>Old: Begin draining
    Old->>Old: Close listeners
    Old->>New: Transfer stats (counters, gauges)
    
    Note over Old: Drain period expires
    Old->>Old: Shutdown
    Note over New: New process now sole owner
```

### Hot Restart Socket Inheritance

```mermaid
flowchart TD
    subgraph OldProcess["Old Process (epoch N-1)"]
        OL["Listeners bound to\n0.0.0.0:80, 0.0.0.0:443"]
        OS["Socket FDs: 5, 6"]
    end

    subgraph HotRestart["Hot Restart Protocol"]
        Req["GetListenSocketRequest\n(address, worker_index)"]
        Resp["GetListenSocketReply\n(socket FD via SCM_RIGHTS)"]
    end

    subgraph NewProcess["New Process (epoch N)"]
        NL["Reuse FDs 5, 6\n(no port conflict)"]
    end

    OL --> OS
    OS -->|"domain socket"| HotRestart
    HotRestart --> NL
```

---

## 8. Config Validation Mode

When Envoy runs with `--mode validate`, it performs full config parsing without binding ports:

```mermaid
flowchart TD
    CLI["envoy --mode validate --config-path envoy.yaml"] --> Parse["Parse options"]
    Parse --> VInst["Create ValidationInstance\n(instead of InstanceImpl)"]
    VInst --> Load["loadBootstrapConfig()"]
    Load --> Init["initialize()\n(same as production, but with stubs)"]
    Init --> Check{"Config valid?"}
    Check -->|Yes| Exit0["Exit 0\n(config OK)"]
    Check -->|No| Exit1["Exit 1\n(config error logged)"]
```

### ValidationInstance Stubs

| Component | Production | Validation |
|-----------|-----------|------------|
| ClusterManager | `ClusterManagerImpl` | `ValidationClusterManager` |
| Admin | Full admin server | `ValidationAdmin` (no-op) |
| ListenerManager | Binds ports | `ValidationListenerManager` (no-op) |
| Health Checks | Active probing | Skipped |
| Hot Restart | Socket transfer | Skipped |

---

## 9. Runtime Layers

Envoy's runtime system provides dynamic feature flags and configuration overrides:

```mermaid
flowchart TD
    subgraph Layers["Runtime Layers (highest priority last)"]
        L1["Layer 0: Static (bootstrap defaults)"]
        L2["Layer 1: Disk (file-based overrides)"]
        L3["Layer 2: Admin (admin API overrides)"]
        L4["Layer 3: RTDS (xDS-based overrides)"]
    end

    Lookup["runtime.snapshot().get(key)"] --> L4
    L4 -->|miss| L3
    L3 -->|miss| L2
    L2 -->|miss| L1
    L1 -->|miss| Default["Use coded default"]
```

---

## 10. Init::Manager — Initialization Coordination

The `Init::Manager` coordinates initialization across components that need asynchronous setup:

```mermaid
sequenceDiagram
    participant IM as Init::Manager
    participant CDS as CDS Init Target
    participant LDS as LDS Init Target
    participant EDS as EDS Init Target

    IM->>CDS: initialize()
    IM->>LDS: initialize()
    Note over IM: Wait for all targets

    CDS-->>IM: ready (first CDS response)
    EDS-->>IM: ready (first EDS response)
    LDS-->>IM: ready (first LDS response)

    Note over IM: All targets ready
    IM->>IM: mark initialized
    IM-->>Server: initialization complete callback
```

---

## 11. Full Bootstrap-to-Ready Timeline

```mermaid
gantt
    title Envoy Startup Timeline
    dateFormat X
    axisFormat %s

    section Process Init
    ProcessWide singletons       :0, 1
    Parse CLI options            :1, 2
    Hot restarter setup          :2, 3
    Stats store creation         :3, 4

    section Server Init
    Load bootstrap config        :4, 5
    Create runtime               :5, 6
    Create admin                 :6, 7
    Create XdsManager            :7, 8

    section Cluster Init
    Create ClusterManager        :8, 10
    Static clusters init         :10, 12
    CDS subscription start       :12, 13
    Wait for CDS response        :13, 16
    CDS clusters warming         :16, 19

    section Listener Init
    LDS subscription start       :13, 14
    Wait for LDS response        :14, 17
    RDS subscriptions start      :17, 18
    Wait for RDS responses       :18, 20

    section Workers
    Start worker threads         :20, 21
    Bind listeners               :21, 22
    Server ready                 :22, 23
```
