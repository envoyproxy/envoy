# RBAC Filter Internals: Authorization Enforcement

## Overview

The RBAC (Role-Based Access Control) filter enforces authorization policies within Envoy by evaluating rules against connection and request attributes. Unlike external authorization filters, RBAC performs local, synchronous policy evaluation with minimal latency. This document covers the internal implementation details, policy evaluation algorithms, matching engines, and performance optimizations.

## Architecture

```mermaid
classDiagram
    class RBACFilter {
        +RBACConfig config_
        +Engine engine_
        +decodeHeaders() FilterHeadersStatus
        +allowed() bool
    }

    class Engine {
        +RBAC rbac_
        +Stats stats_
        +allowed() bool
        +evaluate() bool
    }

    class RBAC {
        +Action action_
        +map~string,Policy~ policies_
        +Matcher matcher_
    }

    class Policy {
        +vector~Permission~ permissions_
        +vector~Principal~ principals_
        +Condition condition_
    }

    class Permission {
        <<interface>>
        +match() bool
    }

    class Principal {
        <<interface>>
        +match() bool
    }

    class AndMatcher {
        +vector~Matcher~ matchers_
        +match() bool
    }

    class OrMatcher {
        +vector~Matcher~ matchers_
        +match() bool
    }

    class NotMatcher {
        +Matcher matcher_
        +match() bool
    }

    RBACFilter --> Engine
    Engine --> RBAC
    RBAC --> Policy
    Policy --> Permission
    Policy --> Principal
    Permission <|-- AndMatcher
    Permission <|-- OrMatcher
    Permission <|-- NotMatcher
    Principal <|-- AndMatcher
    Principal <|-- OrMatcher
    Principal <|-- NotMatcher
```

## Policy Evaluation Engine

```mermaid
flowchart TD
    A[Request Arrives] --> B[Extract Context]
    B --> C[Connection Properties]
    B --> D[Request Properties]
    B --> E[TLS Properties]

    C --> F[Source IP]
    C --> G[Destination IP]
    C --> H[Source Port]

    D --> I[Headers]
    D --> J[Path]
    D --> K[Method]

    E --> L[Client Certificate]
    E --> M[SNI]

    F --> N[Build Evaluation Context]
    G --> N
    H --> N
    I --> N
    J --> N
    K --> N
    L --> N
    M --> N

    N --> O{Action Type?}

    O -->|ALLOW| P[Evaluate ALLOW Policies]
    O -->|DENY| Q[Evaluate DENY Policies]
    O -->|LOG| R[Evaluate LOG Policies]

    P --> S{Any Policy<br/>Matches?}
    Q --> T{Any Policy<br/>Matches?}
    R --> U{Any Policy<br/>Matches?}

    S -->|Yes| V[ALLOW Request]
    S -->|No| W[DENY Request]

    T -->|Yes| W
    T -->|No| V

    U -->|Yes| X[Log: Would DENY]
    U -->|No| Y[Log: Would ALLOW]
    X --> V
    Y --> V

    V --> Z[Continue to Next Filter]
    W --> ZZ[Send 403 Forbidden]
```

## Policy Matching Algorithm

```mermaid
sequenceDiagram
    participant Request
    participant RBAC as RBAC Engine
    participant Policy1
    participant Policy2
    participant Policy3

    Request->>RBAC: Check authorization
    RBAC->>RBAC: Get action (ALLOW/DENY)

    Note over RBAC: Iterate policies

    RBAC->>Policy1: evaluate(context)
    Policy1->>Policy1: Match Principals
    alt Principals Match
        Policy1->>Policy1: Match Permissions
        alt Permissions Match
            Policy1-->>RBAC: MATCHED
            Note over RBAC: Found matching policy
            RBAC->>RBAC: Apply action
        else Permissions Don't Match
            Policy1-->>RBAC: NOT MATCHED
        end
    else Principals Don't Match
        Policy1-->>RBAC: NOT MATCHED
    end

    alt Policy1 Not Matched
        RBAC->>Policy2: evaluate(context)
        Policy2-->>RBAC: NOT MATCHED
        RBAC->>Policy3: evaluate(context)
        Policy3-->>RBAC: MATCHED
    end

    RBAC-->>Request: Authorization Decision
```

## Principal Matching Implementation

```mermaid
classDiagram
    class PrincipalMatcher {
        <<interface>>
        +match(context) bool
    }

    class AnyPrincipal {
        +match() bool
        -Always returns true
    }

    class AuthenticatedPrincipal {
        +StringMatcher principal_name_
        +match() bool
        -Check client certificate
    }

    class SourceIpPrincipal {
        +vector~CidrRange~ cidrs_
        +match() bool
        -Check source IP in CIDR
    }

    class DirectRemoteIpPrincipal {
        +vector~CidrRange~ cidrs_
        +match() bool
        -Check direct source IP
    }

    class HeaderPrincipal {
        +HeaderMatcher header_matcher_
        +match() bool
        -Match header value
    }

    class MetadataPrincipal {
        +MetadataMatcher metadata_matcher_
        +match() bool
        -Check dynamic metadata
    }

    class FilterStatePrincipal {
        +string key_
        +StringMatcher matcher_
        +match() bool
        -Check filter state
    }

    PrincipalMatcher <|-- AnyPrincipal
    PrincipalMatcher <|-- AuthenticatedPrincipal
    PrincipalMatcher <|-- SourceIpPrincipal
    PrincipalMatcher <|-- DirectRemoteIpPrincipal
    PrincipalMatcher <|-- HeaderPrincipal
    PrincipalMatcher <|-- MetadataPrincipal
    PrincipalMatcher <|-- FilterStatePrincipal
```

## Permission Matching Implementation

```mermaid
classDiagram
    class PermissionMatcher {
        <<interface>>
        +match(context) bool
    }

    class AnyPermission {
        +match() bool
        -Always returns true
    }

    class DestinationIpPermission {
        +vector~CidrRange~ cidrs_
        +match() bool
    }

    class DestinationPortPermission {
        +uint32_t port_
        +match() bool
    }

    class HeaderPermission {
        +HeaderMatcher header_matcher_
        +match() bool
    }

    class UrlPathPermission {
        +PathMatcher path_matcher_
        +match() bool
    }

    class MetadataPermission {
        +MetadataMatcher metadata_matcher_
        +match() bool
    }

    class RequestedServerNamePermission {
        +StringMatcher sni_matcher_
        +match() bool
    }

    PermissionMatcher <|-- AnyPermission
    PermissionMatcher <|-- DestinationIpPermission
    PermissionMatcher <|-- DestinationPortPermission
    PermissionMatcher <|-- HeaderPermission
    PermissionMatcher <|-- UrlPathPermission
    PermissionMatcher <|-- MetadataPermission
    PermissionMatcher <|-- RequestedServerNamePermission
```

## AND/OR/NOT Logic Implementation

```mermaid
flowchart TD
    A[Logical Combinator] --> B{Type?}

    B -->|AND| C[AndMatcher]
    C --> C1[For each sub-matcher]
    C1 --> C2{All Match?}
    C2 -->|Yes| M[Return TRUE]
    C2 -->|No| N[Return FALSE]

    B -->|OR| D[OrMatcher]
    D --> D1[For each sub-matcher]
    D1 --> D2{Any Match?}
    D2 -->|Yes| M
    D2 -->|No| N

    B -->|NOT| E[NotMatcher]
    E --> E1[Evaluate sub-matcher]
    E1 --> E2{Match?}
    E2 -->|Yes| N
    E2 -->|No| M
```

## IP Address Matching

```mermaid
flowchart TD
    A[Source IP: 10.0.1.5] --> B[CIDR Ranges]

    B --> C1[10.0.0.0/8]
    B --> C2[192.168.0.0/16]
    B --> C3[172.16.0.0/12]

    C1 --> D1{10.0.1.5 in<br/>10.0.0.0/8?}
    D1 -->|Yes| M[MATCH]

    C2 --> D2{10.0.1.5 in<br/>192.168.0.0/16?}
    D2 -->|No| E[Continue]

    C3 --> D3{10.0.1.5 in<br/>172.16.0.0/12?}
    D3 -->|No| N[NO MATCH]

    subgraph "IP Matching Algorithm"
        F[IP Address] --> G[Convert to binary]
        H[CIDR Range] --> I[Convert to binary + mask]
        G --> J[Apply mask]
        I --> J
        J --> K{Prefix Match?}
    end
```

## Header Matching Engine

```mermaid
flowchart TD
    A[Header Matcher] --> B{Match Type?}

    B -->|Exact| C[Exact String Match]
    C --> C1["header == 'value'"]

    B -->|Prefix| D[Prefix Match]
    D --> D1["header.startsWith('value')"]

    B -->|Suffix| E[Suffix Match]
    E --> E1["header.endsWith('value')"]

    B -->|Regex| F[Regex Match]
    F --> F1["regex.match(header)"]

    B -->|Range| G[Range Match]
    G --> G1["min <= header <= max"]

    B -->|Present| H[Presence Check]
    H --> H1["header exists"]

    B -->|Invert| I[Invert Match]
    I --> I1["!matcher.match()"]

    C1 --> J{Result}
    D1 --> J
    E1 --> J
    F1 --> J
    G1 --> J
    H1 --> J
    I1 --> J
```

## TLS Certificate Extraction

```mermaid
sequenceDiagram
    participant Client
    participant TLS as TLS Layer
    participant RBAC as RBAC Filter
    participant Extractor as Certificate Extractor

    Client->>TLS: mTLS Connection
    TLS->>TLS: Verify client certificate
    TLS->>RBAC: Request arrives

    RBAC->>Extractor: Get client certificate info
    Extractor->>TLS: SSL_get_peer_certificate()
    TLS-->>Extractor: X509 Certificate

    Extractor->>Extractor: Extract Subject DN
    Extractor->>Extractor: Extract SAN (Subject Alternative Names)
    Extractor->>Extractor: Extract URI SAN
    Extractor->>Extractor: Extract DNS SAN
    Extractor->>Extractor: Extract Email SAN

    Extractor-->>RBAC: Certificate properties

    RBAC->>RBAC: Match against principal
    Note over RBAC: authenticated:<br/>  principal_name:<br/>    exact: "CN=client1"
```

## Configuration Example - Complex Policy

```yaml
name: envoy.filters.http.rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: ALLOW
    policies:
      "admin-full-access":
        # Principals: Who can access
        principals:
          - and_ids:
              ids:
                # Must have valid mTLS certificate
                - authenticated:
                    principal_name:
                      exact: "CN=admin,OU=Engineering,O=Example Corp"

                # Must be from office network
                - source_ip:
                    address_prefix: "10.0.0.0"
                    prefix_len: 8

                # Must have admin role in JWT
                - metadata:
                    filter: "envoy.filters.http.jwt_authn"
                    path:
                      - key: "jwt_payload"
                      - key: "role"
                    value:
                      string_match:
                        exact: "admin"

        # Permissions: What can be accessed
        permissions:
          - and_rules:
              rules:
                # Admin paths
                - url_path:
                    path:
                      prefix: "/admin"

                # Allow all HTTP methods
                - any: true

      "api-read-only":
        principals:
          - or_ids:
              ids:
                # Valid API key
                - header:
                    name: "x-api-key"
                    present_match: true

                # Or valid JWT
                - metadata:
                    filter: "envoy.filters.http.jwt_authn"
                    path:
                      - key: "jwt_payload"
                    value:
                      present_match: true

        permissions:
          - and_rules:
              rules:
                # API paths
                - url_path:
                    path:
                      prefix: "/api"

                # Only GET and HEAD
                - or_rules:
                    rules:
                      - header:
                          name: ":method"
                          string_match:
                            exact: "GET"
                      - header:
                          name: ":method"
                          string_match:
                            exact: "HEAD"

                # NOT internal APIs
                - not_rule:
                    url_path:
                      path:
                        prefix: "/api/internal"

      "public-access":
        principals:
          - any: true  # Anyone

        permissions:
          - and_rules:
              rules:
                # Public paths only
                - url_path:
                    path:
                      prefix: "/public"

                # GET only
                - header:
                    name: ":method"
                    string_match:
                      exact: "GET"
```

## Policy Evaluation Optimization

```mermaid
flowchart TD
    A[Multiple Policies] --> B[Optimization Strategy]

    B --> C[Short-Circuit Evaluation]
    C --> C1[Stop on first match<br/>for ALLOW action]
    C --> C2[Stop on first match<br/>for DENY action]

    B --> D[Policy Ordering]
    D --> D1[Most specific first]
    D1 --> D2[Most frequently matched first]

    B --> E[Matcher Caching]
    E --> E1[Cache compiled regex]
    E1 --> E2[Cache CIDR lookups]

    B --> F[Early Rejection]
    F --> F1[Check principals first<br/>Cheap operations]
    F1 --> F2[Check permissions second<br/>Expensive operations]

    B --> G[Lazy Evaluation]
    G --> G1[Don't extract metadata<br/>if not needed]
```

## Statistics and Observability

```yaml
# RBAC decision stats
http.rbac.allowed
http.rbac.denied

# Per-policy stats (if enabled)
http.rbac.policy.admin-full-access.allowed
http.rbac.policy.admin-full-access.denied

# Shadow mode stats
http.rbac.shadow_allowed
http.rbac.shadow_denied

# Network filter RBAC stats
listener.0.0.0.0_443.rbac.allowed
listener.0.0.0.0_443.rbac.denied
```

## Dynamic Metadata Integration

```mermaid
sequenceDiagram
    participant JWT as JWT Filter
    participant RBAC as RBAC Filter
    participant Metadata as Dynamic Metadata

    JWT->>JWT: Verify JWT token
    JWT->>Metadata: Store JWT payload
    Note over Metadata: Namespace: envoy.filters.http.jwt_authn<br/>Key: jwt_payload<br/>Value: { "sub": "user1", "role": "admin" }

    Note over RBAC: Request arrives

    RBAC->>RBAC: Evaluate policy
    RBAC->>Metadata: Get metadata["envoy.filters.http.jwt_authn"]["jwt_payload"]["role"]
    Metadata-->>RBAC: "admin"

    RBAC->>RBAC: Match against principal
    Note over RBAC: metadata:<br/>  filter: "envoy.filters.http.jwt_authn"<br/>  path: [{key: "jwt_payload"}, {key: "role"}]<br/>  value: {string_match: {exact: "admin"}}

    RBAC-->>RBAC: Principal matches!
```

## Filter State Integration

```mermaid
flowchart TD
    A[Upstream Filter] --> B[Set Filter State]
    B --> C["filterState().setData('user_tier', 'premium')"]

    C --> D[RBAC Filter]
    D --> E[Read Filter State]
    E --> F["filterState().getData('user_tier')"]

    F --> G{Match?}
    G -->|premium == premium| H[MATCH]
    G -->|premium != basic| I[NO MATCH]

    H --> J[Allow Request]
    I --> K[Deny Request]
```

## Configuration Example - Network Filter RBAC

```yaml
listeners:
  - name: tcp_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 9999
    filter_chains:
      - filters:
          - name: envoy.filters.network.rbac
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
              stat_prefix: tcp_rbac
              rules:
                action: ALLOW
                policies:
                  "allow-internal":
                    permissions:
                      - any: true
                    principals:
                      - source_ip:
                          address_prefix: "10.0.0.0"
                          prefix_len: 8

                  "allow-vpn":
                    permissions:
                      - destination_port: 9999
                    principals:
                      - source_ip:
                          address_prefix: "172.16.0.0"
                          prefix_len: 12

          - name: envoy.filters.network.tcp_proxy
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
              stat_prefix: tcp
              cluster: backend_cluster
```

## Performance Characteristics

```mermaid
graph TD
    A[Policy Complexity] --> B[Evaluation Time]

    B --> C[Simple Policy:<br/>1 principal + 1 permission]
    C --> C1["~1-5 μs"]

    B --> D[Medium Policy:<br/>AND/OR with 5 matchers]
    D --> D1["~10-50 μs"]

    B --> E[Complex Policy:<br/>Nested AND/OR/NOT + regex]
    E --> E1["~50-500 μs"]

    B --> F[Metadata Lookup]
    F --> F1["Additional ~1-10 μs"]

    B --> G[Filter State Lookup]
    G --> G1["Additional ~0.5-5 μs"]
```

## Memory Usage

```mermaid
flowchart TD
    A[RBAC Configuration] --> B[Policy Storage]
    B --> C[Compiled Matchers]

    C --> D[Regex Compilation]
    D --> D1["~1-10 KB per regex"]

    C --> E[CIDR Trees]
    E --> E1["~100 bytes per CIDR"]

    C --> F[String Matchers]
    F --> F1["~50 bytes per matcher"]

    B --> G[Policy Structure]
    G --> G1["~500 bytes per policy"]

    D1 --> H[Total Memory]
    E1 --> H
    F1 --> H
    G1 --> H

    H --> I["Typical: 10-100 KB<br/>Complex: 100 KB - 1 MB"]
```

## Best Practices

### 1. Order Policies by Specificity

```yaml
policies:
  # Most specific first
  "super-admin":
    principals:
      - authenticated: { principal_name: { exact: "CN=superadmin" } }
      - source_ip: { address_prefix: "10.0.0.1", prefix_len: 32 }
    permissions:
      - any: true

  # Less specific
  "admin":
    principals:
      - authenticated: { principal_name: { suffix: ",OU=Admin" } }
    permissions:
      - url_path: { path: { prefix: "/admin" } }

  # Least specific
  "authenticated-users":
    principals:
      - authenticated: { principal_name: { present_match: true } }
    permissions:
      - url_path: { path: { prefix: "/app" } }
```

### 2. Use AND Before OR

```yaml
# GOOD: Check cheap conditions first
principals:
  - and_ids:
      ids:
        - source_ip: { ... }      # Cheap
        - authenticated: { ... }  # Medium
        - metadata: { ... }       # Expensive

# AVOID: Expensive operations first
principals:
  - and_ids:
      ids:
        - metadata: { ... }       # Expensive
        - source_ip: { ... }      # Cheap
```

### 3. Leverage Shadow Mode

```yaml
# Test policies without affecting traffic
shadow_rules:
  action: DENY
  policies:
    "new-policy-test":
      # ... policy definition ...

shadow_rules_stat_prefix: "rbac_shadow"
```

### 4. Use Descriptive Policy Names

```yaml
policies:
  "admin-write-operations":  # GOOD: Clear intent
    # ...

  "policy1":  # BAD: Not descriptive
    # ...
```

### 5. Monitor and Alert

```yaml
# Monitor denied requests
alert: RBACHighDenyRate
expr: rate(envoy_http_rbac_denied[5m]) > 10
annotations:
  summary: High RBAC deny rate
```

## Troubleshooting

### Debug RBAC Decisions

```bash
# Enable debug logging
curl -X POST "http://localhost:9901/logging?rbac=debug"

# Check RBAC stats
curl http://localhost:9901/stats | grep rbac

# View policy configuration
curl http://localhost:9901/config_dump | jq '.configs[] | select(.["@type"] | contains("RBAC"))'
```

### Common Issues

1. **All requests denied**
   - Check action is ALLOW (not DENY)
   - Verify at least one policy matches
   - Check principal and permission logic

2. **Specific requests denied unexpectedly**
   - Enable debug logging to see evaluation
   - Check NOT rules (might be inverting logic)
   - Verify metadata/filter state availability

3. **Performance degradation**
   - Check for complex regex matchers
   - Monitor policy evaluation time
   - Simplify policies if possible

4. **Metadata not matching**
   - Ensure upstream filter set metadata
   - Verify namespace and key path
   - Check metadata is available at RBAC evaluation time

## References

- [Envoy RBAC Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/rbac_filter)
- [RBAC Network Filter](https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/rbac_filter)
- [Authorization API](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/rbac/v3/rbac.proto)
