# Part 6: Complete End-to-End Flow Example

## Table of Contents
1. [Introduction](#introduction)
2. [Scenario Setup](#scenario-setup)
3. [Initial Configuration](#initial-configuration)
4. [Configuration Update Flow](#configuration-update-flow)
5. [Traffic Flow with Updated Config](#traffic-flow-with-updated-config)
6. [Troubleshooting Walkthrough](#troubleshooting-walkthrough)

## Introduction

This document provides a complete end-to-end example showing how configuration flows from an Istio CRD creation through to active traffic routing in Envoy. We'll trace every step with detailed diagrams.

## Scenario Setup

### Application Architecture

```mermaid
graph TB
    subgraph "Kubernetes Cluster"
        subgraph "Frontend Namespace"
            IG[Ingress Gateway<br/>istio-ingressgateway]
            PW_POD[productpage Pod<br/>+ Envoy Sidecar]
        end

        subgraph "Default Namespace"
            RV_V1_POD1[reviews-v1 Pod 1<br/>+ Envoy Sidecar]
            RV_V1_POD2[reviews-v1 Pod 2<br/>+ Envoy Sidecar]
            RV_V2_POD1[reviews-v2 Pod 1<br/>+ Envoy Sidecar]
            RV_V2_POD2[reviews-v2 Pod 2<br/>+ Envoy Sidecar]

            RV_SVC[reviews Service<br/>ClusterIP]

            RT_POD[ratings Pod<br/>+ Envoy Sidecar]
            DT_POD[details Pod<br/>+ Envoy Sidecar]
        end

        subgraph "istio-system Namespace"
            ISTIOD[Istiod<br/>Control Plane]
        end
    end

    EXT[External User]

    EXT -->|HTTPS| IG
    IG --> PW_POD
    PW_POD --> RV_SVC
    RV_SVC -.-> RV_V1_POD1
    RV_SVC -.-> RV_V1_POD2
    RV_SVC -.-> RV_V2_POD1
    RV_SVC -.-> RV_V2_POD2

    RV_V2_POD1 --> RT_POD
    RV_V2_POD2 --> RT_POD

    ISTIOD -.xDS.-> IG
    ISTIOD -.xDS.-> PW_POD
    ISTIOD -.xDS.-> RV_V1_POD1
    ISTIOD -.xDS.-> RV_V1_POD2
    ISTIOD -.xDS.-> RV_V2_POD1
    ISTIOD -.xDS.-> RV_V2_POD2
    ISTIOD -.xDS.-> RT_POD
    ISTIOD -.xDS.-> DT_POD

    style IG fill:#E74C3C,color:#fff
    style ISTIOD fill:#326CE5,color:#fff
    style RV_V2_POD1 fill:#27AE60,color:#fff
    style RV_V2_POD2 fill:#27AE60,color:#fff
```

### Goal

We want to implement canary deployment for the `reviews` service:
- Send 90% of traffic to reviews-v1 (stable)
- Send 10% of traffic to reviews-v2 (canary)
- Only for production traffic (not internal testing)

## Initial Configuration

### Step 1: Create VirtualService

```mermaid
sequenceDiagram
    participant U as Operator
    participant K as kubectl
    participant API as K8s API Server
    participant I as Istiod
    participant E as Envoy (productpage)

    U->>K: kubectl apply -f reviews-vs.yaml

    Note over K: VirtualService YAML:<br/>- 90% to v1<br/>- 10% to v2

    K->>API: Create VirtualService

    API->>API: Store in etcd

    Note over API: Event: VirtualService ADDED

    API->>I: Watch notification

    I->>I: Process VirtualService

    rect rgb(200, 220, 240)
        Note over I: Validation Phase
        I->>I: Validate hosts
        I->>I: Validate routes
        I->>I: Check weights sum to 100
        I->>I: Verify service exists
    end

    rect rgb(220, 240, 220)
        Note over I: Translation Phase
        I->>I: Build RouteConfiguration
        I->>I: Create weighted clusters
        I->>I: Generate match conditions
    end

    rect rgb(240, 220, 220)
        Note over I: Push Phase
        I->>I: Identify affected proxies
        Note over I: productpage envoy<br/>needs update
        I->>I: Build PushContext
        I->>I: Generate RDS
    end

    I->>E: DiscoveryResponse (RDS)<br/>version: "v2"<br/>nonce: "n123"

    E->>E: Validate RDS

    E->>I: DiscoveryRequest (ACK)<br/>version: "v2"<br/>nonce: "n123"

    Note over E: Config applied<br/>Traffic split active
```

### VirtualService YAML

```yaml
apiVersion: networking.istio.io/v1beta1
kind: VirtualService
metadata:
  name: reviews
  namespace: default
spec:
  hosts:
  - reviews.default.svc.cluster.local
  http:
  - route:
    - destination:
        host: reviews.default.svc.cluster.local
        subset: v1
      weight: 90
    - destination:
        host: reviews.default.svc.cluster.local
        subset: v2
      weight: 10
```

### Step 2: Create DestinationRule

```mermaid
sequenceDiagram
    participant U as Operator
    participant API as K8s API Server
    participant I as Istiod
    participant E1 as Envoy (productpage)
    participant E2 as Envoy (reviews-v1)
    participant E3 as Envoy (reviews-v2)

    U->>API: kubectl apply -f reviews-dr.yaml

    Note over API: DestinationRule YAML:<br/>- Subsets: v1, v2<br/>- Load balancing<br/>- Connection pool

    API->>I: Watch notification

    I->>I: Process DestinationRule

    rect rgb(200, 220, 240)
        Note over I: Validation Phase
        I->>I: Validate host
        I->>I: Validate subsets
        I->>I: Check label selectors
    end

    rect rgb(220, 240, 220)
        Note over I: Translation Phase
        I->>I: For each subset:<br/>Build Cluster
        I->>I: Apply traffic policy
        I->>I: Configure load balancer
        I->>I: Configure circuit breaker
    end

    rect rgb(240, 220, 220)
        Note over I: Push Phase
        I->>I: Identify affected proxies
        Note over I: All proxies calling<br/>reviews need update
        I->>I: Generate CDS + EDS
    end

    par Push to all proxies
        I->>E1: DiscoveryResponse (CDS+EDS)
        E1->>I: ACK

        I->>E2: DiscoveryResponse (CDS+EDS)
        E2->>I: ACK

        I->>E3: DiscoveryResponse (CDS+EDS)
        E3->>I: ACK
    end

    Note over E1,E3: Clusters configured with<br/>subsets and policies
```

### DestinationRule YAML

```yaml
apiVersion: networking.istio.io/v1beta1
kind: DestinationRule
metadata:
  name: reviews
  namespace: default
spec:
  host: reviews.default.svc.cluster.local
  trafficPolicy:
    connectionPool:
      tcp:
        maxConnections: 100
      http:
        http1MaxPendingRequests: 10
        maxRequestsPerConnection: 2
    loadBalancer:
      simple: LEAST_CONN
    outlierDetection:
      consecutiveErrors: 5
      interval: 30s
      baseEjectionTime: 30s
  subsets:
  - name: v1
    labels:
      version: v1
  - name: v2
    labels:
      version: v2
```

## Configuration Update Flow

### Complete Configuration Processing Flow

```mermaid
graph TB
    START[kubectl apply VirtualService]

    START --> K8S[Kubernetes API Server]

    K8S --> ETCD[Store in etcd]
    K8S --> WATCH[Trigger Watch Event]

    WATCH --> ISTIOD_WATCH[Istiod Config Watcher]

    subgraph "Istiod Processing"
        ISTIOD_WATCH --> CACHE[Update Config Cache]
        CACHE --> DEBOUNCE[Debounce<br/>100ms window]
        DEBOUNCE --> VALIDATE[Validate Config]

        VALIDATE --> VALID{Valid?}
        VALID -->|No| REJECT[Reject & Log Error]
        VALID -->|Yes| TRANSLATE

        TRANSLATE[Translate to Envoy Config]
        TRANSLATE --> BUILD_PUSH[Build Push Context]

        BUILD_PUSH --> IDENTIFY[Identify Affected Proxies]
        IDENTIFY --> GEN_XDS[Generate xDS]

        GEN_XDS --> GEN_LDS[Generate LDS]
        GEN_XDS --> GEN_RDS[Generate RDS]
        GEN_XDS --> GEN_CDS[Generate CDS]
        GEN_XDS --> GEN_EDS[Generate EDS]

        GEN_LDS --> CACHE_XDS[Cache Generated xDS]
        GEN_RDS --> CACHE_XDS
        GEN_CDS --> CACHE_XDS
        GEN_EDS --> CACHE_XDS

        CACHE_XDS --> PUSH_QUEUE[Add to Push Queue]
    end

    PUSH_QUEUE --> PUSH_PROXIES[Push to Proxies]

    subgraph "Envoy Processing"
        PUSH_PROXIES --> ENVOY_RECV[Receive xDS]
        ENVOY_RECV --> ENVOY_VALIDATE[Validate xDS]

        ENVOY_VALIDATE --> ENVOY_VALID{Valid?}
        ENVOY_VALID -->|No| NACK[Send NACK]
        ENVOY_VALID -->|Yes| WARM

        WARM[Warm Clusters]
        WARM --> ACTIVATE[Activate Config]
        ACTIVATE --> DRAIN[Drain Old Config]
        DRAIN --> ACK[Send ACK]
    end

    ACK --> COMPLETE[Config Active]

    style VALIDATE fill:#3498DB,color:#fff
    style TRANSLATE fill:#F39C12,color:#fff
    style GEN_XDS fill:#9B59B6,color:#fff
    style ACTIVATE fill:#27AE60,color:#fff
```

### Detailed Istiod Processing

```mermaid
stateDiagram-v2
    [*] --> WatchEvent: VS created/updated

    WatchEvent --> ConfigCache: Store in cache

    state ConfigCache {
        [*] --> CacheUpdate
        CacheUpdate --> IndexUpdate
        IndexUpdate --> [*]
    }

    ConfigCache --> Debouncer: Trigger push

    state Debouncer {
        [*] --> Accumulating
        Accumulating --> Accumulating: More events (within 100ms)
        Accumulating --> Timeout: No events for 100ms
    }

    Debouncer --> Validation: Build push request

    state Validation {
        [*] --> SchemaCheck
        SchemaCheck --> SemanticCheck
        SemanticCheck --> CrossRefCheck
        CrossRefCheck --> RuntimeCheck
    }

    Validation --> Translation: Valid

    state Translation {
        [*] --> ParseVS
        ParseVS --> BuildRoutes
        BuildRoutes --> ApplyPolicies
        ApplyPolicies --> CreateEnvoyConfig
    }

    Translation --> PushContext: Generate

    state PushContext {
        [*] --> LoadServices
        LoadServices --> LoadVS
        LoadVS --> LoadDR
        LoadDR --> BuildIndexes
        BuildIndexes --> GenerateXDS
    }

    PushContext --> PushQueue: Ready to push

    state PushQueue {
        [*] --> CalculateAffected
        CalculateAffected --> PrioritizeProxies
        PrioritizeProxies --> BatchPush
    }

    PushQueue --> [*]: Pushed to proxies
```

## Traffic Flow with Updated Config

### Request Flow with Canary Routing

```mermaid
sequenceDiagram
    participant C as Client
    participant IG as Ingress Gateway
    participant PW as productpage-envoy
    participant RV1 as reviews-v1-envoy
    participant RV2 as reviews-v2-envoy
    participant RT as ratings-envoy

    Note over C,RT: Scenario: 10 requests

    loop 10 requests
        C->>IG: GET /productpage

        IG->>PW: Forward to productpage

        PW->>PW: Call reviews service

        alt Request 1-9 (90%)
            PW->>RV1: Route to reviews-v1<br/>(based on weight)
            Note over RV1: v1 doesn't call ratings
            RV1->>PW: Response (no ratings)
        else Request 10 (10%)
            PW->>RV2: Route to reviews-v2<br/>(based on weight)
            RV2->>RT: GET /ratings
            RT->>RV2: Ratings data
            RV2->>PW: Response (with ratings)
        end

        PW->>IG: Page response
        IG->>C: HTTP 200
    end

    Note over C,RT: Result:<br/>9 responses without ratings<br/>1 response with ratings
```

### Load Balancing Detail

```mermaid
graph TB
    REQ[Request to reviews service]

    REQ --> ROUTE_MATCH[Route Match]

    ROUTE_MATCH --> WEIGHTED[Weighted Cluster Selection]

    WEIGHTED --> RANDOM[Random number: 0-99]

    RANDOM --> CHECK{Value?}

    CHECK -->|0-89<br/>90%| V1_CLUSTER[Select Cluster:<br/>outbound|80|v1|reviews]
    CHECK -->|90-99<br/>10%| V2_CLUSTER[Select Cluster:<br/>outbound|80|v2|reviews]

    V1_CLUSTER --> V1_LB[Load Balancer:<br/>LEAST_CONN]
    V2_CLUSTER --> V2_LB[Load Balancer:<br/>LEAST_CONN]

    V1_LB --> V1_EP[Select Endpoint]
    V2_LB --> V2_EP[Select Endpoint]

    V1_EP --> V1_HEALTH{Healthy?}
    V2_EP --> V2_HEALTH{Healthy?}

    V1_HEALTH -->|Yes| V1_POD[reviews-v1 Pod]
    V1_HEALTH -->|No| V1_LB

    V2_HEALTH -->|Yes| V2_POD[reviews-v2 Pod]
    V2_HEALTH -->|No| V2_LB

    V1_POD --> RESPONSE[Send Request]
    V2_POD --> RESPONSE

    style V1_CLUSTER fill:#3498DB,color:#fff
    style V2_CLUSTER fill:#27AE60,color:#fff
    style V1_POD fill:#3498DB,color:#fff
    style V2_POD fill:#27AE60,color:#fff
```

### Envoy Stats After 1000 Requests

```mermaid
graph LR
    subgraph "productpage Envoy Stats"
        REQ_TOTAL[upstream_rq_total: 1000]

        V1_STATS[Cluster: reviews-v1<br/>---<br/>upstream_rq_total: 903<br/>upstream_rq_success: 903<br/>upstream_rq_active: 0<br/>upstream_cx_active: 2]

        V2_STATS[Cluster: reviews-v2<br/>---<br/>upstream_rq_total: 97<br/>upstream_rq_success: 97<br/>upstream_rq_active: 0<br/>upstream_cx_active: 1]

        REQ_TOTAL --> V1_STATS
        REQ_TOTAL --> V2_STATS
    end

    subgraph "Interpretation"
        NOTE[Distribution close to 90/10:<br/>- reviews-v1: 90.3%<br/>- reviews-v2: 9.7%<br/><br/>All requests successful<br/>No active requests<br/>Connections pooled]
    end

    V1_STATS -.-> NOTE
    V2_STATS -.-> NOTE

    style V1_STATS fill:#3498DB,color:#fff
    style V2_STATS fill:#27AE60,color:#fff
    style NOTE fill:#F39C12,color:#fff
```

## Troubleshooting Walkthrough

### Common Issues and Detection

```mermaid
graph TB
    ISSUE[Configuration Not Working]

    ISSUE --> CHECK1[Check 1: Is config applied?]
    CHECK1 --> CMD1[kubectl get vs,dr -n default]

    CMD1 --> EXISTS{Exists?}
    EXISTS -->|No| FIX1[Apply configuration]
    EXISTS -->|Yes| CHECK2

    CHECK2[Check 2: Is config valid?]
    CHECK2 --> CMD2[kubectl describe vs reviews]

    CMD2 --> VALID{Valid?}
    VALID -->|No| FIX2[Fix validation errors]
    VALID -->|Yes| CHECK3

    CHECK3[Check 3: Did Istiod process it?]
    CHECK3 --> CMD3[kubectl logs -n istio-system<br/>deployment/istiod]

    CMD3 --> PROCESSED{Processed?}
    PROCESSED -->|No| FIX3[Check Istiod logs<br/>for errors]
    PROCESSED -->|Yes| CHECK4

    CHECK4[Check 4: Did Envoy receive it?]
    CHECK4 --> CMD4[istioctl proxy-status productpage-xxx]

    CMD4 --> SYNCED{Synced?}
    SYNCED -->|No| FIX4[Check xDS connection]
    SYNCED -->|Yes| CHECK5

    CHECK5[Check 5: Is config active in Envoy?]
    CHECK5 --> CMD5[istioctl proxy-config routes<br/>productpage-xxx]

    CMD5 --> ACTIVE{Active?}
    ACTIVE -->|No| FIX5[Check Envoy logs<br/>for NACK]
    ACTIVE -->|Yes| CHECK6

    CHECK6[Check 6: Are endpoints healthy?]
    CHECK6 --> CMD6[istioctl proxy-config endpoints<br/>productpage-xxx]

    CMD6 --> HEALTHY{Healthy?}
    HEALTHY -->|No| FIX6[Check pod health<br/>and readiness]
    HEALTHY -->|Yes| CHECK7

    CHECK7[Check 7: Is traffic flowing?]
    CHECK7 --> CMD7[Check Envoy access logs<br/>or use metrics]

    style FIX1 fill:#E74C3C,color:#fff
    style FIX2 fill:#E74C3C,color:#fff
    style FIX3 fill:#E74C3C,color:#fff
    style FIX4 fill:#E74C3C,color:#fff
    style FIX5 fill:#E74C3C,color:#fff
    style FIX6 fill:#E74C3C,color:#fff
    style CHECK7 fill:#27AE60,color:#fff
```

### Debug Commands

```mermaid
graph TB
    subgraph "Istio Debug Commands"
        C1["kubectl get vs,dr,gw,se -A<br/>List all Istio configs"]
        C2["kubectl describe vs reviews<br/>Check config details + events"]
        C3["kubectl logs -n istio-system<br/>deployment/istiod --tail=100<br/>Check Istiod logs"]
        C4["istioctl analyze<br/>Analyze config for issues"]
        C5["istioctl proxy-status<br/>Check all proxies sync status"]
    end

    subgraph "Envoy Debug Commands"
        E1["istioctl proxy-config<br/>routes/clusters/endpoints<br/>productpage-xxx<br/>View active config"]
        E2["kubectl exec productpage-xxx -c istio-proxy<br/>-- curl localhost:15000/config_dump<br/>Full config dump"]
        E3["kubectl exec productpage-xxx -c istio-proxy<br/>-- curl localhost:15000/clusters<br/>Cluster status"]
        E4["kubectl logs productpage-xxx -c istio-proxy<br/>Envoy access/error logs"]
        E5["kubectl exec productpage-xxx -c istio-proxy<br/>-- curl localhost:15000/stats/prometheus<br/>All metrics"]
    end

    subgraph "Traffic Debug"
        T1["istioctl dashboard envoy productpage-xxx<br/>Open Envoy admin UI"]
        T2["kubectl port-forward productpage-xxx 15000<br/>Access Envoy admin locally"]
        T3["istioctl experimental describe pod<br/>productpage-xxx<br/>Describe config + policies"]
    end

    style C4 fill:#27AE60,color:#fff
    style E1 fill:#3498DB,color:#fff
    style T1 fill:#F39C12,color:#fff
```

### Example: NACK Investigation

```mermaid
sequenceDiagram
    participant O as Operator
    participant I as Istiod
    participant E as Envoy
    participant D as Debug Tools

    Note over O: Config not working

    O->>D: istioctl proxy-status

    D-->>O: productpage: version mismatch<br/>Istiod: v123<br/>Envoy: v122 (NACK)

    Note over O: Envoy rejected config!

    O->>E: kubectl logs productpage-xxx<br/>-c istio-proxy

    E-->>O: [error] Route validation failed:<br/>cluster 'outbound|80|v3|reviews'<br/>not found

    Note over O: Envoy can't find cluster v3<br/>but we only have v1 and v2!

    O->>I: kubectl describe vs reviews

    I-->>O: VirtualService has typo:<br/>subset: v3<br/>(should be v2)

    Note over O: Found the issue!

    O->>I: kubectl edit vs reviews<br/>Fix: v3 → v2

    I->>E: DiscoveryResponse<br/>version: v124<br/>(corrected config)

    E->>E: Validation success

    E->>I: DiscoveryRequest (ACK)<br/>version: v124

    O->>D: istioctl proxy-status

    D-->>O: productpage: SYNCED ✓

    Note over O: Config now working!
```

## Summary

This document provided a complete end-to-end example:

1. **Setup**: Microservices architecture with Istio
2. **Configuration**: VirtualService and DestinationRule creation
3. **Processing**: Step-by-step flow from CRD to Envoy
4. **Traffic**: Request routing with canary deployment
5. **Troubleshooting**: Debug workflow and tools

### Key Flows Demonstrated

- **Configuration Creation**: kubectl → K8s API → Istiod → Envoy
- **Validation**: Multi-stage validation at Istiod and Envoy
- **Translation**: High-level Istio config → Low-level Envoy xDS
- **Distribution**: xDS push to affected proxies
- **Activation**: Zero-downtime config application
- **Traffic Routing**: Weighted cluster selection and load balancing

### Debugging Tools Used

- `kubectl get/describe` - View Istio resources
- `istioctl proxy-status` - Check sync status
- `istioctl proxy-config` - View active Envoy config
- `istioctl analyze` - Validate configurations
- Envoy admin interface - Detailed runtime info
- Logs and metrics - Troubleshooting

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Complete Series**: Parts 1-6 cover the full Istio → Envoy configuration flow
