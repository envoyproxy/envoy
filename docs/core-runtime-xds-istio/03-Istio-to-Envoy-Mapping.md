# Part 3: Istio-to-Envoy Mapping

## Series Navigation

| Part | Topic |
|------|-------|
| Part 1 | [Core Runtime & Bootstrapping](./01-Core-Runtime-and-Bootstrapping.md) |
| Part 2 | [xDS & Dynamic Configuration](./02-xDS-and-Dynamic-Configuration.md) |
| **Part 3** | **Istio-to-Envoy Mapping** (this document) |
| Part 4 | [EnvoyFilter, Sidecar vs Gateway](./04-EnvoyFilter-Sidecar-vs-Gateway.md) |

---

## Overview

Istiod translates Kubernetes and Istio custom resources into Envoy xDS configuration. This document maps each major Istio resource to its Envoy equivalent, showing exactly how Istio CRDs become listeners, routes, clusters, and endpoints in the sidecar/gateway proxy.

---

## 1. The Translation Pipeline

```mermaid
flowchart TD
    subgraph Kubernetes["Kubernetes API Server"]
        Svc["Service"]
        EP["Endpoints / EndpointSlice"]
        VS["VirtualService"]
        DR["DestinationRule"]
        GW["Gateway"]
        SE["ServiceEntry"]
        EF["EnvoyFilter"]
        Sidecar["Sidecar"]
    end

    subgraph Istiod["Istiod (Pilot)"]
        Watch["K8s Watch / Informers"]
        Model["Internal Service Model"]
        Push["Config Push Engine"]
        XDSGen["xDS Generators\n(LDS, RDS, CDS, EDS, SDS)"]
    end

    subgraph Envoy["Envoy Proxy"]
        LDS_E["Listeners (LDS)"]
        RDS_E["Routes (RDS)"]
        CDS_E["Clusters (CDS)"]
        EDS_E["Endpoints (EDS)"]
        SDS_E["Secrets (SDS)"]
    end

    Kubernetes -->|watch events| Watch
    Watch --> Model
    Model --> Push
    Push --> XDSGen
    XDSGen -->|ADS gRPC stream| Envoy
```

---

## 2. VirtualService → RDS (Route Configuration)

A VirtualService defines traffic routing rules. Istiod translates it into Envoy `RouteConfiguration` delivered via RDS.

### Mapping Table

| VirtualService Field | Envoy RDS Equivalent |
|---------------------|---------------------|
| `hosts` | `VirtualHost.domains` |
| `http[].match` | `Route.match` (prefix, exact, regex, headers, query params) |
| `http[].route` | `Route.route` (weighted clusters) |
| `http[].route[].destination.host` | `Route.route.cluster` (or `weighted_clusters`) |
| `http[].route[].destination.subset` | Appended to cluster name: `outbound|port|subset|host` |
| `http[].route[].destination.port` | Part of cluster name |
| `http[].timeout` | `Route.route.timeout` |
| `http[].retries` | `Route.route.retry_policy` |
| `http[].fault` | `Route.typed_per_filter_config` (fault filter) |
| `http[].mirror` | `Route.route.request_mirror_policies` |
| `http[].rewrite` | `Route.route.prefix_rewrite` or `regex_rewrite` |
| `http[].headers` | `Route.request_headers_to_add/remove`, `response_headers_to_add/remove` |
| `http[].corsPolicy` | `Route.typed_per_filter_config` (CORS filter) |
| `tcp[].match` | Listener filter chain match |
| `tcp[].route` | TCP proxy cluster |
| `tls[].match` | SNI-based filter chain match |

### Translation Flow

```mermaid
flowchart TD
    VS["VirtualService YAML"] --> Parse["Istiod parses VirtualService"]
    Parse --> Merge["Merge with Service discovery\n(resolve hosts to FQDN)"]
    Merge --> RC["Build RouteConfiguration"]

    subgraph RouteConfig["RouteConfiguration"]
        VH1["VirtualHost\ndomains: [reviews.default.svc.cluster.local]"]
        VH2["VirtualHost\ndomains: [ratings.default.svc.cluster.local]"]
    end

    RC --> RouteConfig

    subgraph VH1Routes["VirtualHost Routes"]
        R1["Route: match /reviews/v2\n→ cluster: outbound|9080|v2|reviews.default.svc.cluster.local"]
        R2["Route: match / (default)\n→ weighted_clusters:\n  80% → outbound|9080|v1|reviews\n  20% → outbound|9080|v2|reviews"]
    end

    VH1 --> VH1Routes
```

### VirtualService Example

```mermaid
flowchart LR
    subgraph VS_Input["VirtualService"]
        direction TB
        VS_Host["hosts: [reviews]"]
        VS_HTTP["http:\n- match: {uri: {prefix: /v2}}\n  route:\n  - destination:\n      host: reviews\n      subset: v2\n- route:\n  - destination:\n      host: reviews\n      subset: v1\n    weight: 80\n  - destination:\n      host: reviews\n      subset: v2\n    weight: 20"]
    end

    subgraph RDS_Output["Envoy RDS"]
        direction TB
        RC["RouteConfiguration: '9080'"]
        VH["VirtualHost:\n  domains: ['reviews.default.svc.cluster.local']"]
        R1["Route 1:\n  match: {prefix: '/v2'}\n  route: {cluster: 'outbound|9080|v2|reviews...'}"]
        R2["Route 2:\n  match: {prefix: '/'}\n  route: {weighted_clusters:\n    {v1: 80%, v2: 20%}}"]
    end

    VS_Input -->|"Istiod\ntranslation"| RDS_Output
```

### Cluster Naming Convention

Istio uses a structured cluster name format:

```
{direction}|{port}|{subset}|{FQDN}
```

```mermaid
flowchart LR
    Name["outbound|9080|v2|reviews.default.svc.cluster.local"]
    Name --> Dir["outbound\n(direction)"]
    Name --> Port["9080\n(service port)"]
    Name --> Sub["v2\n(subset from DR)"]
    Name --> FQDN["reviews.default.svc.cluster.local\n(service FQDN)"]
```

---

## 3. Gateway → LDS (Listener Configuration)

An Istio Gateway resource defines what ports and protocols the gateway proxy should listen on. Istiod translates it into Envoy `Listener` resources delivered via LDS.

### Mapping Table

| Gateway Field | Envoy LDS Equivalent |
|--------------|---------------------|
| `servers[].port.number` | `Listener.address.socket_address.port_value` |
| `servers[].port.protocol` | Determines filter chain (HTTP or TCP) |
| `servers[].hosts` | `FilterChainMatch.server_names` (SNI) |
| `servers[].tls.mode` | `TransportSocket` (TLS config) |
| `servers[].tls.credentialName` | SDS secret reference |
| `servers[].tls.minProtocolVersion` | `TlsParameters.tls_minimum_protocol_version` |
| `servers[].tls.cipherSuites` | `TlsParameters.cipher_suites` |
| Attached VirtualServices | `HttpConnectionManager.rds` (route config ref) |

### Gateway Translation Flow

```mermaid
flowchart TD
    GW["Gateway Resource"] --> Parse["Istiod processes Gateway"]
    Parse --> Listeners["Generate Listener per port"]

    subgraph Listener443["Listener: 0.0.0.0:443"]
        FC1["FilterChain 1\nSNI: app1.example.com\nTLS: credentialName=app1-cert"]
        FC2["FilterChain 2\nSNI: app2.example.com\nTLS: credentialName=app2-cert"]
    end

    subgraph FC1Detail["FilterChain 1 Detail"]
        HCM1["HttpConnectionManager\nrds: route_config_name=https.443.app1"]
        TLS1["DownstreamTlsContext\nSDS: app1-cert"]
    end

    Listeners --> Listener443
    FC1 --> FC1Detail
```

### Gateway Example: Multi-Host TLS

```mermaid
flowchart LR
    subgraph GW_Input["Gateway"]
        direction TB
        GW_Server1["server:\n  port: {number: 443, protocol: HTTPS}\n  hosts: [app1.example.com]\n  tls:\n    mode: SIMPLE\n    credentialName: app1-cert"]
        GW_Server2["server:\n  port: {number: 443, protocol: HTTPS}\n  hosts: [app2.example.com]\n  tls:\n    mode: SIMPLE\n    credentialName: app2-cert"]
    end

    subgraph LDS_Output["Envoy LDS"]
        direction TB
        L["Listener 0.0.0.0:443"]
        FC1["FilterChain:\n  match: {server_names: [app1.example.com]}\n  transport_socket: SDS(app1-cert)\n  filters: [HCM → RDS]"]
        FC2["FilterChain:\n  match: {server_names: [app2.example.com]}\n  transport_socket: SDS(app2-cert)\n  filters: [HCM → RDS]"]
    end

    GW_Input -->|"Istiod"| LDS_Output
```

### Sidecar Listener Generation (No Gateway Resource)

For sidecars, Istiod generates listeners automatically based on service discovery:

```mermaid
flowchart TD
    subgraph ServiceDiscovery["Service Discovery"]
        SvcA["reviews:9080"]
        SvcB["ratings:9080"]
        SvcC["productpage:9080"]
    end

    subgraph IstiodGen["Istiod Generates"]
        VL["Virtual Listener 0.0.0.0:15001\n(iptables-intercepted traffic)"]
        OL1["Outbound Listener 10.0.1.5:9080\n(reviews)"]
        OL2["Outbound Listener 10.0.1.6:9080\n(ratings)"]
        IL["Inbound Listener 0.0.0.0:15006\n(traffic to this pod)"]
    end

    ServiceDiscovery -->|"auto-generated"| IstiodGen
```

---

## 4. DestinationRule → CDS (Cluster Configuration)

A DestinationRule defines traffic policies and subsets for a destination. Istiod translates it into Envoy `Cluster` resources delivered via CDS.

### Mapping Table

| DestinationRule Field | Envoy CDS Equivalent |
|----------------------|---------------------|
| `host` | Base cluster: `outbound|port||host.namespace.svc.cluster.local` |
| `subsets[].name` | Subset cluster: `outbound|port|subset_name|host...` |
| `subsets[].labels` | EDS endpoint metadata filter |
| `trafficPolicy.connectionPool.tcp` | `Cluster.circuit_breakers`, `max_connections` |
| `trafficPolicy.connectionPool.http` | `Cluster.circuit_breakers`, `max_requests`, `max_pending_requests` |
| `trafficPolicy.loadBalancer` | `Cluster.lb_policy` (ROUND_ROBIN, LEAST_REQUEST, RANDOM) |
| `trafficPolicy.loadBalancer.consistentHash` | `Cluster.lb_policy = RING_HASH` + `ring_hash_lb_config` |
| `trafficPolicy.outlierDetection` | `Cluster.outlier_detection` |
| `trafficPolicy.tls.mode` | `Cluster.transport_socket` (upstream TLS) |
| `trafficPolicy.portLevelSettings` | Per-port cluster overrides |

### Translation Flow

```mermaid
flowchart TD
    DR["DestinationRule"] --> Parse["Istiod processes DR"]
    Parse --> BaseClusters["Base clusters\n(one per port)"]
    Parse --> SubsetClusters["Subset clusters\n(one per subset per port)"]

    subgraph Generated["Generated CDS Clusters"]
        C1["outbound|9080||reviews.default.svc.cluster.local\n(base - all endpoints)"]
        C2["outbound|9080|v1|reviews.default.svc.cluster.local\n(subset v1 - labels: version=v1)"]
        C3["outbound|9080|v2|reviews.default.svc.cluster.local\n(subset v2 - labels: version=v2)"]
    end

    BaseClusters --> C1
    SubsetClusters --> C2 & C3
```

### DestinationRule Example

```mermaid
flowchart LR
    subgraph DR_Input["DestinationRule"]
        direction TB
        DR_Host["host: reviews.default.svc.cluster.local"]
        DR_Policy["trafficPolicy:\n  connectionPool:\n    tcp: {maxConnections: 100}\n    http: {h2UpgradePolicy: UPGRADE}\n  outlierDetection:\n    consecutive5xxErrors: 5\n    interval: 10s\n  loadBalancer:\n    simple: ROUND_ROBIN"]
        DR_Subsets["subsets:\n- name: v1\n  labels: {version: v1}\n- name: v2\n  labels: {version: v2}\n  trafficPolicy:\n    loadBalancer:\n      simple: LEAST_REQUEST"]
    end

    subgraph CDS_Output["Envoy CDS"]
        direction TB
        C_Base["Cluster: outbound|9080||reviews...\n  type: EDS\n  lb_policy: ROUND_ROBIN\n  circuit_breakers: max_connections=100\n  outlier_detection: consecutive_5xx=5"]
        C_V1["Cluster: outbound|9080|v1|reviews...\n  type: EDS\n  lb_policy: ROUND_ROBIN\n  (inherits base policy)"]
        C_V2["Cluster: outbound|9080|v2|reviews...\n  type: EDS\n  lb_policy: LEAST_REQUEST\n  (subset overrides LB)"]
    end

    DR_Input -->|"Istiod"| CDS_Output
```

### Subset-to-Endpoint Filtering

Subsets filter EDS endpoints by label matching:

```mermaid
flowchart TD
    subgraph AllEndpoints["EDS: All Endpoints for reviews:9080"]
        EP1["10.0.1.1:9080\nmetadata: {version: v1}"]
        EP2["10.0.1.2:9080\nmetadata: {version: v1}"]
        EP3["10.0.1.3:9080\nmetadata: {version: v2}"]
        EP4["10.0.1.4:9080\nmetadata: {version: v2}"]
    end

    subgraph V1["Subset v1 endpoints"]
        EP1V["10.0.1.1:9080"]
        EP2V["10.0.1.2:9080"]
    end

    subgraph V2["Subset v2 endpoints"]
        EP3V["10.0.1.3:9080"]
        EP4V["10.0.1.4:9080"]
    end

    EP1 & EP2 -->|"labels match {version: v1}"| V1
    EP3 & EP4 -->|"labels match {version: v2}"| V2
```

---

## 5. ServiceEntry → CDS + EDS

ServiceEntry registers external services into the mesh:

```mermaid
flowchart LR
    subgraph SE_Input["ServiceEntry"]
        direction TB
        SE_Host["hosts: [external-api.example.com]"]
        SE_Ports["ports:\n- number: 443\n  protocol: TLS"]
        SE_Resolution["resolution: DNS"]
        SE_Endpoints["endpoints:\n- address: api1.external.com\n- address: api2.external.com"]
    end

    subgraph Envoy_Output["Envoy Config"]
        direction TB
        C["CDS Cluster:\n  outbound|443||external-api.example.com\n  type: STRICT_DNS\n  dns_lookup_family: V4_ONLY"]
        E["EDS/DNS Endpoints:\n  api1.external.com:443\n  api2.external.com:443"]
    end

    SE_Input -->|"Istiod"| Envoy_Output
```

### ServiceEntry Resolution Types

| Resolution | Envoy Cluster Type | Endpoint Source |
|-----------|-------------------|-----------------|
| `NONE` | `ORIGINAL_DST` | Original destination from connection |
| `STATIC` | `STATIC` or `EDS` | Fixed addresses from SE endpoints |
| `DNS` | `STRICT_DNS` | DNS resolution of SE endpoint addresses |
| `DNS_ROUND_ROBIN` | `LOGICAL_DNS` | DNS with round-robin |

---

## 6. Kubernetes Service/Endpoints → CDS + EDS

Without any Istio CRDs, basic Kubernetes services still get translated:

```mermaid
sequenceDiagram
    participant K8s as Kubernetes API
    participant Istiod as Istiod
    participant Envoy as Envoy Proxy

    K8s->>Istiod: Service "reviews" created (ClusterIP: 10.0.1.5)
    K8s->>Istiod: Endpoints for "reviews" (pods: 10.0.2.1, 10.0.2.2)

    Istiod->>Istiod: Build cluster: outbound|9080||reviews.default.svc.cluster.local
    Istiod->>Istiod: Build endpoints: [10.0.2.1:9080, 10.0.2.2:9080]

    Istiod->>Envoy: CDS push (cluster config)
    Istiod->>Envoy: EDS push (endpoint list)

    Note over Envoy: VirtualService adds routing rules on top
    Note over Envoy: DestinationRule adds traffic policy + subsets
```

---

## 7. Complete Mapping: Bookinfo Example

```mermaid
flowchart TD
    subgraph IstioResources["Istio Resources"]
        VS_R["VirtualService: reviews\n(80% v1, 20% v3)"]
        DR_R["DestinationRule: reviews\n(subsets: v1, v2, v3)"]
        GW_B["Gateway: bookinfo-gateway\n(port 80)"]
        VS_B["VirtualService: bookinfo\n(route /productpage)"]
    end

    subgraph EnvoyGW["Gateway Envoy"]
        L_GW["Listener 0.0.0.0:80"]
        RC_GW["RouteConfig: http.80\nVH: *.bookinfo.com\n/productpage → outbound|9080||productpage..."]
        C_GW["Cluster: outbound|9080||productpage..."]
    end

    subgraph EnvoySidecar["Reviews Sidecar Envoy"]
        L_In["Inbound Listener :15006\n→ local app :9080"]
        L_Out["Outbound Listeners\n→ ratings, mongodb, etc."]
    end

    subgraph EnvoyPP["Productpage Sidecar Envoy"]
        RC_PP["RouteConfig for reviews\n80% → outbound|9080|v1|reviews\n20% → outbound|9080|v3|reviews"]
        C_V1["Cluster: outbound|9080|v1|reviews..."]
        C_V3["Cluster: outbound|9080|v3|reviews..."]
        E_V1["EDS v1: [pod1, pod2]\n(labels: version=v1)"]
        E_V3["EDS v3: [pod5]\n(labels: version=v3)"]
    end

    GW_B --> L_GW
    VS_B --> RC_GW
    VS_R --> RC_PP
    DR_R --> C_V1 & C_V3
    C_V1 --> E_V1
    C_V3 --> E_V3
```

---

## 8. mTLS Translation

### PeerAuthentication + DestinationRule TLS

```mermaid
flowchart TD
    subgraph IstioMTLS["Istio mTLS Config"]
        PA["PeerAuthentication:\n  mode: STRICT"]
        DR_TLS["DestinationRule:\n  trafficPolicy:\n    tls:\n      mode: ISTIO_MUTUAL"]
    end

    subgraph EnvoyTLS["Envoy TLS Config"]
        direction TB
        Upstream["Upstream (CDS):\n  transport_socket:\n    typed_config:\n      common_tls_context:\n        tls_certificate_sds_secret_configs:\n          - name: default\n            sds_config: {api_type: GRPC}\n        validation_context_sds_secret_config:\n          name: ROOTCA\n          sds_config: {api_type: GRPC}"]
        Downstream["Downstream (LDS):\n  transport_socket:\n    typed_config:\n      require_client_certificate: true\n      common_tls_context:\n        tls_certificate_sds_secret_configs:\n          - name: default\n        validation_context_sds_secret_config:\n          name: ROOTCA"]
    end

    IstioMTLS -->|"Istiod translates"| EnvoyTLS
```

### SDS for Certificate Delivery

```mermaid
sequenceDiagram
    participant Agent as Istio Agent (pilot-agent)
    participant SDS as SDS Server (in-process)
    participant Envoy as Envoy Proxy
    participant CA as Istio CA (Istiod)

    Envoy->>SDS: SDS request for "default" (workload cert)
    SDS->>CA: CSR (Certificate Signing Request)
    CA-->>SDS: Signed certificate + CA chain
    SDS-->>Envoy: TlsCertificate (cert, key, ca)

    Note over Envoy: Certificate rotated before expiry
    SDS->>Envoy: Updated TlsCertificate (new cert)
```

---

## 9. Resource Dependency Chain

```mermaid
flowchart TD
    subgraph Istio["Istio CRDs"]
        GW["Gateway"] --> VS["VirtualService"]
        VS --> DR["DestinationRule"]
        DR --> Svc["K8s Service"]
        Svc --> EP["K8s Endpoints"]
        PA["PeerAuthentication"] -.-> TLS["TLS Policy"]
    end

    subgraph Envoy["Envoy xDS"]
        LDS["LDS (Listeners)"] --> RDS["RDS (Routes)"]
        RDS --> CDS["CDS (Clusters)"]
        CDS --> EDS["EDS (Endpoints)"]
        SDS["SDS (Secrets)"] -.-> LDS & CDS
    end

    GW -->|generates| LDS
    VS -->|generates| RDS
    DR -->|generates| CDS
    Svc -->|generates| CDS
    EP -->|generates| EDS
    PA & TLS -->|generates| SDS
```
