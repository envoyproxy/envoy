# Part 4: EnvoyFilter CRD, Sidecar vs Gateway Mode

## Series Navigation

| Part | Topic |
|------|-------|
| Part 1 | [Core Runtime & Bootstrapping](./01-Core-Runtime-and-Bootstrapping.md) |
| Part 2 | [xDS & Dynamic Configuration](./02-xDS-and-Dynamic-Configuration.md) |
| Part 3 | [Istio-to-Envoy Mapping](./03-Istio-to-Envoy-Mapping.md) |
| **Part 4** | **EnvoyFilter CRD, Sidecar vs Gateway Mode** (this document) |

---

## Section A: EnvoyFilter CRD

### Overview

The `EnvoyFilter` CRD provides escape-hatch access to the raw Envoy configuration. It allows operators to patch generated xDS resources at specific points in the config generation pipeline before they are pushed to proxies.

---

### 1. EnvoyFilter Processing Pipeline

```mermaid
flowchart TD
    subgraph Input["Istio CRDs"]
        VS["VirtualService"]
        DR["DestinationRule"]
        GW["Gateway"]
        SE["ServiceEntry"]
    end

    subgraph Istiod["Istiod Config Generation"]
        Gen["Generate base xDS config\n(Listeners, Routes, Clusters, Endpoints)"]
        EF["Apply EnvoyFilters\n(in priority order)"]
        Validate["Validate final config"]
        Push["Push to proxies via ADS"]
    end

    Input --> Gen
    Gen --> EF
    EF --> Validate
    Validate --> Push
    
    EFR["EnvoyFilter CRDs"] -->|"patches"| EF
```

### 2. EnvoyFilter Structure

```mermaid
mindmap
  root((EnvoyFilter))
    workloadSelector
      labels: match specific pods
      Empty: applies to all proxies
    priority
      Order of application (lower = first)
      Default: 0
    configPatches
      applyTo: target resource type
      match: where to apply
      patch: what to change
```

### 3. `applyTo` — What Can Be Patched

| applyTo Value | Target | xDS Type |
|--------------|--------|----------|
| `LISTENER` | Entire listener | LDS |
| `FILTER_CHAIN` | Filter chain within a listener | LDS |
| `NETWORK_FILTER` | Network-level filter | LDS |
| `HTTP_FILTER` | HTTP-level filter in HCM | LDS |
| `ROUTE_CONFIGURATION` | Route config | RDS |
| `VIRTUAL_HOST` | Virtual host within route config | RDS |
| `HTTP_ROUTE` | Route within virtual host | RDS |
| `CLUSTER` | Cluster | CDS |
| `EXTENSION_CONFIG` | ECDS extension | ECDS |

### 4. Patch Operations

```mermaid
flowchart TD
    subgraph Operations["patch.operation"]
        ADD["ADD\nInsert new element"]
        REMOVE["REMOVE\nDelete matched element"]
        MERGE["MERGE\nDeep-merge with existing"]
        REPLACE["REPLACE\nReplace entire element"]
        INSERT_BEFORE["INSERT_BEFORE\nInsert before matched filter"]
        INSERT_AFTER["INSERT_AFTER\nInsert after matched filter"]
        INSERT_FIRST["INSERT_FIRST\nInsert at beginning of filter chain"]
    end
```

### 5. Match Criteria

```mermaid
flowchart TD
    subgraph MatchContext["match.context"]
        ANY["ANY\n(all traffic)"]
        SIDECAR_INBOUND["SIDECAR_INBOUND\n(traffic TO the pod)"]
        SIDECAR_OUTBOUND["SIDECAR_OUTBOUND\n(traffic FROM the pod)"]
        GATEWAY["GATEWAY\n(gateway proxy only)"]
    end

    subgraph MatchListener["match.listener"]
        LName["name: specific listener name"]
        LPort["portNumber: match by port"]
        LFiltChain["filterChain:\n  name, sni, transport_protocol,\n  application_protocols"]
        LFilter["filter:\n  name: envoy.filters.network.http_connection_manager"]
        LSubFilter["subFilter:\n  name: envoy.filters.http.router"]
    end

    subgraph MatchRoute["match.routeConfiguration"]
        RCName["name: route config name"]
        RVHost["vhost:\n  name: virtual host name\n  route:\n    name: route name\n    action: ROUTE|REDIRECT|DIRECT_RESPONSE"]
    end

    subgraph MatchCluster["match.cluster"]
        CName["name: cluster name"]
        CSubset["subset: subset name"]
        CService["service: service FQDN"]
        CPort["portNumber: service port"]
    end
```

### 6. EnvoyFilter Examples

#### Example 1: Add Custom HTTP Filter

```mermaid
flowchart LR
    subgraph Before["Before EnvoyFilter"]
        direction TB
        F1["envoy.filters.http.fault"]
        F2["envoy.filters.http.cors"]
        F3["envoy.filters.http.router"]
    end

    subgraph EF["EnvoyFilter Patch"]
        direction TB
        EFP["applyTo: HTTP_FILTER\nmatch:\n  context: SIDECAR_INBOUND\n  listener:\n    filterChain:\n      filter:\n        name: envoy.filters.network.http_connection_manager\n        subFilter:\n          name: envoy.filters.http.router\npatch:\n  operation: INSERT_BEFORE\n  value:\n    name: envoy.filters.http.lua\n    typed_config:\n      inline_code: ..."]
    end

    subgraph After["After EnvoyFilter"]
        direction TB
        AF1["envoy.filters.http.fault"]
        AF2["envoy.filters.http.cors"]
        AF3["envoy.filters.http.lua ← INSERTED"]
        AF4["envoy.filters.http.router"]
    end

    Before -->|"apply"| EF -->|"result"| After
```

#### Example 2: Modify Cluster Settings

```mermaid
flowchart LR
    subgraph Before["Before"]
        direction TB
        CB["Cluster: outbound|8080||myservice...\n  connect_timeout: 10s\n  lb_policy: ROUND_ROBIN"]
    end

    subgraph EF["EnvoyFilter"]
        direction TB
        EFP2["applyTo: CLUSTER\nmatch:\n  cluster:\n    service: myservice.default.svc.cluster.local\npatch:\n  operation: MERGE\n  value:\n    connect_timeout: 30s\n    outlier_detection:\n      consecutive_5xx: 3"]
    end

    subgraph After["After"]
        direction TB
        CA["Cluster: outbound|8080||myservice...\n  connect_timeout: 30s ← MERGED\n  lb_policy: ROUND_ROBIN\n  outlier_detection: ← MERGED\n    consecutive_5xx: 3"]
    end

    Before -->|"MERGE"| EF -->|"result"| After
```

#### Example 3: Add Listener-Level Filter

```mermaid
flowchart LR
    subgraph Before["Before"]
        direction TB
        L["Listener 0.0.0.0:15006\n  filter_chains:\n    - filters:\n        - http_connection_manager"]
    end

    subgraph EF["EnvoyFilter"]
        direction TB
        EFP3["applyTo: NETWORK_FILTER\nmatch:\n  context: SIDECAR_INBOUND\n  listener:\n    portNumber: 15006\npatch:\n  operation: INSERT_FIRST\n  value:\n    name: envoy.filters.network.rbac\n    typed_config: ..."]
    end

    subgraph After["After"]
        direction TB
        LA["Listener 0.0.0.0:15006\n  filter_chains:\n    - filters:\n        - envoy.filters.network.rbac ← INSERTED\n        - http_connection_manager"]
    end

    Before -->|"INSERT_FIRST"| EF -->|"result"| After
```

### 7. EnvoyFilter Priority and Ordering

```mermaid
flowchart TD
    subgraph Ordering["Application Order"]
        direction TB
        Root["Root namespace EnvoyFilters\n(istio-system, priority 0)"]
        RootP1["Root namespace EnvoyFilters\n(istio-system, priority 1)"]
        NS["Workload namespace EnvoyFilters\n(default, priority 0)"]
        NSP1["Workload namespace EnvoyFilters\n(default, priority 1)"]
    end

    Root --> RootP1 --> NS --> NSP1

    Note1["Lower priority number = applied first\nRoot namespace EnvoyFilters always before workload namespace"]
```

### 8. Common Pitfalls

```mermaid
flowchart TD
    subgraph Pitfalls["EnvoyFilter Pitfalls"]
        P1["Version coupling\nEnvoyFilter references internal Envoy\nconfig types - breaks on upgrade"]
        P2["Over-broad matching\nEmpty workloadSelector applies to\nALL proxies in namespace"]
        P3["Order sensitivity\nMultiple EnvoyFilters can conflict\nor override each other"]
        P4["Validation gaps\nEnvoyFilter patches bypass Istio\nvalidation - invalid Envoy config possible"]
        P5["Debugging difficulty\nHard to trace which EnvoyFilter\ncaused a specific config change"]
    end
```

---

## Section B: Sidecar vs Gateway Mode

### 1. Deployment Topologies

```mermaid
flowchart TD
    subgraph SidecarMode["Sidecar Mode"]
        Pod["Application Pod"]
        SC["Envoy Sidecar\n(injected container)"]
        IPT["iptables rules\n(traffic interception)"]
        Pod <--> IPT <--> SC
    end

    subgraph GatewayMode["Gateway Mode"]
        GWPod["Gateway Pod\n(standalone Envoy)"]
        Ingress["External Traffic\n→ Gateway → Services"]
    end

    subgraph Ambient["Ambient Mode (ztunnel)"]
        ZT["ztunnel\n(node-level proxy)"]
        WP["waypoint proxy\n(optional L7)"]
    end
```

### 2. Listener Topology: Sidecar

```mermaid
flowchart TD
    subgraph ExternalTraffic["Incoming Traffic to Pod"]
        In["TCP connection\nto pod IP:port"]
    end

    subgraph Iptables["iptables REDIRECT"]
        InRule["PREROUTING → REDIRECT :15006\n(inbound)"]
        OutRule["OUTPUT → REDIRECT :15001\n(outbound)"]
    end

    subgraph SidecarListeners["Sidecar Envoy Listeners"]
        VirtIn["Virtual Inbound Listener\n0.0.0.0:15006"]
        VirtOut["Virtual Outbound Listener\n0.0.0.0:15001"]

        subgraph InboundFC["Inbound Filter Chains"]
            FC_App["FilterChain: app port 9080\n→ HCM → local app 127.0.0.1:9080"]
            FC_App2["FilterChain: app port 8080\n→ HCM → local app 127.0.0.1:8080"]
        end

        subgraph OutboundFC["Outbound Filter Chains / Listeners"]
            FC_Svc1["Listener: 10.0.1.5:9080\n→ HCM → RDS → upstream cluster"]
            FC_Svc2["Listener: 10.0.2.3:8080\n→ HCM → RDS → upstream cluster"]
            FC_PT["PassthroughCluster\n(unmatched traffic)"]
        end
    end

    subgraph App["Application"]
        AppIn["App listening on\n127.0.0.1:9080"]
        AppOut["App connects to\nexternal service"]
    end

    ExternalTraffic --> InRule --> VirtIn --> InboundFC --> AppIn
    AppOut --> OutRule --> VirtOut --> OutboundFC
```

### 3. Listener Topology: Gateway

```mermaid
flowchart TD
    subgraph ExternalTraffic["External Traffic"]
        Client["Client\n(internet/other cluster)"]
    end

    subgraph LBService["K8s LoadBalancer Service"]
        LB["Cloud LB\n→ NodePort → Pod"]
    end

    subgraph GatewayListeners["Gateway Envoy Listeners"]
        L80["Listener 0.0.0.0:80\n(HTTP)"]
        L443["Listener 0.0.0.0:443\n(HTTPS/TLS)"]

        subgraph L443FC["Port 443 Filter Chains"]
            FC_SNI1["FilterChain: SNI=app1.example.com\nTLS: SDS(app1-cert)\n→ HCM → RDS"]
            FC_SNI2["FilterChain: SNI=app2.example.com\nTLS: SDS(app2-cert)\n→ HCM → RDS"]
        end
    end

    subgraph UpstreamClusters["Upstream Clusters"]
        C1["outbound|9080||app1.ns.svc.cluster.local"]
        C2["outbound|8080||app2.ns.svc.cluster.local"]
    end

    Client --> LB --> GatewayListeners
    L443 --> L443FC
    FC_SNI1 --> C1
    FC_SNI2 --> C2
```

### 4. Side-by-Side Comparison

| Aspect | Sidecar | Gateway |
|--------|---------|---------|
| **Deployment** | Injected into every app pod | Standalone pod(s) |
| **Traffic interception** | iptables REDIRECT | Direct port binding |
| **Inbound listener** | `0.0.0.0:15006` (virtual) | N/A (all traffic is "inbound") |
| **Outbound listener** | `0.0.0.0:15001` (virtual) + per-service | N/A |
| **Listener source** | Auto-generated from service discovery | Gateway CRD |
| **Filter chains** | Per-service port | Per-server (SNI-based) |
| **Route configs** | Per-service port | Per-Gateway server |
| **Clusters** | All services visible to sidecar | Only referenced services |
| **mTLS** | Both sides (upstream + downstream) | Usually downstream only |
| **Scale** | One proxy per pod | Shared proxy for many services |

### 5. Bootstrap Differences

```mermaid
flowchart LR
    subgraph SidecarBootstrap["Sidecar Bootstrap"]
        direction TB
        SB1["node:\n  id: sidecar~10.0.1.5~reviews.default~default.svc.cluster.local\n  cluster: reviews.default"]
        SB2["dynamic_resources:\n  ads_config:\n    api_type: GRPC\n    grpc_services:\n      envoy_grpc:\n        cluster_name: xds-grpc"]
        SB3["static_resources:\n  clusters:\n  - name: xds-grpc\n    type: STATIC\n    endpoints: [{127.0.0.1:15010}]"]
        SB4["admin:\n  address: 127.0.0.1:15000"]
    end

    subgraph GatewayBootstrap["Gateway Bootstrap"]
        direction TB
        GB1["node:\n  id: router~10.0.3.1~istio-ingressgateway.istio-system\n  cluster: istio-ingressgateway"]
        GB2["dynamic_resources:\n  ads_config:\n    api_type: GRPC\n    grpc_services:\n      envoy_grpc:\n        cluster_name: xds-grpc"]
        GB3["static_resources:\n  clusters:\n  - name: xds-grpc\n    type: STATIC\n    endpoints: [{istiod.istio-system:15010}]"]
        GB4["admin:\n  address: 0.0.0.0:15000"]
    end
```

### Key Bootstrap Differences

| Field | Sidecar | Gateway |
|-------|---------|---------|
| `node.id` prefix | `sidecar~` | `router~` |
| `node.id` format | `sidecar~{IP}~{pod}.{ns}~{ns}.svc.cluster.local` | `router~{IP}~{pod}.{ns}~{ns}.svc.cluster.local` |
| xDS server address | `127.0.0.1:15010` (local pilot-agent) | `istiod.istio-system:15010` (remote) |
| Admin bind | `127.0.0.1:15000` (localhost only) | `0.0.0.0:15000` (accessible) |
| Proxy type in metadata | `sidecar` | `router` |

### 6. How Istiod Uses Proxy Type

```mermaid
flowchart TD
    Connect["Proxy connects to Istiod\n(ADS stream)"]
    Connect --> NodeID["Parse node.id\nExtract proxy type"]
    NodeID --> Decision{Proxy Type?}

    Decision -->|"sidecar~"| SidecarGen["Sidecar Config Generation"]
    Decision -->|"router~"| GatewayGen["Gateway Config Generation"]

    subgraph SidecarGen
        SG1["Generate inbound listener :15006"]
        SG2["Generate outbound listener :15001"]
        SG3["Generate per-service outbound listeners"]
        SG4["Generate clusters for all visible services"]
        SG5["Apply Sidecar CRD scope (if present)"]
    end

    subgraph GatewayGen
        GG1["Generate listeners from Gateway CRDs\n(matching this gateway)"]
        GG2["Generate routes from VirtualServices\n(attached to matching Gateways)"]
        GG3["Generate clusters only for\nreferenced destinations"]
    end
```

### 7. Sidecar CRD — Scoping Sidecar Config

The `Sidecar` CRD controls what services a sidecar can see:

```mermaid
flowchart TD
    subgraph WithoutSidecarCRD["Without Sidecar CRD"]
        All["Sidecar receives config for\nALL services in the mesh\n(100s or 1000s of clusters)"]
    end

    subgraph WithSidecarCRD["With Sidecar CRD"]
        Scoped["Sidecar receives config only for\nservices in egress.hosts\n(much smaller config)"]
    end

    Without["No Sidecar CRD"] --> All
    With["Sidecar CRD applied"] --> Scoped
```

### Sidecar CRD Structure

```mermaid
mindmap
  root((Sidecar CRD))
    workloadSelector
      labels: match specific pods
    ingress
      port: override inbound port
      defaultEndpoint: local app address
      tls: inbound TLS settings
    egress
      hosts: ["namespace/service"]
      port: specific port
      captureMode: IPTABLES or NONE
    outboundTrafficPolicy
      mode: ALLOW_ANY or REGISTRY_ONLY
```

### Sidecar CRD Example

```mermaid
flowchart LR
    subgraph SidecarCRD["Sidecar CRD"]
        direction TB
        WS["workloadSelector:\n  labels: {app: reviews}"]
        Egress["egress:\n- hosts:\n  - './ratings.default.svc.cluster.local'\n  - 'istio-system/*'"]
        Policy["outboundTrafficPolicy:\n  mode: REGISTRY_ONLY"]
    end

    subgraph Result["Generated Config for 'reviews' Pods"]
        direction TB
        R1["Only clusters/routes for:\n- ratings.default\n- istio-system services"]
        R2["All other traffic:\nblocked (REGISTRY_ONLY)"]
        R3["Config size: dramatically reduced"]
    end

    SidecarCRD -->|"Istiod generates\nscoped config"| Result
```

### 8. Sidecar CRD Impact on Listeners

```mermaid
flowchart TD
    subgraph DefaultScope["Default (no Sidecar CRD)"]
        direction TB
        DL1["Outbound: 0.0.0.0:15001"]
        DL2["Per-IP: 10.0.1.1:9080 (svc-a)"]
        DL3["Per-IP: 10.0.1.2:8080 (svc-b)"]
        DL4["Per-IP: 10.0.1.3:3306 (svc-c)"]
        DL5["... 200+ more listeners"]
    end

    subgraph ScopedScope["With Sidecar CRD (egress: [./svc-a])"]
        direction TB
        SL1["Outbound: 0.0.0.0:15001"]
        SL2["Per-IP: 10.0.1.1:9080 (svc-a)"]
        SL3["Passthrough for everything else"]
    end
```

### 9. Traffic Flow Comparison

#### Sidecar Inbound Traffic

```mermaid
sequenceDiagram
    participant Client as Client Pod
    participant IPT as iptables
    participant Inbound as Sidecar Inbound :15006
    participant App as Application :9080

    Client->>IPT: TCP to pod-IP:9080
    IPT->>Inbound: REDIRECT to :15006
    Inbound->>Inbound: Determine original destination
    Inbound->>Inbound: Apply inbound policies\n(mTLS termination, authz, telemetry)
    Inbound->>App: Forward to 127.0.0.1:9080
    App-->>Inbound: Response
    Inbound-->>IPT: Response
    IPT-->>Client: Response
```

#### Sidecar Outbound Traffic

```mermaid
sequenceDiagram
    participant App as Application
    participant IPT as iptables
    participant Outbound as Sidecar Outbound :15001
    participant LB as Load Balancer
    participant Upstream as Upstream Pod

    App->>IPT: TCP to service-IP:port
    IPT->>Outbound: REDIRECT to :15001
    Outbound->>Outbound: Match listener by original dest
    Outbound->>Outbound: Apply route rules (VirtualService)
    Outbound->>LB: Select upstream host
    LB-->>Outbound: Host selected
    Outbound->>Upstream: mTLS connection to upstream pod
    Upstream-->>Outbound: Response
    Outbound-->>App: Response
```

#### Gateway Traffic

```mermaid
sequenceDiagram
    participant Client as External Client
    participant GW as Gateway Envoy :443
    participant SNI as SNI-based Filter Chain
    participant LB as Load Balancer
    participant Upstream as Upstream Pod

    Client->>GW: TLS ClientHello (SNI=app.example.com)
    GW->>SNI: Match filter chain by SNI
    SNI->>SNI: TLS termination
    SNI->>SNI: Apply route rules (VirtualService)
    SNI->>LB: Select upstream host
    LB-->>SNI: Host selected
    SNI->>Upstream: Forward (with or without mTLS)
    Upstream-->>GW: Response
    GW-->>Client: Response
```

### 10. Istio-Specific Envoy Extensions

These extensions in `contrib/istio/` are loaded into the Envoy binary for Istio-specific functionality:

```mermaid
flowchart TD
    subgraph IstioExtensions["Istio-Specific Envoy Extensions"]
        subgraph NetworkFilters["Network Filters"]
            MX["metadata_exchange\n(exchange workload identity\nbetween proxies)"]
        end

        subgraph HTTPFilters["HTTP Filters"]
            PM["peer_metadata\n(propagate peer info\nvia HTTP headers)"]
            IS["istio_stats\n(Istio-specific metrics:\nrequest count, duration, size\nby source/dest workload)"]
            ALPN["alpn\n(protocol negotiation\nfor Istio mTLS)"]
        end

        subgraph Common["Common Libraries"]
            WMO["WorkloadMetadataObject\n(workload identity:\nnamespace, name, labels)"]
            WDS["WorkloadDiscoveryService\n(resolve peer identity)"]
        end
    end

    MX --> WMO
    PM --> WMO
    IS --> WMO
    WMO --> WDS
```

### 11. Decision Matrix: When to Use What

```mermaid
flowchart TD
    Start["Need to configure Envoy behavior?"] --> Q1{"Standard Istio\nCRD covers it?"}
    Q1 -->|Yes| UseCRD["Use VirtualService/\nDestinationRule/\nGateway/PeerAuthentication"]
    Q1 -->|No| Q2{"Need raw Envoy\nconfig access?"}
    Q2 -->|Yes| UseEF["Use EnvoyFilter"]
    Q2 -->|No| Q3{"Need to scope\nsidecar config?"}
    Q3 -->|Yes| UseSC["Use Sidecar CRD"]
    Q3 -->|No| UseWASM["Consider WASM\nor Lua extension"]

    UseCRD --> Best["Best: type-safe,\nupgrade-resilient"]
    UseEF --> Caution["Caution: version-coupled,\nhard to debug"]
    UseSC --> Good["Good: reduces config size,\nimproves performance"]
    UseWASM --> Advanced["Advanced: custom logic\nwithout config coupling"]
```
