# Part 3: xDS Protocol Deep Dive

## Table of Contents
1. [Introduction](#introduction)
2. [xDS Protocol Fundamentals](#xds-protocol-fundamentals)
3. [Discovery Request and Response](#discovery-request-and-response)
4. [Resource Types](#resource-types)
5. [Aggregated Discovery Service (ADS)](#aggregated-discovery-service-ads)
6. [Incremental xDS](#incremental-xds)
7. [Version Control and ACK/NACK](#version-control-and-acknack)
8. [Error Handling](#error-handling)

## Introduction

The xDS (Discovery Service) protocol is the mechanism by which Envoy dynamically discovers its configuration from a management server (Istiod in Istio's case). This document explores the protocol in detail.

## xDS Protocol Fundamentals

### Protocol Stack

```mermaid
graph TB
    subgraph "Transport Layer"
        HTTP2[HTTP/2]
        GRPC[gRPC]
        TLS[TLS 1.2+<br/>mTLS Authentication]
    end

    subgraph "xDS Layer"
        STREAM[Bidirectional Streaming]
        REQ[DiscoveryRequest]
        RESP[DiscoveryResponse]
    end

    subgraph "Resource Layer"
        LDS[Listener Discovery Service]
        RDS[Route Discovery Service]
        CDS[Cluster Discovery Service]
        EDS[Endpoint Discovery Service]
        SDS[Secret Discovery Service]
        RTDS[Runtime Discovery Service]
        ECDS[Extension Config Discovery]
    end

    HTTP2 --> GRPC
    GRPC --> TLS
    TLS --> STREAM

    STREAM --> REQ
    STREAM --> RESP

    REQ --> LDS
    REQ --> RDS
    REQ --> CDS
    REQ --> EDS
    REQ --> SDS
    REQ --> RTDS
    REQ --> ECDS

    RESP --> LDS
    RESP --> RDS
    RESP --> CDS
    RESP --> EDS
    RESP --> SDS
    RESP --> RTDS
    RESP --> ECDS

    style GRPC fill:#E74C3C,color:#fff
    style TLS fill:#27AE60,color:#fff
    style STREAM fill:#3498DB,color:#fff
```

### xDS Service Methods

```mermaid
classDiagram
    class AggregatedDiscoveryService {
        <<interface>>
        +StreamAggregatedResources(stream) stream
        +DeltaAggregatedResources(stream) stream
    }

    class ListenerDiscoveryService {
        <<interface>>
        +StreamListeners(stream) stream
        +DeltaListeners(stream) stream
        +FetchListeners(request) response
    }

    class RouteDiscoveryService {
        <<interface>>
        +StreamRoutes(stream) stream
        +DeltaRoutes(stream) stream
        +FetchRoutes(request) response
    }

    class ClusterDiscoveryService {
        <<interface>>
        +StreamClusters(stream) stream
        +DeltaClusters(stream) stream
        +FetchClusters(request) response
    }

    class EndpointDiscoveryService {
        <<interface>>
        +StreamEndpoints(stream) stream
        +DeltaEndpoints(stream) stream
        +FetchEndpoints(request) response
    }

    AggregatedDiscoveryService --> ListenerDiscoveryService: Aggregates
    AggregatedDiscoveryService --> RouteDiscoveryService: Aggregates
    AggregatedDiscoveryService --> ClusterDiscoveryService: Aggregates
    AggregatedDiscoveryService --> EndpointDiscoveryService: Aggregates
```

### Stream Types

```mermaid
graph LR
    subgraph "State of the World (SotW)"
        SOW1[Client subscribes]
        SOW2[Server sends ALL resources]
        SOW3[Client ACKs/NACKs]
        SOW4[Server sends updates<br/>ALL resources again]

        SOW1 --> SOW2
        SOW2 --> SOW3
        SOW3 --> SOW4
        SOW4 --> SOW3
    end

    subgraph "Incremental/Delta xDS"
        INC1[Client subscribes]
        INC2[Server sends resources]
        INC3[Client ACKs/NACKs]
        INC4[Server sends ONLY changes<br/>Added/Modified/Removed]

        INC1 --> INC2
        INC2 --> INC3
        INC3 --> INC4
        INC4 --> INC3
    end

    style SOW2 fill:#3498DB,color:#fff
    style INC4 fill:#27AE60,color:#fff
```

## Discovery Request and Response

### DiscoveryRequest Structure

```mermaid
classDiagram
    class DiscoveryRequest {
        +string version_info
        +Node node
        +repeated string resource_names
        +string type_url
        +string response_nonce
        +Status error_detail
    }

    class Node {
        +string id
        +string cluster
        +Struct metadata
        +Locality locality
        +BuildVersion user_agent_build_version
    }

    class Locality {
        +string region
        +string zone
        +string sub_zone
    }

    DiscoveryRequest --> Node
    Node --> Locality

    note for DiscoveryRequest "version_info: Last accepted version\nresponse_nonce: Nonce from last response\nerror_detail: Present in NACK"

    note for Node "id: Unique proxy identifier\ncluster: Cluster name\nmetadata: Custom metadata"
```

### DiscoveryResponse Structure

```mermaid
classDiagram
    class DiscoveryResponse {
        +string version_info
        +repeated Any resources
        +bool canary
        +string type_url
        +string nonce
        +ControlPlane control_plane
    }

    class Any {
        +string type_url
        +bytes value
    }

    class ControlPlane {
        +string identifier
    }

    DiscoveryResponse --> Any: contains
    DiscoveryResponse --> ControlPlane

    note for DiscoveryResponse "version_info: New config version\nnonce: Unique request identifier\nresources: Actual config resources"

    note for Any "Protobuf Any type\nContains serialized xDS resources\n(Listener, Route, Cluster, etc.)"
```

### Request-Response Flow

```mermaid
sequenceDiagram
    participant E as Envoy
    participant I as Istiod

    Note over E,I: Initial Connection

    E->>I: gRPC Stream Established<br/>(mTLS authenticated)

    Note over E: Node metadata sent:<br/>- Proxy ID<br/>- Cluster name<br/>- Pod labels<br/>- Namespace

    rect rgb(200, 220, 240)
        Note over E,I: LDS Discovery
        E->>I: DiscoveryRequest<br/>type_url: type.googleapis.com/.../Listener<br/>version_info: ""<br/>nonce: ""

        Note over I: Generate listeners<br/>based on proxy identity

        I->>E: DiscoveryResponse<br/>version_info: "v1"<br/>nonce: "n1"<br/>resources: [listener1, listener2, ...]

        E->>I: DiscoveryRequest (ACK)<br/>version_info: "v1"<br/>nonce: "n1"
    end

    rect rgb(220, 240, 220)
        Note over E,I: RDS Discovery (for each listener)
        E->>I: DiscoveryRequest<br/>type_url: type.googleapis.com/.../RouteConfiguration<br/>resource_names: ["route_config_name"]

        I->>E: DiscoveryResponse<br/>version_info: "v1"<br/>nonce: "n2"<br/>resources: [route_config]

        E->>I: DiscoveryRequest (ACK)<br/>version_info: "v1"<br/>nonce: "n2"
    end

    rect rgb(240, 220, 220)
        Note over E,I: CDS Discovery
        E->>I: DiscoveryRequest<br/>type_url: type.googleapis.com/.../Cluster

        I->>E: DiscoveryResponse<br/>version_info: "v1"<br/>nonce: "n3"<br/>resources: [cluster1, cluster2, ...]

        E->>I: DiscoveryRequest (ACK)<br/>version_info: "v1"<br/>nonce: "n3"
    end

    rect rgb(240, 240, 200)
        Note over E,I: EDS Discovery (for each cluster)
        E->>I: DiscoveryRequest<br/>type_url: type.googleapis.com/.../ClusterLoadAssignment<br/>resource_names: ["cluster1", "cluster2"]

        I->>E: DiscoveryResponse<br/>version_info: "v1"<br/>nonce: "n4"<br/>resources: [endpoints1, endpoints2]

        E->>I: DiscoveryRequest (ACK)<br/>version_info: "v1"<br/>nonce: "n4"
    end

    Note over E: Configuration applied<br/>Proxy ready
```

## Resource Types

### xDS Resource Type URLs

```mermaid
graph TB
    subgraph "Core xDS Types"
        LDS[type.googleapis.com/envoy.config.listener.v3.Listener]
        RDS[type.googleapis.com/envoy.config.route.v3.RouteConfiguration]
        CDS[type.googleapis.com/envoy.config.cluster.v3.Cluster]
        EDS[type.googleapis.com/envoy.config.endpoint.v3.ClusterLoadAssignment]
    end

    subgraph "Security xDS Types"
        SDS[type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret]
    end

    subgraph "Extension xDS Types"
        ECDS[type.googleapis.com/envoy.config.core.v3.TypedExtensionConfig]
        RTDS[type.googleapis.com/envoy.service.runtime.v3.Runtime]
    end

    subgraph "Mesh-specific Types"
        WASM[type.googleapis.com/envoy.extensions.filters.http.wasm.v3.Wasm]
    end

    style LDS fill:#3498DB,color:#fff
    style RDS fill:#9B59B6,color:#fff
    style CDS fill:#F39C12,color:#fff
    style EDS fill:#27AE60,color:#fff
    style SDS fill:#E74C3C,color:#fff
```

### Resource Dependencies

```mermaid
graph TD
    LDS[Listener<br/>LDS]
    RDS[RouteConfiguration<br/>RDS]
    CDS[Cluster<br/>CDS]
    EDS[ClusterLoadAssignment<br/>EDS]
    SDS[Secret<br/>SDS]

    LDS -->|route_config_name| RDS
    LDS -->|cluster in filter| CDS
    LDS -->|tls_context.sds_config| SDS

    RDS -->|cluster in route| CDS

    CDS -->|eds_cluster_config| EDS
    CDS -->|transport_socket.sds_config| SDS

    style LDS fill:#3498DB,color:#fff
    style RDS fill:#9B59B6,color:#fff
    style CDS fill:#F39C12,color:#fff
    style EDS fill:#27AE60,color:#fff
    style SDS fill:#E74C3C,color:#fff
```

### Listener Resource Example Flow

```mermaid
graph TB
    subgraph "Listener Resource"
        L[Listener: 0.0.0.0:80]
        L --> FC[FilterChain]
        FC --> HCM[HttpConnectionManager]
        HCM --> RDS_REF[RDS Reference:<br/>route_config_name]
    end

    subgraph "Route Resource"
        RC[RouteConfiguration:<br/>route_config_name]
        RC --> VH[VirtualHost]
        VH --> ROUTE[Route]
        ROUTE --> CLUSTER_REF[Cluster Reference:<br/>outbound|80||reviews.default]
    end

    subgraph "Cluster Resource"
        C[Cluster:<br/>outbound|80||reviews.default]
        C --> EDS_REF[EDS Reference:<br/>service_name]
    end

    subgraph "Endpoint Resource"
        CLA[ClusterLoadAssignment:<br/>service_name]
        CLA --> LBE[LocalityLbEndpoints]
        LBE --> EP[Endpoint: 10.244.0.5:8080]
    end

    RDS_REF -.Resolves to.-> RC
    CLUSTER_REF -.Resolves to.-> C
    EDS_REF -.Resolves to.-> CLA

    style L fill:#3498DB,color:#fff
    style RC fill:#9B59B6,color:#fff
    style C fill:#F39C12,color:#fff
    style CLA fill:#27AE60,color:#fff
```

## Aggregated Discovery Service (ADS)

### ADS Architecture

```mermaid
graph TB
    subgraph "Without ADS - Multiple Streams"
        E1[Envoy]
        E1 -->|LDS Stream| I1[Istiod]
        E1 -->|RDS Stream| I1
        E1 -->|CDS Stream| I1
        E1 -->|EDS Stream| I1
        E1 -->|SDS Stream| I1

        Note1[5 separate gRPC streams<br/>- Harder to synchronize<br/>- Potential inconsistency<br/>- More overhead]
    end

    subgraph "With ADS - Single Stream"
        E2[Envoy]
        E2 <-->|Single ADS Stream<br/>All resource types| I2[Istiod]

        Note2[1 gRPC stream<br/>- Ordered updates<br/>- Consistent state<br/>- Efficient]
    end

    style E2 fill:#27AE60,color:#fff
    style I2 fill:#326CE5,color:#fff
```

### ADS Sequencing and Ordering

```mermaid
sequenceDiagram
    participant E as Envoy
    participant I as Istiod (ADS)

    Note over E,I: Single ADS Stream

    E->>I: StreamAggregatedResources()

    Note over E,I: Request all types via ADS

    rect rgb(200, 220, 240)
        Note over I: Phase 1: Send CDS first
        I->>E: DiscoveryResponse (CDS)<br/>nonce: "n1"
        E->>I: DiscoveryRequest (ACK) CDS<br/>nonce: "n1"
    end

    rect rgb(220, 200, 240)
        Note over I: Phase 2: Send EDS<br/>(after CDS ACK)
        I->>E: DiscoveryResponse (EDS)<br/>nonce: "n2"
        E->>I: DiscoveryRequest (ACK) EDS<br/>nonce: "n2"
    end

    rect rgb(240, 220, 200)
        Note over I: Phase 3: Send LDS<br/>(after CDS+EDS ready)
        I->>E: DiscoveryResponse (LDS)<br/>nonce: "n3"
        E->>I: DiscoveryRequest (ACK) LDS<br/>nonce: "n3"
    end

    rect rgb(200, 240, 220)
        Note over I: Phase 4: Send RDS<br/>(after LDS ACK)
        I->>E: DiscoveryResponse (RDS)<br/>nonce: "n4"
        E->>I: DiscoveryRequest (ACK) RDS<br/>nonce: "n4"
    end

    Note over E: All resources consistent<br/>Configuration active

    Note over I: Configuration update

    I->>E: DiscoveryResponse (CDS)<br/>nonce: "n5"<br/>(New cluster added)
    E->>I: DiscoveryRequest (ACK) CDS<br/>nonce: "n5"

    I->>E: DiscoveryResponse (EDS)<br/>nonce: "n6"<br/>(Endpoints for new cluster)
    E->>I: DiscoveryRequest (ACK) EDS<br/>nonce: "n6"

    I->>E: DiscoveryResponse (RDS)<br/>nonce: "n7"<br/>(Route to new cluster)
    E->>I: DiscoveryRequest (ACK) RDS<br/>nonce: "n7"
```

### ADS Ordering Guarantees

```mermaid
graph TB
    ORDER[ADS Ordering Rules]

    ORDER --> RULE1[CDS must be sent/ACKed<br/>before dependent EDS]
    ORDER --> RULE2[EDS must be sent/ACKed<br/>before LDS referencing clusters]
    ORDER --> RULE3[LDS must be sent/ACKed<br/>before dependent RDS]
    ORDER --> RULE4[RDS can only reference<br/>clusters in CDS]

    RULE1 --> BENEFIT1[Ensures clusters exist<br/>before endpoints]
    RULE2 --> BENEFIT2[Ensures backends available<br/>before listeners accept traffic]
    RULE3 --> BENEFIT3[Ensures listeners exist<br/>before routes configured]
    RULE4 --> BENEFIT4[Prevents routing to<br/>non-existent clusters]

    style ORDER fill:#E74C3C,color:#fff
    style BENEFIT1 fill:#27AE60,color:#fff
    style BENEFIT2 fill:#27AE60,color:#fff
    style BENEFIT3 fill:#27AE60,color:#fff
    style BENEFIT4 fill:#27AE60,color:#fff
```

## Incremental xDS

### Incremental vs State of the World

```mermaid
graph TB
    subgraph "State of the World (SotW)"
        SOW_REQ[Request: Subscribe to clusters]
        SOW_RESP1[Response: ALL clusters<br/>cluster1, cluster2, ..., cluster100]
        SOW_UPDATE[Update: ONE cluster changed]
        SOW_RESP2[Response: ALL clusters again<br/>cluster1, cluster2, ..., cluster100]

        SOW_REQ --> SOW_RESP1
        SOW_RESP1 --> SOW_UPDATE
        SOW_UPDATE --> SOW_RESP2

        Note_SOW[Inefficient for large configs:<br/>- High bandwidth<br/>- Long processing time<br/>- Entire state retransmitted]
    end

    subgraph "Incremental/Delta"
        INC_REQ[Request: Subscribe to clusters]
        INC_RESP1[Response: ALL clusters<br/>cluster1, cluster2, ..., cluster100]
        INC_UPDATE[Update: ONE cluster changed]
        INC_RESP2[Response: ONLY changed cluster<br/>cluster42 (modified)]

        INC_REQ --> INC_RESP1
        INC_RESP1 --> INC_UPDATE
        INC_UPDATE --> INC_RESP2

        Note_INC[Efficient:<br/>- Low bandwidth<br/>- Fast processing<br/>- Only deltas sent]
    end

    style INC_RESP2 fill:#27AE60,color:#fff
    style Note_INC fill:#27AE60,color:#fff
```

### Delta Discovery Protocol

```mermaid
sequenceDiagram
    participant E as Envoy
    participant I as Istiod

    Note over E,I: Initial Subscription

    E->>I: DeltaDiscoveryRequest<br/>resource_names_subscribe: ["*"]

    I->>E: DeltaDiscoveryResponse<br/>resources: [all clusters]<br/>system_version_info: "v1"

    E->>I: DeltaDiscoveryRequest (ACK)

    Note over E: Using v1 config

    Note over I: cluster-new added<br/>cluster-old removed

    I->>E: DeltaDiscoveryResponse<br/>resources: [cluster-new]<br/>removed_resources: ["cluster-old"]<br/>system_version_info: "v2"

    E->>I: DeltaDiscoveryRequest (ACK)

    Note over E: Updated to v2<br/>cluster-new active<br/>cluster-old removed

    Note over I: cluster-new modified

    I->>E: DeltaDiscoveryResponse<br/>resources: [cluster-new (modified)]<br/>system_version_info: "v3"

    E->>I: DeltaDiscoveryRequest (ACK)
```

### Resource Subscription Management

```mermaid
stateDiagram-v2
    [*] --> Unsubscribed

    Unsubscribed --> Subscribed: resource_names_subscribe
    Subscribed --> Unsubscribed: resource_names_unsubscribe

    Subscribed --> Wildcard: Subscribe to "*"
    Wildcard --> Subscribed: Subscribe to specific names

    state Subscribed {
        [*] --> Waiting
        Waiting --> Received: Resource in response
        Received --> Applied: Validation success
        Applied --> Updated: New version received
        Updated --> Applied: Validation success
        Received --> Rejected: Validation failed
        Rejected --> Waiting: Retry
    }

    note right of Wildcard
        Wildcard subscription:
        - Receive all resources
        - Automatic updates
        - No explicit subscription needed
    end note

    note right of Subscribed
        Explicit subscription:
        - Only subscribed resources sent
        - On-demand updates
        - Fine-grained control
    end note
```

## Version Control and ACK/NACK

### Version and Nonce Mechanism

```mermaid
graph TB
    subgraph "Version vs Nonce"
        V[Version Info]
        N[Nonce]

        V --> V1[Identifies config version<br/>e.g., "v1", "v2"]
        V --> V2[Updated when config changes]
        V --> V3[Envoy tracks accepted version]

        N --> N1[Unique per response<br/>e.g., UUID]
        N --> N2[Used for ACK/NACK matching]
        N --> N3[Not tied to config version]
    end

    subgraph "ACK Example"
        ACK1[Response: version=v2, nonce=abc123]
        ACK2[Request: version_info=v2, response_nonce=abc123]
        ACK3[Meaning: Successfully applied v2]

        ACK1 --> ACK2
        ACK2 --> ACK3
    end

    subgraph "NACK Example"
        NACK1[Response: version=v2, nonce=def456]
        NACK2[Request: version_info=v1, response_nonce=def456, error_detail=...]
        NACK3[Meaning: Rejected v2, still using v1]

        NACK1 --> NACK2
        NACK2 --> NACK3
    end

    style ACK3 fill:#27AE60,color:#fff
    style NACK3 fill:#E74C3C,color:#fff
```

### ACK/NACK Flow

```mermaid
sequenceDiagram
    participant E as Envoy
    participant I as Istiod

    Note over E,I: Current version: v1

    I->>E: DiscoveryResponse<br/>version_info: "v2"<br/>nonce: "nonce-123"<br/>resources: [...]

    alt Validation Success
        Note over E: Apply v2 successfully

        E->>I: DiscoveryRequest (ACK)<br/>version_info: "v2"<br/>response_nonce: "nonce-123"

        Note over I: v2 accepted by client

    else Validation Failure
        Note over E: Validation error<br/>Keep using v1

        E->>I: DiscoveryRequest (NACK)<br/>version_info: "v1"<br/>response_nonce: "nonce-123"<br/>error_detail: "Invalid cluster config"

        Note over I: v2 rejected<br/>Log error<br/>Client still on v1

        I->>E: DiscoveryResponse<br/>version_info: "v1"<br/>nonce: "nonce-124"<br/>(Resend v1 or fix v2)
    end
```

### Error States and Recovery

```mermaid
stateDiagram-v2
    [*] --> Active_v1: Initial config applied

    Active_v1 --> Validating_v2: New config received

    state Validating_v2 {
        [*] --> Parsing
        Parsing --> Checking: Parse success
        Parsing --> Error: Parse failed

        Checking --> Dependencies: Syntax valid
        Dependencies --> Complete: All deps satisfied
        Dependencies --> Error: Missing dependencies

        Complete --> [*]: Validation passed
        Error --> [*]: Validation failed
    }

    Validating_v2 --> Active_v2: ACK sent
    Validating_v2 --> Active_v1: NACK sent<br/>(continue with v1)

    Active_v2 --> Validating_v3: New config received
    Active_v1 --> Validating_v2_fixed: Fixed v2 received

    note right of Active_v1
        Current active version
        Serving traffic normally
    end note

    note right of Validating_v2
        New version being validated
        Old version still active
        Zero-downtime updates
    end note
```

## Error Handling

### Error Categories

```mermaid
graph TB
    ERRORS[xDS Errors]

    ERRORS --> TRANSPORT[Transport Errors]
    ERRORS --> PROTOCOL[Protocol Errors]
    ERRORS --> CONFIG[Configuration Errors]

    TRANSPORT --> T1[Connection lost<br/>→ Retry with backoff]
    TRANSPORT --> T2[TLS handshake failed<br/>→ Certificate issue]
    TRANSPORT --> T3[DNS resolution failed<br/>→ Istiod unreachable]

    PROTOCOL --> P1[Invalid protobuf<br/>→ NACK with error]
    PROTOCOL --> P2[Unknown type URL<br/>→ Ignore resource]
    PROTOCOL --> P3[Missing required fields<br/>→ NACK]

    CONFIG --> C1[Resource validation failed<br/>→ NACK, keep old config]
    CONFIG --> C2[Dependency not satisfied<br/>→ NACK, wait for dependency]
    CONFIG --> C3[Circular dependency<br/>→ NACK, log error]

    style T1 fill:#F39C12,color:#fff
    style P1 fill:#E74C3C,color:#fff
    style C1 fill:#E74C3C,color:#fff
```

### Error Handling Flow

```mermaid
sequenceDiagram
    participant E as Envoy
    participant I as Istiod

    Note over E: Active config: v1

    I->>E: DiscoveryResponse<br/>version: v2, nonce: n1<br/>(Invalid config)

    Note over E: Validation fails:<br/>- Parse error<br/>- Missing cluster reference<br/>- Invalid regex

    E->>I: DiscoveryRequest (NACK)<br/>version: v1, nonce: n1<br/>error_detail: {<br/>  code: INVALID_ARGUMENT<br/>  message: "cluster 'foo' not found"<br/>}

    Note over I: Log error<br/>Alert operator<br/>Client still on v1

    opt Automatic fix attempt
        Note over I: Generate corrected v2

        I->>E: DiscoveryResponse<br/>version: v2-fixed, nonce: n2<br/>(Corrected config)

        Note over E: Validation success

        E->>I: DiscoveryRequest (ACK)<br/>version: v2-fixed, nonce: n2
    end

    opt Manual intervention
        Note over I: Operator fixes config<br/>in Istio CRDs

        Note over I: New config generated

        I->>E: DiscoveryResponse<br/>version: v3, nonce: n3

        E->>I: DiscoveryRequest (ACK)<br/>version: v3, nonce: n3
    end
```

### Retry and Backoff Strategy

```mermaid
graph TB
    START[Connection Lost]

    START --> RETRY1[Retry 1<br/>Wait: 1s]
    RETRY1 --> SUCCESS1{Connected?}
    SUCCESS1 -->|Yes| ACTIVE[Active Stream]
    SUCCESS1 -->|No| RETRY2[Retry 2<br/>Wait: 2s]

    RETRY2 --> SUCCESS2{Connected?}
    SUCCESS2 -->|Yes| ACTIVE
    SUCCESS2 -->|No| RETRY3[Retry 3<br/>Wait: 4s]

    RETRY3 --> SUCCESS3{Connected?}
    SUCCESS3 -->|Yes| ACTIVE
    SUCCESS3 -->|No| RETRY4[Retry 4<br/>Wait: 8s]

    RETRY4 --> SUCCESS4{Connected?}
    SUCCESS4 -->|Yes| ACTIVE
    SUCCESS4 -->|No| RETRY5[Retry N<br/>Wait: max 30s]

    RETRY5 --> SUCCESS5{Connected?}
    SUCCESS5 -->|Yes| ACTIVE
    SUCCESS5 -->|No| RETRY5

    ACTIVE --> MONITOR[Monitor connection]
    MONITOR --> ERROR{Connection lost?}
    ERROR -->|Yes| START
    ERROR -->|No| MONITOR

    style START fill:#E74C3C,color:#fff
    style ACTIVE fill:#27AE60,color:#fff
```

## Summary

This document covered the xDS protocol in depth:

1. **Protocol Fundamentals**: gRPC streaming, bidirectional communication
2. **Request/Response**: DiscoveryRequest and DiscoveryResponse structures
3. **Resource Types**: LDS, RDS, CDS, EDS, SDS and their dependencies
4. **ADS**: Single stream for all resource types with ordering guarantees
5. **Incremental xDS**: Delta updates for efficiency
6. **Version Control**: ACK/NACK mechanism for reliable configuration updates
7. **Error Handling**: Comprehensive error categories and recovery strategies

### Key Takeaways

- xDS uses gRPC bidirectional streaming over HTTP/2
- ADS provides ordered, consistent configuration updates
- Incremental xDS reduces bandwidth for large configurations
- ACK/NACK mechanism ensures reliable config application
- Errors are categorized and handled gracefully with retries

## Next Steps

Continue to **Part 4: Istiod Configuration Processing** to understand how Istiod generates xDS configurations from Istio CRDs.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Documentation**:
- [xDS Protocol Specification](https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol)
- [Envoy xDS REST and gRPC protocol](https://github.com/envoyproxy/data-plane-api)
