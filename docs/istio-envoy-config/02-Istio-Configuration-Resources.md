# Part 2: Istio Configuration Resources

## Table of Contents
1. [Introduction](#introduction)
2. [Istio CRD Overview](#istio-crd-overview)
3. [VirtualService](#virtualservice)
4. [DestinationRule](#destinationrule)
5. [Gateway](#gateway)
6. [ServiceEntry](#serviceentry)
7. [Configuration Translation to Envoy](#configuration-translation-to-envoy)
8. [Complete Example](#complete-example)

## Introduction

Istio uses Kubernetes Custom Resource Definitions (CRDs) to define service mesh behavior. These high-level configurations are translated by Istiod into low-level Envoy xDS configurations.

## Istio CRD Overview

### Istio Configuration Resources

```mermaid
graph TB
    subgraph "Traffic Management"
        VS[VirtualService<br/>- Routing rules<br/>- Traffic splitting<br/>- Retries/Timeouts]
        DR[DestinationRule<br/>- Load balancing<br/>- Connection pool<br/>- Outlier detection]
        GW[Gateway<br/>- Ingress/Egress<br/>- TLS termination<br/>- Port configuration]
        SE[ServiceEntry<br/>- External services<br/>- Service registry<br/>- Resolution]
    end

    subgraph "Security"
        PA[PeerAuthentication<br/>- mTLS mode<br/>- Port-level mTLS]
        RA[RequestAuthentication<br/>- JWT validation<br/>- OIDC]
        AP[AuthorizationPolicy<br/>- Access control<br/>- RBAC]
    end

    subgraph "Observability"
        TM[Telemetry<br/>- Metrics<br/>- Traces<br/>- Access logs]
    end

    subgraph "Resilience"
        WA[WasmPlugin<br/>- Custom filters<br/>- Extensions]
        EF[EnvoyFilter<br/>- Low-level Envoy config<br/>- Advanced customization]
    end

    style VS fill:#3498DB,color:#fff
    style DR fill:#9B59B6,color:#fff
    style GW fill:#E74C3C,color:#fff
    style SE fill:#F39C12,color:#fff
```

### CRD Relationships

```mermaid
graph LR
    subgraph "Kubernetes Resources"
        SVC[Service]
        POD[Pod]
        NS[Namespace]
    end

    subgraph "Istio Resources"
        VS[VirtualService]
        DR[DestinationRule]
        GW[Gateway]
        SE[ServiceEntry]
    end

    subgraph "Envoy Config"
        L[Listeners]
        R[Routes]
        C[Clusters]
        E[Endpoints]
    end

    SVC -.Discovers.-> POD
    VS -.References.-> SVC
    VS -.References.-> GW
    DR -.References.-> SVC
    SE -.Defines.-> SVC

    GW --> L
    VS --> R
    DR --> C
    SVC --> E
    SE --> E

    style VS fill:#3498DB,color:#fff
    style DR fill:#9B59B6,color:#fff
    style GW fill:#E74C3C,color:#fff
    style SE fill:#F39C12,color:#fff
```

## VirtualService

### VirtualService Structure

```mermaid
graph TB
    VS[VirtualService]

    VS --> HOSTS[hosts<br/>- example.com<br/>- *.example.com]
    VS --> GATEWAYS[gateways<br/>- mesh<br/>- my-gateway]
    VS --> HTTP[http]
    VS --> TLS[tls]
    VS --> TCP[tcp]

    HTTP --> MATCH[match<br/>- URI<br/>- Headers<br/>- Query params<br/>- Source labels]
    HTTP --> ROUTE[route<br/>- Destination<br/>- Weight<br/>- Headers]
    HTTP --> REDIRECT[redirect<br/>- URI<br/>- Authority]
    HTTP --> REWRITE[rewrite<br/>- URI<br/>- Authority]
    HTTP --> TIMEOUT[timeout]
    HTTP --> RETRIES[retries<br/>- Attempts<br/>- Per-try timeout]
    HTTP --> FAULT[fault<br/>- Delay<br/>- Abort]
    HTTP --> MIRROR[mirror<br/>- Destination<br/>- Percentage]
    HTTP --> CORS[corsPolicy]
    HTTP --> HEADERS_OP[headers<br/>- Add<br/>- Set<br/>- Remove]

    ROUTE --> DEST[destination<br/>- Host<br/>- Subset<br/>- Port]
    ROUTE --> WEIGHT_VAL[weight]
    ROUTE --> HDR_OP[headers]

    style VS fill:#3498DB,color:#fff
    style MATCH fill:#F39C12,color:#fff
    style ROUTE fill:#27AE60,color:#fff
    style FAULT fill:#E74C3C,color:#fff
```

### VirtualService Example and Translation

```mermaid
graph TB
    subgraph "VirtualService YAML"
        YAML["apiVersion: networking.istio.io/v1beta1
        kind: VirtualService
        metadata:
          name: reviews
        spec:
          hosts:
          - reviews.default.svc.cluster.local
          http:
          - match:
            - headers:
                end-user:
                  exact: jason
            route:
            - destination:
                host: reviews
                subset: v2
          - route:
            - destination:
                host: reviews
                subset: v1"]
    end

    subgraph "Envoy RDS Translation"
        ROUTE_CONFIG["RouteConfiguration:
        - name: reviews.default.svc.cluster.local:80
        - virtual_hosts:
          - name: reviews.default.svc.cluster.local:80
            domains: ['reviews.default.svc.cluster.local']
            routes:
            - match:
                prefix: /
                headers:
                - name: end-user
                  exact_match: jason
              route:
                cluster: outbound|80|v2|reviews.default.svc.cluster.local
            - match:
                prefix: /
              route:
                cluster: outbound|80|v1|reviews.default.svc.cluster.local"]
    end

    YAML --> ROUTE_CONFIG

    style YAML fill:#3498DB,color:#fff
    style ROUTE_CONFIG fill:#9B59B6,color:#fff
```

### Traffic Splitting Flow

```mermaid
graph LR
    REQUEST[Incoming Request]

    REQUEST --> MATCH{Match Headers?}

    MATCH -->|end-user: jason| V2[reviews-v2<br/>100%]
    MATCH -->|No match| V1[reviews-v1<br/>100%]

    subgraph "Canary Routing Example"
        REQUEST2[Request] --> WEIGHT{Weight Distribution}
        WEIGHT -->|90%| STABLE[Stable Version]
        WEIGHT -->|10%| CANARY[Canary Version]
    end

    style V2 fill:#27AE60,color:#fff
    style V1 fill:#3498DB,color:#fff
    style CANARY fill:#F39C12,color:#fff
```

## DestinationRule

### DestinationRule Structure

```mermaid
graph TB
    DR[DestinationRule]

    DR --> HOST[host<br/>reviews.default.svc.cluster.local]
    DR --> TRAFFIC[trafficPolicy]
    DR --> SUBSETS[subsets]

    TRAFFIC --> LB[loadBalancer<br/>- Simple: ROUND_ROBIN<br/>- ConsistentHash<br/>- LocalityLbSetting]
    TRAFFIC --> POOL[connectionPool<br/>- TCP settings<br/>- HTTP settings]
    TRAFFIC --> OUTLIER[outlierDetection<br/>- consecutiveErrors<br/>- interval<br/>- baseEjectionTime]
    TRAFFIC --> TLS_SETTINGS[tls<br/>- Mode: ISTIO_MUTUAL<br/>- ClientCertificate<br/>- CaCertificates]
    TRAFFIC --> PORT_LEVEL[portLevelSettings]

    SUBSETS --> SUB1[subset: v1<br/>labels:<br/>  version: v1]
    SUBSETS --> SUB2[subset: v2<br/>labels:<br/>  version: v2]
    SUBSETS --> SUB_TRAFFIC[trafficPolicy<br/>Per-subset overrides]

    style DR fill:#9B59B6,color:#fff
    style TRAFFIC fill:#3498DB,color:#fff
    style SUBSETS fill:#F39C12,color:#fff
    style OUTLIER fill:#E74C3C,color:#fff
```

### DestinationRule to Envoy CDS Translation

```mermaid
graph TB
    subgraph "DestinationRule"
        DR_YAML["apiVersion: networking.istio.io/v1beta1
        kind: DestinationRule
        metadata:
          name: reviews
        spec:
          host: reviews.default.svc.cluster.local
          trafficPolicy:
            connectionPool:
              tcp:
                maxConnections: 100
              http:
                http1MaxPendingRequests: 1
                maxRequestsPerConnection: 2
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
              version: v2"]
    end

    subgraph "Envoy CDS Translation"
        CDS["Cluster:
        - name: outbound|80|v1|reviews.default.svc.cluster.local
        - type: EDS
        - connect_timeout: 10s
        - circuit_breakers:
            thresholds:
            - max_connections: 100
              max_pending_requests: 1
              max_requests_per_connection: 2
        - outlier_detection:
            consecutive_5xx: 5
            interval: 30s
            base_ejection_time: 30s
        - eds_cluster_config:
            service_name: outbound|80|v1|reviews.default.svc.cluster.local"]
    end

    DR_YAML --> CDS

    style DR_YAML fill:#9B59B6,color:#fff
    style CDS fill:#F39C12,color:#fff
```

### Load Balancing Algorithms

```mermaid
graph TB
    LB[Load Balancing Policies]

    LB --> SIMPLE[Simple LB]
    LB --> CONSISTENT[Consistent Hash]
    LB --> LOCALITY[Locality-based]

    SIMPLE --> RR[ROUND_ROBIN<br/>Distribute evenly]
    SIMPLE --> LC[LEAST_CONN<br/>Fewest connections]
    SIMPLE --> RANDOM[RANDOM<br/>Random selection]
    SIMPLE --> PASSTHROUGH[PASSTHROUGH<br/>Cluster from original dst]

    CONSISTENT --> HEADER[httpHeaderName<br/>Hash based on header]
    CONSISTENT --> COOKIE[httpCookie<br/>Hash based on cookie]
    CONSISTENT --> SOURCE_IP[useSourceIp<br/>Hash based on source IP]
    CONSISTENT --> QUERY_PARAM[httpQueryParameterName<br/>Hash based on query param]

    LOCALITY --> DISTRIBUTE[Distribute across zones]
    LOCALITY --> FAILOVER[Failover to other zones]
    LOCALITY --> WEIGHTED[Weighted distribution]

    style SIMPLE fill:#3498DB,color:#fff
    style CONSISTENT fill:#9B59B6,color:#fff
    style LOCALITY fill:#27AE60,color:#fff
```

## Gateway

### Gateway Structure

```mermaid
graph TB
    GW[Gateway]

    GW --> SELECTOR[selector<br/>istio: ingressgateway]
    GW --> SERVERS[servers]

    SERVERS --> S1[Server 1]
    SERVERS --> S2[Server 2]

    S1 --> PORT1[port<br/>- number: 80<br/>- protocol: HTTP<br/>- name: http]
    S1 --> HOSTS1[hosts<br/>- '*.example.com']
    S1 --> TLS1[tls<br/>- httpsRedirect: true]

    S2 --> PORT2[port<br/>- number: 443<br/>- protocol: HTTPS<br/>- name: https]
    S2 --> HOSTS2[hosts<br/>- '*.example.com']
    S2 --> TLS2[tls<br/>- mode: SIMPLE<br/>- credentialName: example-cert]

    style GW fill:#E74C3C,color:#fff
    style S1 fill:#3498DB,color:#fff
    style S2 fill:#27AE60,color:#fff
```

### Gateway to Envoy LDS Translation

```mermaid
graph TB
    subgraph "Gateway YAML"
        GW_YAML["apiVersion: networking.istio.io/v1beta1
        kind: Gateway
        metadata:
          name: my-gateway
        spec:
          selector:
            istio: ingressgateway
          servers:
          - port:
              number: 80
              name: http
              protocol: HTTP
            hosts:
            - '*.example.com'
          - port:
              number: 443
              name: https
              protocol: HTTPS
            tls:
              mode: SIMPLE
              credentialName: example-credential
            hosts:
            - '*.example.com'"]
    end

    subgraph "Envoy LDS Translation"
        LDS["Listener:
        - name: 0.0.0.0_80
        - address: 0.0.0.0:80
        - filter_chains:
          - filters:
            - name: envoy.filters.network.http_connection_manager
              typed_config:
                route_config_name: http.80

        Listener:
        - name: 0.0.0.0_443
        - address: 0.0.0.0:443
        - filter_chains:
          - filter_chain_match:
              server_names: ['*.example.com']
            transport_socket:
              name: envoy.transport_sockets.tls
              typed_config:
                common_tls_context:
                  tls_certificate_sds_secret_configs:
                  - name: example-credential"]
    end

    GW_YAML --> LDS

    style GW_YAML fill:#E74C3C,color:#fff
    style LDS fill:#3498DB,color:#fff
```

### Gateway Request Flow

```mermaid
sequenceDiagram
    participant C as Client
    participant IG as Ingress Gateway<br/>(Envoy)
    participant VS as VirtualService<br/>(Routing Rules)
    participant P as Pod<br/>(Service)

    C->>IG: HTTPS Request<br/>Host: api.example.com

    Note over IG: Match Gateway<br/>Port: 443<br/>Host: *.example.com

    IG->>IG: TLS Termination<br/>(using example-credential)

    IG->>VS: Route request based on<br/>VirtualService rules

    Note over VS: Match rules:<br/>- URI prefix<br/>- Headers<br/>- Query params

    VS->>P: Forward to destination<br/>with load balancing

    P->>VS: Response

    VS->>IG: Response

    IG->>C: HTTPS Response
```

## ServiceEntry

### ServiceEntry Structure

```mermaid
graph TB
    SE[ServiceEntry]

    SE --> HOSTS[hosts<br/>- external-api.example.com]
    SE --> ADDRESSES[addresses<br/>- 192.168.1.10]
    SE --> PORTS[ports]
    SE --> LOCATION[location<br/>- MESH_EXTERNAL<br/>- MESH_INTERNAL]
    SE --> RESOLUTION[resolution<br/>- NONE<br/>- STATIC<br/>- DNS<br/>- DNS_ROUND_ROBIN]
    SE --> ENDPOINTS[endpoints]

    PORTS --> P1[port:<br/>- number: 443<br/>- protocol: HTTPS<br/>- name: https]

    ENDPOINTS --> EP1[endpoint:<br/>- address: 1.2.3.4<br/>- labels:<br/>    region: us-west]

    style SE fill:#F39C12,color:#fff
    style LOCATION fill:#3498DB,color:#fff
    style RESOLUTION fill:#9B59B6,color:#fff
```

### ServiceEntry Use Cases

```mermaid
graph TB
    subgraph "External Service Access"
        A[Pod in Mesh]
        A -->|Call external API| B[external-api.com]

        Note1["Without ServiceEntry:
        - Blocked by default (outbound traffic policy)
        - No retry/timeout policies
        - No telemetry"]

        Note2["With ServiceEntry:
        - Allowed traffic
        - Apply DestinationRule
        - Full observability"]
    end

    subgraph "Legacy System Integration"
        C[Istio Service]
        C -->|Access legacy DB| D[Legacy Database<br/>Not in K8s]

        SE[ServiceEntry defines:<br/>- Static IPs<br/>- Port mappings<br/>- TLS settings]

        D -.Registered via.-> SE
    end

    subgraph "Service Mesh Expansion"
        E[Service in K8s]
        E -->|Call VM service| F[Service on VM]

        SE2[ServiceEntry:<br/>- Registers VM service<br/>- Enables mTLS<br/>- Load balancing]

        F -.Registered via.-> SE2
    end

    style A fill:#3498DB,color:#fff
    style C fill:#3498DB,color:#fff
    style E fill:#3498DB,color:#fff
    style SE fill:#F39C12,color:#fff
    style SE2 fill:#F39C12,color:#fff
```

## Configuration Translation to Envoy

### Translation Pipeline

```mermaid
graph TB
    subgraph "1. Input - Istio CRDs"
        VS[VirtualService]
        DR[DestinationRule]
        GW[Gateway]
        SE[ServiceEntry]
    end

    subgraph "2. Kubernetes API"
        K8S[Watch & Cache<br/>Resources]
    end

    subgraph "3. Istiod - Config Processing"
        WATCH[Config Watcher]
        VALIDATE[Validation]
        MERGE[Merge Configs]
        TRANSLATE[Translation Engine]
    end

    subgraph "4. xDS Generation"
        GEN_LDS[Generate LDS]
        GEN_RDS[Generate RDS]
        GEN_CDS[Generate CDS]
        GEN_EDS[Generate EDS]
    end

    subgraph "5. Output - Envoy xDS"
        OUT_LDS[Listeners]
        OUT_RDS[Routes]
        OUT_CDS[Clusters]
        OUT_EDS[Endpoints]
    end

    VS --> K8S
    DR --> K8S
    GW --> K8S
    SE --> K8S

    K8S --> WATCH
    WATCH --> VALIDATE
    VALIDATE --> MERGE
    MERGE --> TRANSLATE

    TRANSLATE --> GEN_LDS
    TRANSLATE --> GEN_RDS
    TRANSLATE --> GEN_CDS
    TRANSLATE --> GEN_EDS

    GEN_LDS --> OUT_LDS
    GEN_RDS --> OUT_RDS
    GEN_CDS --> OUT_CDS
    GEN_EDS --> OUT_EDS

    GW -.Primarily.-> GEN_LDS
    VS -.Primarily.-> GEN_RDS
    DR -.Primarily.-> GEN_CDS
    SE -.Primarily.-> GEN_EDS

    style TRANSLATE fill:#E74C3C,color:#fff
    style OUT_LDS fill:#3498DB,color:#fff
    style OUT_RDS fill:#9B59B6,color:#fff
    style OUT_CDS fill:#F39C12,color:#fff
    style OUT_EDS fill:#27AE60,color:#fff
```

### Configuration Mapping

```mermaid
graph LR
    subgraph "Istio High-Level"
        IH1[Gateway<br/>Ingress/Egress]
        IH2[VirtualService<br/>Routing Rules]
        IH3[DestinationRule<br/>Traffic Policy]
        IH4[ServiceEntry<br/>Service Registry]
    end

    subgraph "Envoy Low-Level"
        EL1[Listener<br/>Port binding<br/>TLS config]
        EL2[Route<br/>Match & Route<br/>Redirects]
        EL3[Cluster<br/>LB policy<br/>Circuit breaker]
        EL4[Endpoint<br/>IP:Port<br/>Health status]
    end

    IH1 ==>|Translates to| EL1
    IH2 ==>|Translates to| EL2
    IH3 ==>|Translates to| EL3
    IH4 ==>|Provides| EL4

    style IH1 fill:#E74C3C,color:#fff
    style IH2 fill:#3498DB,color:#fff
    style IH3 fill:#9B59B6,color:#fff
    style IH4 fill:#F39C12,color:#fff
```

## Complete Example

### Bookinfo Application Configuration

```mermaid
graph TB
    subgraph "Application Topology"
        PW[productpage]
        RV[reviews]
        RT[ratings]
        DT[details]

        PW --> RV
        PW --> DT
        RV --> RT
    end

    subgraph "Istio Configuration"
        GW_BOOK[Gateway<br/>my-gateway]
        VS_PW[VirtualService<br/>productpage]
        VS_RV[VirtualService<br/>reviews]
        DR_RV[DestinationRule<br/>reviews<br/>subsets: v1, v2, v3]
        DR_RT[DestinationRule<br/>ratings]
    end

    subgraph "Traffic Flow"
        IGW[Ingress Gateway]
        IGW -->|Route to| PW
        PW -->|90% to v1<br/>10% to v2| RV
        RV -->|All traffic| RT
    end

    GW_BOOK -.Configures.-> IGW
    VS_PW -.Configures.-> PW
    VS_RV -.Configures.-> RV
    DR_RV -.Configures.-> RV
    DR_RT -.Configures.-> RT

    style GW_BOOK fill:#E74C3C,color:#fff
    style VS_PW fill:#3498DB,color:#fff
    style VS_RV fill:#3498DB,color:#fff
    style DR_RV fill:#9B59B6,color:#fff
```

### Bookinfo Configuration Flow

```mermaid
sequenceDiagram
    participant U as User
    participant IG as Ingress Gateway
    participant PP as productpage
    participant RV as reviews (v1/v2)
    participant RT as ratings

    Note over U,IG: Gateway Configuration<br/>Opens port 80

    U->>IG: GET /productpage

    Note over IG: VirtualService: productpage<br/>Route to productpage service

    IG->>PP: Forward request

    Note over PP: Call reviews service

    PP->>RV: GET /reviews

    Note over RV: VirtualService: reviews<br/>90% → v1, 10% → v2<br/>DestinationRule applies

    alt Reviews v2 selected (10%)
        RV->>RT: GET /ratings
        Note over RT: DestinationRule:<br/>- Circuit breaker<br/>- Connection pool
        RT->>RV: Ratings response
    else Reviews v1 selected (90%)
        Note over RV: v1 doesn't call ratings
    end

    RV->>PP: Reviews response
    PP->>IG: Page response
    IG->>U: HTTP 200
```

## Summary

This document covered Istio's configuration resources:

1. **VirtualService**: Routing rules, traffic splitting, retries, timeouts
2. **DestinationRule**: Load balancing, connection pools, circuit breakers
3. **Gateway**: Ingress/egress configuration, TLS termination
4. **ServiceEntry**: External service registration, mesh expansion

### Key Translation Mappings

| Istio CRD | Primary Envoy xDS | Purpose |
|-----------|-------------------|---------|
| Gateway | LDS (Listeners) | Port binding, TLS config |
| VirtualService | RDS (Routes) | Traffic routing rules |
| DestinationRule | CDS (Clusters) | Backend policies |
| ServiceEntry | EDS (Endpoints) | Service discovery |

## Next Steps

Continue to **Part 3: xDS Protocol Deep Dive** to understand how these configurations are transmitted from Istiod to Envoy.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Documentation**:
- [Istio Traffic Management](https://istio.io/latest/docs/concepts/traffic-management/)
- [Istio Configuration Reference](https://istio.io/latest/docs/reference/config/)
