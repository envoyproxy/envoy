# Part 1: Istio-Envoy Architecture Overview

## Table of Contents
1. [Introduction](#introduction)
2. [Istio Architecture](#istio-architecture)
3. [Envoy as Data Plane](#envoy-as-data-plane)
4. [Control Plane to Data Plane Communication](#control-plane-to-data-plane-communication)
5. [xDS Protocol Overview](#xds-protocol-overview)
6. [Configuration Flow Overview](#configuration-flow-overview)

## Introduction

Istio is a service mesh that provides traffic management, security, and observability for microservices. Envoy proxy serves as the data plane, with Istio's control plane (Istiod) managing configuration distribution to all Envoy proxies in the mesh.

This document series explains how configuration flows from Istio to Envoy, how it's processed, and how it affects runtime behavior.

## Istio Architecture

### High-Level Architecture

```mermaid
graph TB
    subgraph "Istio Control Plane"
        A[Istiod]
        A --> A1[Pilot - Service Discovery]
        A --> A2[Citadel - Certificate Management]
        A --> A3[Galley - Configuration Validation]
    end

    subgraph "Data Plane - Service A"
        B[App Container A]
        C[Envoy Sidecar A]
    end

    subgraph "Data Plane - Service B"
        D[App Container B]
        E[Envoy Sidecar B]
    end

    subgraph "Data Plane - Service C"
        F[App Container C]
        G[Envoy Sidecar C]
    end

    subgraph "Kubernetes API"
        K8S[Kubernetes API Server]
    end

    subgraph "Configuration Sources"
        CR1[VirtualService]
        CR2[DestinationRule]
        CR3[Gateway]
        CR4[ServiceEntry]
    end

    K8S --> A
    CR1 --> K8S
    CR2 --> K8S
    CR3 --> K8S
    CR4 --> K8S

    A -.xDS Protocol.-> C
    A -.xDS Protocol.-> E
    A -.xDS Protocol.-> G

    B <--> C
    D <--> E
    F <--> G

    C <-.Service Traffic.-> E
    E <-.Service Traffic.-> G

    style A fill:#326CE5,color:#fff
    style C fill:#F24C3D,color:#fff
    style E fill:#F24C3D,color:#fff
    style G fill:#F24C3D,color:#fff
```

### Components Breakdown

```mermaid
graph LR
    subgraph "Istiod Components"
        direction TB
        P[Pilot]
        P --> PS[Service Discovery]
        P --> PC[Config Distribution]
        P --> PP[xDS Server]

        C[Citadel]
        C --> CC[CA Server]
        C --> CS[Certificate Signing]

        G[Galley]
        G --> GV[Config Validation]
        G --> GT[Config Translation]
    end

    subgraph "Configuration Input"
        I1[Kubernetes Services]
        I2[Istio CRDs]
        I3[Service Registry]
    end

    subgraph "Configuration Output"
        O1[LDS - Listener Discovery]
        O2[RDS - Route Discovery]
        O3[CDS - Cluster Discovery]
        O4[EDS - Endpoint Discovery]
        O5[SDS - Secret Discovery]
    end

    I1 --> P
    I2 --> G
    I3 --> P

    G --> P

    P --> O1
    P --> O2
    P --> O3
    P --> O4
    C --> O5

    style P fill:#4A90E2,color:#fff
    style C fill:#50C878,color:#fff
    style G fill:#F39C12,color:#fff
```

## Envoy as Data Plane

### Envoy Sidecar Deployment

```mermaid
graph TB
    subgraph "Kubernetes Pod"
        direction LR
        subgraph "Init Containers"
            INIT[istio-init<br/>iptables rules]
        end

        subgraph "Main Containers"
            APP[Application<br/>Container]
            ENVOY[Envoy Sidecar<br/>istio-proxy]
        end
    end

    subgraph "Envoy Configuration"
        BOOT[Bootstrap Config<br/>- Admin port<br/>- xDS server address<br/>- Node identity]
        DYN[Dynamic Config<br/>- Listeners<br/>- Routes<br/>- Clusters<br/>- Endpoints]
    end

    subgraph "Istiod"
        XDS[xDS Server]
    end

    INIT -.Configure iptables.-> APP
    INIT -.Configure iptables.-> ENVOY

    BOOT --> ENVOY
    XDS -.Push Config.-> DYN
    DYN --> ENVOY

    APP <-.All traffic intercepted.-> ENVOY
    ENVOY <-.gRPC Stream.-> XDS

    style ENVOY fill:#F24C3D,color:#fff
    style XDS fill:#326CE5,color:#fff
    style BOOT fill:#95A5A6
    style DYN fill:#3498DB,color:#fff
```

### Envoy Bootstrap Configuration

```mermaid
graph TB
    subgraph "Bootstrap Config Structure"
        ROOT[envoy.yaml]

        ROOT --> NODE[Node Identity<br/>- ID<br/>- Cluster<br/>- Metadata]
        ROOT --> ADMIN[Admin Interface<br/>- Port 15000<br/>- Access log]
        ROOT --> STATIC[Static Resources<br/>- Prometheus listener<br/>- Health check listener]
        ROOT --> DYNAMIC[Dynamic Resources<br/>- LDS config<br/>- CDS config]
        ROOT --> STATS[Stats Configuration<br/>- Sinks<br/>- Tag extraction]

        DYNAMIC --> LDS_CONFIG[LDS Config<br/>- API type: GRPC<br/>- Cluster: xds-grpc]
        DYNAMIC --> CDS_CONFIG[CDS Config<br/>- API type: GRPC<br/>- Cluster: xds-grpc]

        LDS_CONFIG --> XDS_CLUSTER[xds-grpc Cluster<br/>- Istiod address<br/>- TLS config<br/>- HTTP/2]
    end

    style ROOT fill:#E74C3C,color:#fff
    style DYNAMIC fill:#3498DB,color:#fff
    style XDS_CLUSTER fill:#326CE5,color:#fff
```

## Control Plane to Data Plane Communication

### Communication Flow

```mermaid
sequenceDiagram
    participant E as Envoy Proxy
    participant I as Istiod (Pilot)
    participant K as Kubernetes API

    Note over E: Envoy starts with<br/>bootstrap config

    E->>I: Establish gRPC stream<br/>mTLS authenticated
    activate I

    Note over I: Verify proxy identity<br/>from certificate

    I-->>E: Stream established

    E->>I: DiscoveryRequest (LDS)
    Note over I: Generate listeners<br/>based on proxy identity
    I-->>E: DiscoveryResponse (LDS)

    E->>I: ACK/NACK

    E->>I: DiscoveryRequest (CDS)
    Note over I: Generate clusters<br/>based on services
    I-->>E: DiscoveryResponse (CDS)

    E->>I: DiscoveryRequest (RDS)
    Note over I: Generate routes<br/>for virtual hosts
    I-->>E: DiscoveryResponse (RDS)

    E->>I: DiscoveryRequest (EDS)
    Note over I: Get endpoints<br/>from service registry
    K->>I: Watch Services/Endpoints
    I-->>E: DiscoveryResponse (EDS)

    deactivate I

    Note over E: Config applied<br/>Proxy ready

    Note over K: Service endpoint changes
    K->>I: Endpoint update event
    activate I
    I->>E: DiscoveryResponse (EDS)<br/>Push update
    E->>I: ACK
    deactivate I
```

### xDS Protocol Stack

```mermaid
graph TB
    subgraph "Transport Layer"
        T1[gRPC HTTP/2]
        T2[mTLS - Mutual Authentication]
        T3[Bidirectional Streaming]
    end

    subgraph "Protocol Layer"
        P1[xDS Protocol]
        P2[Aggregated Discovery Service - ADS]
        P3[Incremental xDS]
    end

    subgraph "Resource Types"
        R1[LDS - Listeners]
        R2[RDS - Routes]
        R3[CDS - Clusters]
        R4[EDS - Endpoints]
        R5[SDS - Secrets]
        R6[ECDS - Extension Config]
        R7[RTDS - Runtime]
    end

    T1 --> P1
    T2 --> P1
    T3 --> P1

    P1 --> P2
    P2 --> P3

    P3 --> R1
    P3 --> R2
    P3 --> R3
    P3 --> R4
    P3 --> R5
    P3 --> R6
    P3 --> R7

    style P2 fill:#E74C3C,color:#fff
    style T2 fill:#27AE60,color:#fff
```

## xDS Protocol Overview

### xDS API Types

```mermaid
graph LR
    subgraph "Discovery APIs"
        LDS[LDS<br/>Listener Discovery Service]
        RDS[RDS<br/>Route Discovery Service]
        CDS[CDS<br/>Cluster Discovery Service]
        EDS[EDS<br/>Endpoint Discovery Service]
        SDS[SDS<br/>Secret Discovery Service]
        ADS[ADS<br/>Aggregated Discovery Service]
    end

    subgraph "Envoy Configuration"
        L[Listeners]
        R[Routes]
        C[Clusters]
        EP[Endpoints]
        S[Secrets/Certs]
    end

    LDS --> L
    RDS --> R
    CDS --> C
    EDS --> EP
    SDS --> S

    ADS -.Aggregates.-> LDS
    ADS -.Aggregates.-> RDS
    ADS -.Aggregates.-> CDS
    ADS -.Aggregates.-> EDS
    ADS -.Aggregates.-> SDS

    L -.References.-> R
    R -.References.-> C
    C -.References.-> EP

    style ADS fill:#E74C3C,color:#fff
    style L fill:#3498DB,color:#fff
    style R fill:#9B59B6,color:#fff
    style C fill:#F39C12,color:#fff
    style EP fill:#1ABC9C,color:#fff
```

### xDS Resource Dependencies

```mermaid
graph TD
    LDS[Listener Discovery<br/>LDS]
    RDS[Route Discovery<br/>RDS]
    CDS[Cluster Discovery<br/>CDS]
    EDS[Endpoint Discovery<br/>EDS]
    SDS[Secret Discovery<br/>SDS]

    LDS -->|References| RDS
    LDS -->|References| CDS
    RDS -->|References| CDS
    CDS -->|References| EDS
    LDS -->|References| SDS
    CDS -->|References| SDS

    style LDS fill:#3498DB,color:#fff
    style RDS fill:#9B59B6,color:#fff
    style CDS fill:#F39C12,color:#fff
    style EDS fill:#1ABC9C,color:#fff
    style SDS fill:#E74C3C,color:#fff
```

## Configuration Flow Overview

### End-to-End Configuration Flow

```mermaid
graph TB
    subgraph "1. Configuration Input"
        VS[VirtualService]
        DR[DestinationRule]
        GW[Gateway]
        SE[ServiceEntry]
    end

    subgraph "2. Kubernetes API"
        K8S[K8s API Server]
        WATCH[Watch Resources]
    end

    subgraph "3. Istiod Processing"
        VALIDATE[Config Validation]
        TRANSLATE[Config Translation]
        AGGREGATE[Aggregate Configs]
        GENERATE[Generate xDS]
    end

    subgraph "4. xDS Distribution"
        CACHE[Config Cache]
        PUSH[Push to Proxies]
    end

    subgraph "5. Envoy Processing"
        RECEIVE[Receive xDS]
        VALIDATE_E[Validate Config]
        APPLY[Apply Config]
        WARM[Warm Clusters]
        ACTIVATE[Activate]
    end

    subgraph "6. Runtime State"
        LISTENERS[Active Listeners]
        ROUTES[Active Routes]
        CLUSTERS[Active Clusters]
        ENDPOINTS[Active Endpoints]
    end

    VS --> K8S
    DR --> K8S
    GW --> K8S
    SE --> K8S

    K8S --> WATCH
    WATCH --> VALIDATE

    VALIDATE --> TRANSLATE
    TRANSLATE --> AGGREGATE
    AGGREGATE --> GENERATE

    GENERATE --> CACHE
    CACHE --> PUSH

    PUSH --> RECEIVE
    RECEIVE --> VALIDATE_E
    VALIDATE_E --> APPLY
    APPLY --> WARM
    WARM --> ACTIVATE

    ACTIVATE --> LISTENERS
    ACTIVATE --> ROUTES
    ACTIVATE --> CLUSTERS
    ACTIVATE --> ENDPOINTS

    style VALIDATE fill:#27AE60,color:#fff
    style GENERATE fill:#3498DB,color:#fff
    style PUSH fill:#E74C3C,color:#fff
    style APPLY fill:#9B59B6,color:#fff
    style ACTIVATE fill:#F39C12,color:#fff
```

### Configuration Lifecycle States

```mermaid
stateDiagram-v2
    [*] --> Watching: Istiod starts

    Watching --> Validating: Config change detected
    Validating --> Invalid: Validation fails
    Invalid --> Watching: Error logged

    Validating --> Translating: Valid config
    Translating --> Generating: Translation complete

    Generating --> Caching: xDS generated
    Caching --> Pushing: Proxy subscribed

    Pushing --> Receiving: gRPC stream
    Receiving --> Validating_Envoy: Config received

    Validating_Envoy --> Rejected: Invalid config
    Rejected --> Caching: NACK sent

    Validating_Envoy --> Applying: Valid config
    Applying --> Warming: Dependencies resolved

    Warming --> Active: Clusters warmed
    Active --> [*]: Config active

    Active --> Draining: New config arrives
    Draining --> Watching: Old config drained
```

## Key Concepts

### Configuration Update Model

```mermaid
graph TB
    subgraph "Push Model"
        P1[Config Change]
        P2[Istiod Detects]
        P3[Push to All Proxies]
        P4[Immediate Update]

        P1 --> P2
        P2 --> P3
        P3 --> P4
    end

    subgraph "Request-Response Model"
        R1[Envoy Requests]
        R2[Istiod Responds]
        R3[Envoy Polls]

        R1 --> R2
        R2 --> R3
        R3 --> R1
    end

    subgraph "Istio xDS Model"
        I1[Streaming gRPC]
        I2[Bidirectional]
        I3[Request + Push]
        I4[Incremental Updates]

        I1 --> I2
        I2 --> I3
        I3 --> I4
    end

    style I1 fill:#E74C3C,color:#fff
    style I2 fill:#3498DB,color:#fff
    style I3 fill:#27AE60,color:#fff
```

### Version and Nonce Handling

```mermaid
sequenceDiagram
    participant E as Envoy
    participant I as Istiod

    Note over E,I: Initial Request
    E->>I: DiscoveryRequest<br/>version: ""<br/>nonce: ""

    I->>E: DiscoveryResponse<br/>version: "v1"<br/>nonce: "n1"<br/>resources: [...]

    Note over E: Apply config successfully

    E->>I: DiscoveryRequest (ACK)<br/>version: "v1"<br/>nonce: "n1"

    Note over I: Config changes

    I->>E: DiscoveryResponse<br/>version: "v2"<br/>nonce: "n2"<br/>resources: [...]

    Note over E: Validation fails

    E->>I: DiscoveryRequest (NACK)<br/>version: "v1"<br/>nonce: "n2"<br/>error_detail: "..."

    Note over I: Keep using v1<br/>Log error

    I->>E: DiscoveryResponse<br/>version: "v1"<br/>nonce: "n3"<br/>resources: [...]

    E->>I: DiscoveryRequest (ACK)<br/>version: "v1"<br/>nonce: "n3"
```

## Summary

This document provides an overview of the Istio-Envoy architecture:

1. **Istio Control Plane**: Istiod manages service mesh configuration
2. **Envoy Data Plane**: Sidecar proxies handle all service traffic
3. **xDS Protocol**: gRPC-based streaming protocol for config distribution
4. **Configuration Flow**: From Kubernetes CRDs to active Envoy configuration
5. **Update Model**: Streaming, bidirectional, incremental updates

### Key Takeaways

- Istio uses Envoy as its data plane proxy
- Configuration flows from Istio CRDs → Kubernetes API → Istiod → Envoy
- xDS protocol provides dynamic, zero-downtime configuration updates
- Multiple xDS APIs (LDS, RDS, CDS, EDS, SDS) with dependencies
- Version control with ACK/NACK mechanism ensures reliability

## Next Steps

Continue to **Part 2: Istio Configuration Resources** to understand the Istio CRDs that define service mesh behavior.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Documentation**:
- [Envoy xDS Protocol](https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol)
- [Istio Architecture](https://istio.io/latest/docs/ops/deployment/architecture/)
