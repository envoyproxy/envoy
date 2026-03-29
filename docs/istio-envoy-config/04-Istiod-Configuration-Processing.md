# Part 4: Istiod Configuration Processing

## Table of Contents
1. [Introduction](#introduction)
2. [Istiod Architecture](#istiod-architecture)
3. [Configuration Watch and Cache](#configuration-watch-and-cache)
4. [Configuration Validation](#configuration-validation)
5. [Configuration Translation Pipeline](#configuration-translation-pipeline)
6. [Push Context and Generation](#push-context-and-generation)
7. [xDS Generation for Each Resource Type](#xds-generation-for-each-resource-type)
8. [Optimization and Caching](#optimization-and-caching)

## Introduction

Istiod is the control plane component that processes Istio configurations and generates Envoy xDS configurations. This document explains how Istiod transforms high-level Istio CRDs into low-level Envoy configurations.

## Istiod Architecture

### Istiod Components

```mermaid
graph TB
    subgraph "Istiod Process"
        direction TB

        subgraph "Configuration Input"
            K8S_WATCH[Kubernetes Watchers]
            K8S_WATCH --> WATCH_SVC[Service Watcher]
            K8S_WATCH --> WATCH_EP[Endpoint Watcher]
            K8S_WATCH --> WATCH_POD[Pod Watcher]
            K8S_WATCH --> WATCH_CRD[CRD Watchers<br/>VS, DR, GW, SE]
        end

        subgraph "Configuration Processing"
            CACHE[Config Cache]
            VALIDATE[Config Validator]
            TRANSLATE[Config Translator]
            MERGE[Config Merger]
        end

        subgraph "Service Discovery"
            REGISTRY[Service Registry]
            AGGREGATE[Service Aggregator]
            REGISTRY --> K8S_REG[Kubernetes Registry]
            REGISTRY --> CONSUL[Consul Registry]
            REGISTRY --> MC[Multicluster Registry]
        end

        subgraph "xDS Server"
            XDS[ADS Server]
            GENERATOR[Config Generator]
            PUSH[Push Queue]
            CONN_MGR[Connection Manager]
        end

        subgraph "Security"
            CA[Certificate Authority]
            SDS_SERVER[SDS Server]
        end
    end

    WATCH_SVC --> CACHE
    WATCH_EP --> CACHE
    WATCH_POD --> CACHE
    WATCH_CRD --> CACHE

    CACHE --> VALIDATE
    VALIDATE --> TRANSLATE
    TRANSLATE --> MERGE
    MERGE --> GENERATOR

    REGISTRY --> GENERATOR
    AGGREGATE --> REGISTRY

    GENERATOR --> PUSH
    PUSH --> CONN_MGR
    CONN_MGR --> XDS

    CA --> SDS_SERVER
    SDS_SERVER --> XDS

    style ISTIOD fill:#326CE5,color:#fff
    style XDS fill:#E74C3C,color:#fff
    style GENERATOR fill:#F39C12,color:#fff
```

### Core Data Structures

```mermaid
classDiagram
    class IstioController {
        +ConfigStore configStore
        +ServiceDiscovery serviceDiscovery
        +PushContext pushContext
        +XDSServer xdsServer
        +startWatch()
        +handleEvent()
    }

    class ConfigStore {
        +List(type, namespace) []Config
        +Get(type, name, namespace) Config
        +Create(config) error
        +Update(config) error
        +Delete(type, name, namespace) error
    }

    class ServiceDiscovery {
        +Services() []*Service
        +GetService(hostname) *Service
        +InstancesByPort(*Service, port) []*ServiceInstance
        +GetProxyServiceInstances(*Proxy) []*ServiceInstance
    }

    class PushContext {
        +Services []*Service
        +VirtualServices []*VirtualService
        +DestinationRules []*DestinationRule
        +Gateways []*Gateway
        +ServiceIndex map[hostname]*Service
        +initContext()
        +generateLDS(proxy) []*Listener
        +generateRDS(proxy) []*RouteConfiguration
        +generateCDS(proxy) []*Cluster
        +generateEDS(proxy) []*ClusterLoadAssignment
    }

    class XDSServer {
        +Generators map[string]Generator
        +ConfigGenerator *ConfigGeneratorImpl
        +DiscoveryServer *DiscoveryServer
        +Push(req PushRequest)
        +StreamAggregatedResources(stream)
    }

    IstioController --> ConfigStore
    IstioController --> ServiceDiscovery
    IstioController --> PushContext
    IstioController --> XDSServer

    XDSServer --> PushContext: reads
```

## Configuration Watch and Cache

### Kubernetes Watch Mechanism

```mermaid
sequenceDiagram
    participant K8S as Kubernetes API
    participant W as Istiod Watcher
    participant C as Config Cache
    participant P as Push Queue
    participant E as Envoy Proxies

    Note over K8S,E: Initial Sync

    W->>K8S: Watch(VirtualService)
    W->>K8S: Watch(DestinationRule)
    W->>K8S: Watch(Gateway)
    W->>K8S: Watch(Service)
    W->>K8S: Watch(Endpoints)

    K8S-->>W: Existing resources (LIST)
    W->>C: Initialize cache

    Note over K8S,E: Runtime Updates

    K8S->>W: Event: VirtualService ADDED
    W->>C: Update cache
    C->>P: Trigger push<br/>(RDS affected)

    P->>E: Push RDS updates<br/>to affected proxies

    K8S->>W: Event: Endpoints MODIFIED
    W->>C: Update cache
    C->>P: Trigger push<br/>(EDS affected)

    P->>E: Push EDS updates<br/>to affected proxies

    K8S->>W: Event: DestinationRule DELETED
    W->>C: Update cache
    C->>P: Trigger push<br/>(CDS affected)

    P->>E: Push CDS updates<br/>to affected proxies
```

### Config Cache Structure

```mermaid
graph TB
    subgraph "Config Cache"
        direction TB

        CACHE[In-Memory Cache]

        CACHE --> VS_CACHE[VirtualService Cache<br/>Map: namespace/name → VS]
        CACHE --> DR_CACHE[DestinationRule Cache<br/>Map: namespace/name → DR]
        CACHE --> GW_CACHE[Gateway Cache<br/>Map: namespace/name → GW]
        CACHE --> SE_CACHE[ServiceEntry Cache<br/>Map: namespace/name → SE]

        CACHE --> SVC_CACHE[Service Cache<br/>Map: hostname → Service]
        CACHE --> EP_CACHE[Endpoint Cache<br/>Map: service → Endpoints]

        CACHE --> INDEX[Indexes]
        INDEX --> HOST_INDEX[Hostname → Config]
        INDEX --> LABEL_INDEX[Label Selector → Services]
        INDEX --> GW_INDEX[Gateway → VirtualServices]
    end

    subgraph "Cache Operations"
        ADD[Add/Update]
        DELETE[Delete]
        LIST[List by Type]
        GET[Get by Key]
        QUERY[Query with Filters]
    end

    ADD --> CACHE
    DELETE --> CACHE
    LIST --> CACHE
    GET --> CACHE
    QUERY --> INDEX

    style CACHE fill:#3498DB,color:#fff
    style INDEX fill:#F39C12,color:#fff
```

### Event Debouncing and Batching

```mermaid
stateDiagram-v2
    [*] --> Idle

    Idle --> Buffering: Config change event

    state Buffering {
        [*] --> Accumulating
        Accumulating --> Accumulating: More events<br/>(within debounce window)
        Accumulating --> Ready: Debounce timeout
    }

    Buffering --> Idle: No events
    Buffering --> Processing: Ready signal

    state Processing {
        [*] --> Computing
        Computing --> Generating: Push context built
        Generating --> Pushing: xDS generated
        Pushing --> [*]: Pushed to proxies
    }

    Processing --> Idle: Complete

    note right of Buffering
        Debounce window: 100ms
        Avoids push storms
        Batches multiple changes
    end note

    note right of Processing
        Full push vs partial push
        Affected proxies calculated
        Incremental xDS used
    end note
```

## Configuration Validation

### Validation Pipeline

```mermaid
graph TB
    subgraph "Validation Stages"
        INPUT[Configuration Input]

        INPUT --> STAGE1[Stage 1: Schema Validation]
        STAGE1 --> STAGE2[Stage 2: Semantic Validation]
        STAGE2 --> STAGE3[Stage 3: Cross-Resource Validation]
        STAGE3 --> STAGE4[Stage 4: Runtime Validation]

        STAGE1 --> S1_CHECKS{Checks}
        S1_CHECKS --> S1_1[Protobuf schema valid?]
        S1_CHECKS --> S1_2[Required fields present?]
        S1_CHECKS --> S1_3[Field types correct?]

        STAGE2 --> S2_CHECKS{Checks}
        S2_CHECKS --> S2_1[Valid hostnames?]
        S2_CHECKS --> S2_2[Valid regex patterns?]
        S2_CHECKS --> S2_3[Valid port numbers?]
        S2_CHECKS --> S2_4[Valid weights sum to 100?]

        STAGE3 --> S3_CHECKS{Checks}
        S3_CHECKS --> S3_1[Referenced services exist?]
        S3_CHECKS --> S3_2[Referenced gateways exist?]
        S3_CHECKS --> S3_3[Subset labels match?]
        S3_CHECKS --> S3_4[No circular references?]

        STAGE4 --> S4_CHECKS{Checks}
        S4_CHECKS --> S4_1[Conflicts with other configs?]
        S4_CHECKS --> S4_2[Overlapping match conditions?]
        S4_CHECKS --> S4_3[Ambiguous routing rules?]

        S1_1 --> RESULT
        S1_2 --> RESULT
        S1_3 --> RESULT
        S2_1 --> RESULT
        S2_2 --> RESULT
        S2_3 --> RESULT
        S2_4 --> RESULT
        S3_1 --> RESULT
        S3_2 --> RESULT
        S3_3 --> RESULT
        S3_4 --> RESULT
        S4_1 --> RESULT
        S4_2 --> RESULT
        S4_3 --> RESULT

        RESULT{All Pass?}
        RESULT -->|Yes| ACCEPT[Accept Configuration]
        RESULT -->|No| REJECT[Reject with Errors]
    end

    ACCEPT --> CACHE[Add to Cache]
    REJECT --> LOG[Log Errors & Alert]

    style ACCEPT fill:#27AE60,color:#fff
    style REJECT fill:#E74C3C,color:#fff
```

### Validation Example: VirtualService

```mermaid
graph TB
    VS[VirtualService Validation]

    VS --> V1[Check hosts field]
    V1 --> V1A{Valid DNS names?}
    V1A -->|No| ERROR1[Error: Invalid hostname]
    V1A -->|Yes| V2

    V2[Check http routes]
    V2 --> V2A{Routes defined?}
    V2A -->|No| WARN1[Warning: No routes]
    V2A -->|Yes| V3

    V3[Check match conditions]
    V3 --> V3A{Valid URI/headers?}
    V3A -->|No| ERROR2[Error: Invalid match]
    V3A -->|Yes| V4

    V4[Check destinations]
    V4 --> V4A{Service exists?}
    V4A -->|No| ERROR3[Error: Unknown service]
    V4A -->|Yes| V5

    V5[Check weights]
    V5 --> V5A{Sum to 100?}
    V5A -->|No| ERROR4[Error: Invalid weights]
    V5A -->|Yes| V6

    V6[Check subsets]
    V6 --> V6A{DestinationRule defines subset?}
    V6A -->|No| ERROR5[Error: Unknown subset]
    V6A -->|Yes| VALID[Valid Configuration]

    ERROR1 --> REJECT[Reject]
    ERROR2 --> REJECT
    ERROR3 --> REJECT
    ERROR4 --> REJECT
    ERROR5 --> REJECT
    WARN1 --> VALID

    VALID --> ACCEPT[Accept & Cache]

    style VALID fill:#27AE60,color:#fff
    style REJECT fill:#E74C3C,color:#fff
    style WARN1 fill:#F39C12,color:#fff
```

## Configuration Translation Pipeline

### Translation Flow

```mermaid
graph TB
    subgraph "High-Level Istio Config"
        VS[VirtualService]
        DR[DestinationRule]
        GW[Gateway]
        SE[ServiceEntry]
    end

    subgraph "Translation Layer"
        TRANS[Config Translator]

        TRANS --> VS_TRANS[VS Translator]
        TRANS --> DR_TRANS[DR Translator]
        TRANS --> GW_TRANS[GW Translator]
        TRANS --> SE_TRANS[SE Translator]

        VS_TRANS --> BUILD_ROUTES[Build Route Configs]
        DR_TRANS --> BUILD_CLUSTERS[Build Cluster Policies]
        GW_TRANS --> BUILD_LISTENERS[Build Gateway Listeners]
        SE_TRANS --> BUILD_ENDPOINTS[Build Service Endpoints]
    end

    subgraph "Intermediate Model"
        MODEL[Istio Internal Model]

        MODEL --> VIRTUAL_HOSTS[VirtualHosts]
        MODEL --> ROUTES[Routes]
        MODEL --> CLUSTER_CONFIGS[ClusterConfigs]
        MODEL --> LISTENER_CONFIGS[ListenerConfigs]
    end

    subgraph "Low-Level Envoy Config"
        LDS[Listeners]
        RDS[RouteConfigurations]
        CDS[Clusters]
        EDS[ClusterLoadAssignments]
    end

    VS --> VS_TRANS
    DR --> DR_TRANS
    GW --> GW_TRANS
    SE --> SE_TRANS

    BUILD_ROUTES --> VIRTUAL_HOSTS
    BUILD_ROUTES --> ROUTES
    BUILD_CLUSTERS --> CLUSTER_CONFIGS
    BUILD_LISTENERS --> LISTENER_CONFIGS
    BUILD_ENDPOINTS --> CLUSTER_CONFIGS

    VIRTUAL_HOSTS --> RDS
    ROUTES --> RDS
    CLUSTER_CONFIGS --> CDS
    LISTENER_CONFIGS --> LDS
    CLUSTER_CONFIGS --> EDS

    style TRANS fill:#3498DB,color:#fff
    style MODEL fill:#F39C12,color:#fff
    style LDS fill:#9B59B6,color:#fff
    style RDS fill:#E74C3C,color:#fff
    style CDS fill:#27AE60,color:#fff
```

### VirtualService to RDS Translation

```mermaid
graph LR
    subgraph "VirtualService"
        VS_INPUT["hosts: ['reviews.default.svc.cluster.local']
        http:
        - match:
          - uri:
              prefix: '/v1'
          route:
          - destination:
              host: reviews
              subset: v1
            weight: 90
          - destination:
              host: reviews
              subset: v2
            weight: 10"]
    end

    subgraph "Translation Process"
        PARSE[Parse VS Config]
        BUILD_VH[Build VirtualHost]
        BUILD_ROUTE[Build Routes]
        BUILD_WEIGHTED[Build WeightedClusters]
        APPLY_POLICIES[Apply Policies<br/>retry, timeout, etc.]
    end

    subgraph "RDS Output"
        RDS_OUTPUT["RouteConfiguration:
        virtual_hosts:
        - name: reviews.default.svc.cluster.local:80
          domains: ['reviews.default.svc.cluster.local', ...]
          routes:
          - match:
              prefix: '/v1'
            route:
              weighted_clusters:
                clusters:
                - name: outbound|80|v1|reviews...
                  weight: 90
                - name: outbound|80|v2|reviews...
                  weight: 10
              timeout: 15s
              retry_policy: {...}"]
    end

    VS_INPUT --> PARSE
    PARSE --> BUILD_VH
    BUILD_VH --> BUILD_ROUTE
    BUILD_ROUTE --> BUILD_WEIGHTED
    BUILD_WEIGHTED --> APPLY_POLICIES
    APPLY_POLICIES --> RDS_OUTPUT

    style PARSE fill:#3498DB,color:#fff
    style RDS_OUTPUT fill:#E74C3C,color:#fff
```

### DestinationRule to CDS Translation

```mermaid
graph LR
    subgraph "DestinationRule"
        DR_INPUT["host: reviews.default.svc.cluster.local
        trafficPolicy:
          connectionPool:
            tcp:
              maxConnections: 100
            http:
              http1MaxPendingRequests: 1
          loadBalancer:
            simple: LEAST_CONN
          outlierDetection:
            consecutiveErrors: 5
            interval: 30s
        subsets:
        - name: v1
          labels:
            version: v1
        - name: v2
          labels:
            version: v2"]
    end

    subgraph "Translation Process"
        PARSE_DR[Parse DR Config]
        FOR_EACH_SUBSET[For Each Subset]
        BUILD_CLUSTER[Build Cluster]
        APPLY_TRAFFIC[Apply Traffic Policy]
        APPLY_LB[Apply Load Balancer]
        APPLY_CB[Apply Circuit Breaker]
        APPLY_OD[Apply Outlier Detection]
    end

    subgraph "CDS Output"
        CDS_OUTPUT["Cluster: outbound|80|v1|reviews...
        type: EDS
        lb_policy: LEAST_REQUEST
        circuit_breakers:
          thresholds:
          - max_connections: 100
            max_pending_requests: 1
        outlier_detection:
          consecutive_5xx: 5
          interval: 30s
          base_ejection_time: 30s

        Cluster: outbound|80|v2|reviews...
        (similar config)"]
    end

    DR_INPUT --> PARSE_DR
    PARSE_DR --> FOR_EACH_SUBSET
    FOR_EACH_SUBSET --> BUILD_CLUSTER
    BUILD_CLUSTER --> APPLY_TRAFFIC
    APPLY_TRAFFIC --> APPLY_LB
    APPLY_LB --> APPLY_CB
    APPLY_CB --> APPLY_OD
    APPLY_OD --> CDS_OUTPUT

    style PARSE_DR fill:#3498DB,color:#fff
    style CDS_OUTPUT fill:#27AE60,color:#fff
```

## Push Context and Generation

### Push Context Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Empty: Istiod starts

    Empty --> Building: Config change detected

    state Building {
        [*] --> LoadServices
        LoadServices --> LoadVirtualServices
        LoadVirtualServices --> LoadDestinationRules
        LoadDestinationRules --> LoadGateways
        LoadGateways --> LoadServiceEntries
        LoadServiceEntries --> BuildIndexes
        BuildIndexes --> ValidateConsistency
        ValidateConsistency --> [*]
    }

    Building --> Ready: Build complete

    state Ready {
        [*] --> Serving
        Serving --> Serving: xDS requests served
    }

    Ready --> Building: Config change detected

    note right of Building
        - Load all configs from cache
        - Build indexes for fast lookup
        - Validate cross-references
        - Usually takes 100-500ms
    end note

    note right of Ready
        - Immutable snapshot
        - Thread-safe reads
        - Fast xDS generation
        - No locks needed
    end note
```

### Push Context Data Structure

```mermaid
classDiagram
    class PushContext {
        +ServiceIndex map[hostname]*Service
        +VirtualServices []*VirtualService
        +VirtualServiceIndex map[hostname][]*VirtualService
        +DestinationRules []*DestinationRule
        +DestinationRuleIndex map[hostname]*DestinationRule
        +Gateways []*Gateway
        +GatewayIndex map[name]*Gateway
        +ServiceEntries []*ServiceEntry
        +Sidecars []*Sidecar

        +GetService(hostname) *Service
        +VirtualServicesForGateway(gateway) []*VirtualService
        +DestinationRule(hostname, subset) *DestinationRule
        +initServiceRegistry()
        +initVirtualServices()
        +initDestinationRules()
    }

    class Service {
        +Hostname string
        +Address string
        +Ports []*Port
        +Attributes map[string]string
        +Resolution ServiceResolution
        +MeshExternal bool
    }

    class VirtualService {
        +Hosts []string
        +Gateways []string
        +HTTP []*HTTPRoute
        +TLS []*TLSRoute
        +TCP []*TCPRoute
    }

    class DestinationRule {
        +Host string
        +TrafficPolicy *TrafficPolicy
        +Subsets []*Subset
    }

    PushContext --> Service: indexes
    PushContext --> VirtualService: contains
    PushContext --> DestinationRule: contains

    note for PushContext "Immutable snapshot\nRebuilt on config changes\nThread-safe for reads"
```

### Push Request and Triggering

```mermaid
sequenceDiagram
    participant W as Config Watcher
    participant D as Debouncer
    participant B as PushContext Builder
    participant Q as Push Queue
    participant G as xDS Generator
    participant C as Connection Manager
    participant E as Envoy Proxies

    Note over W,E: Config Change Event

    W->>D: Config changed<br/>(VirtualService updated)

    Note over D: Accumulate events<br/>Wait 100ms

    D->>D: Debounce timeout

    D->>B: Build new PushContext

    Note over B: Build context:<br/>- Load all configs<br/>- Build indexes<br/>- Validate

    B->>Q: New PushContext ready

    Q->>Q: Calculate affected proxies<br/>Calculate affected types

    Note over Q: Full push vs partial:<br/>- LDS/CDS: Full push<br/>- RDS/EDS: Partial push

    Q->>C: Push request<br/>affected_proxies: [proxy1, proxy2]<br/>config_types: [RDS]

    par Push to all affected proxies
        C->>G: Generate RDS for proxy1
        G-->>C: RDS config
        C->>E: DiscoveryResponse (RDS)<br/>to proxy1

        C->>G: Generate RDS for proxy2
        G-->>C: RDS config
        C->>E: DiscoveryResponse (RDS)<br/>to proxy2
    end

    E->>C: ACK
    E->>C: ACK
```

## xDS Generation for Each Resource Type

### LDS Generation

```mermaid
graph TB
    START[Generate LDS for Proxy]

    START --> PROXY_TYPE{Proxy Type?}

    PROXY_TYPE -->|Sidecar| SIDECAR[Generate Sidecar Listeners]
    PROXY_TYPE -->|Gateway| GATEWAY[Generate Gateway Listeners]

    SIDECAR --> INBOUND[Inbound Listeners<br/>Per service port]
    SIDECAR --> OUTBOUND[Outbound Listeners<br/>Per service port<br/>+ virtual outbound]

    INBOUND --> APPLY_AUTHZ[Apply AuthZ policies]
    OUTBOUND --> APPLY_SIDECAR[Apply Sidecar scope]

    GATEWAY --> GW_SERVERS[For each Gateway.Server]
    GW_SERVERS --> GW_LISTENER[Create Listener<br/>Port + Protocol + TLS]

    APPLY_AUTHZ --> BUILD_FILTERS[Build Filter Chains]
    APPLY_SIDECAR --> BUILD_FILTERS
    GW_LISTENER --> BUILD_FILTERS

    BUILD_FILTERS --> HCM[HTTP Connection Manager<br/>RDS reference]
    BUILD_FILTERS --> TCP[TCP Proxy<br/>CDS reference]
    BUILD_FILTERS --> TLS[TLS Inspector]

    HCM --> RESULT[Listener List]
    TCP --> RESULT
    TLS --> RESULT

    style RESULT fill:#3498DB,color:#fff
```

### RDS Generation

```mermaid
graph TB
    START[Generate RDS for Proxy]

    START --> GET_LISTENERS[Get Listeners with RDS]

    GET_LISTENERS --> FOR_EACH[For Each Listener]

    FOR_EACH --> GET_VS[Get VirtualServices<br/>matching listener]

    GET_VS --> FILTER_VS{Filter by:<br/>- Gateway<br/>- Host<br/>- Namespace}

    FILTER_VS --> SORT_VS[Sort by:<br/>1. Specificity<br/>2. Creation time]

    SORT_VS --> BUILD_VH[Build VirtualHosts]

    BUILD_VH --> FOR_EACH_VS[For Each VirtualService]

    FOR_EACH_VS --> BUILD_ROUTES[Build Routes]

    BUILD_ROUTES --> MATCH[Build Match Conditions<br/>- URI<br/>- Headers<br/>- Query params]
    BUILD_ROUTES --> ROUTE_ACTION[Build Route Action<br/>- Cluster<br/>- Weighted clusters<br/>- Redirect]
    BUILD_ROUTES --> POLICIES[Apply Policies<br/>- Retry<br/>- Timeout<br/>- Mirror<br/>- Fault]

    MATCH --> COMBINE
    ROUTE_ACTION --> COMBINE
    POLICIES --> COMBINE[Combine into Route]

    COMBINE --> RESULT[RouteConfiguration]

    style RESULT fill:#9B59B6,color:#fff
```

### CDS Generation

```mermaid
graph TB
    START[Generate CDS for Proxy]

    START --> GET_SERVICES[Get Services accessible<br/>to proxy]

    GET_SERVICES --> APPLY_SIDECAR[Apply Sidecar egress rules]

    APPLY_SIDECAR --> FOR_EACH_SVC[For Each Service]

    FOR_EACH_SVC --> GET_DR[Get DestinationRule<br/>for service]

    GET_DR --> HAS_DR{DR exists?}

    HAS_DR -->|Yes| FOR_EACH_SUBSET[For Each Subset]
    HAS_DR -->|No| DEFAULT_CLUSTER[Create Default Cluster]

    FOR_EACH_SUBSET --> BUILD_CLUSTER[Build Cluster]
    DEFAULT_CLUSTER --> BUILD_CLUSTER

    BUILD_CLUSTER --> CLUSTER_NAME[Generate Cluster Name<br/>outbound|port|subset|host]
    BUILD_CLUSTER --> LB_POLICY[Apply LB Policy]
    BUILD_CLUSTER --> CONN_POOL[Apply Connection Pool]
    BUILD_CLUSTER --> CB[Apply Circuit Breaker]
    BUILD_CLUSTER --> OD[Apply Outlier Detection]
    BUILD_CLUSTER --> TLS_CONFIG[Apply TLS Config]
    BUILD_CLUSTER --> EDS_CONFIG[Configure EDS]

    CLUSTER_NAME --> COMBINE
    LB_POLICY --> COMBINE
    CONN_POOL --> COMBINE
    CB --> COMBINE
    OD --> COMBINE
    TLS_CONFIG --> COMBINE
    EDS_CONFIG --> COMBINE[Combine into Cluster]

    COMBINE --> RESULT[Cluster List]

    style RESULT fill:#27AE60,color:#fff
```

### EDS Generation

```mermaid
graph TB
    START[Generate EDS for Proxy]

    START --> GET_CLUSTERS[Get EDS Clusters<br/>from CDS]

    GET_CLUSTERS --> FOR_EACH_CLUSTER[For Each Cluster]

    FOR_EACH_CLUSTER --> PARSE_NAME[Parse Cluster Name<br/>Extract: service, subset]

    PARSE_NAME --> GET_SERVICE[Get Service from Registry]

    GET_SERVICE --> GET_INSTANCES[Get Service Instances<br/>Pods with matching labels]

    GET_INSTANCES --> FILTER{Filter by:<br/>- Subset labels<br/>- Health status<br/>- Locality}

    FILTER --> GROUP_LOCALITY[Group by Locality<br/>- Region<br/>- Zone<br/>- Subzone]

    GROUP_LOCALITY --> FOR_EACH_LOCALITY[For Each Locality]

    FOR_EACH_LOCALITY --> BUILD_ENDPOINTS[Build Endpoints]

    BUILD_ENDPOINTS --> EP_ADDRESS[Endpoint Address<br/>IP:Port]
    BUILD_ENDPOINTS --> EP_HEALTH[Health Status]
    BUILD_ENDPOINTS --> EP_LB_WEIGHT[Load Balancing Weight]
    BUILD_ENDPOINTS --> EP_METADATA[Endpoint Metadata<br/>- Labels<br/>- Canary]

    EP_ADDRESS --> COMBINE
    EP_HEALTH --> COMBINE
    EP_LB_WEIGHT --> COMBINE
    EP_METADATA --> COMBINE[Combine into LbEndpoint]

    COMBINE --> LOCALITY_GROUP[LocalityLbEndpoints]

    LOCALITY_GROUP --> RESULT[ClusterLoadAssignment]

    style RESULT fill:#1ABC9C,color:#fff
```

## Optimization and Caching

### Configuration Caching Strategy

```mermaid
graph TB
    subgraph "Cache Layers"
        L1[L1: Generated xDS Cache<br/>Per proxy, per type]
        L2[L2: Compiled Config Cache<br/>Shared across proxies]
        L3[L3: Push Context<br/>Immutable snapshot]
        L4[L4: Source Config Cache<br/>Raw K8s resources]
    end

    REQ[xDS Request]

    REQ --> CHECK_L1{L1 Hit?}
    CHECK_L1 -->|Yes| RETURN_L1[Return Cached xDS]
    CHECK_L1 -->|No| CHECK_L2

    CHECK_L2{L2 Hit?}
    CHECK_L2 -->|Yes| GEN_L1[Generate for proxy]
    GEN_L1 --> CACHE_L1[Cache in L1]
    CACHE_L1 --> RETURN_L1

    CHECK_L2 -->|No| CHECK_L3{L3 Valid?}
    CHECK_L3 -->|Yes| GEN_L2[Generate shared config]
    GEN_L2 --> CACHE_L2[Cache in L2]
    CACHE_L2 --> GEN_L1

    CHECK_L3 -->|No| REBUILD[Rebuild Push Context]
    REBUILD --> L3
    L3 --> GEN_L2

    style L1 fill:#27AE60,color:#fff
    style L2 fill:#3498DB,color:#fff
    style L3 fill:#F39C12,color:#fff
    style L4 fill:#9B59B6,color:#fff
```

### Partial Push Optimization

```mermaid
graph LR
    subgraph "Config Change Analysis"
        CHANGE[Config Changed]

        CHANGE --> ANALYZE{Analyze Impact}

        ANALYZE -->|VirtualService| RDS_ONLY[Push: RDS only<br/>Affected: Specific routes]
        ANALYZE -->|Endpoints| EDS_ONLY[Push: EDS only<br/>Affected: Specific clusters]
        ANALYZE -->|DestinationRule| CDS_MAYBE[Push: CDS<br/>Maybe EDS]
        ANALYZE -->|Gateway| LDS_RDS[Push: LDS + RDS<br/>Affected: Gateway proxies]
        ANALYZE -->|ServiceEntry| FULL[Push: All<br/>Affected: All proxies]
    end

    subgraph "Push Optimization"
        RDS_ONLY --> CALC_PROXIES[Calculate Affected Proxies]
        EDS_ONLY --> CALC_PROXIES
        CDS_MAYBE --> CALC_PROXIES
        LDS_RDS --> CALC_PROXIES
        FULL --> ALL_PROXIES[All Proxies]

        CALC_PROXIES --> PROXIES[Proxy Subset]
        ALL_PROXIES --> PROXIES

        PROXIES --> PUSH[Incremental Push]
    end

    style RDS_ONLY fill:#9B59B6,color:#fff
    style EDS_ONLY fill:#27AE60,color:#fff
    style CDS_MAYBE fill:#F39C12,color:#fff
    style FULL fill:#E74C3C,color:#fff
    style PUSH fill:#3498DB,color:#fff
```

## Summary

This document covered Istiod's configuration processing:

1. **Architecture**: Component breakdown and data structures
2. **Watch & Cache**: Kubernetes watchers and in-memory cache
3. **Validation**: Multi-stage validation pipeline
4. **Translation**: High-level Istio → Low-level Envoy
5. **Push Context**: Immutable configuration snapshot
6. **xDS Generation**: Per-type generation logic
7. **Optimization**: Multi-layer caching and partial pushes

### Key Takeaways

- Istiod watches Kubernetes resources and maintains in-memory cache
- Configuration changes are debounced and batched
- PushContext provides immutable, thread-safe snapshot
- Translation happens in multiple stages with validation
- xDS generation is optimized with caching
- Partial pushes minimize unnecessary updates

## Next Steps

Continue to **Part 5: Envoy Configuration Application** to understand how Envoy processes and applies received xDS configurations.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code**: `istio/pilot/pkg/` (Istio source)
