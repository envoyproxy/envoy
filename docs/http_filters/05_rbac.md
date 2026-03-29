# RBAC (Role-Based Access Control) Filter

## Overview

The RBAC (Role-Based Access Control) filter enforces access control policies based on request attributes such as source IP, headers, path, and TLS certificate properties. It provides a powerful and flexible way to implement authorization policies directly within Envoy without external service calls.

## Key Responsibilities

- Evaluate authorization policies against requests
- Support both allow and deny policies
- Match on multiple request attributes
- Evaluate TLS certificate properties
- Support complex logical combinations (AND/OR/NOT)
- Log authorization decisions
- Shadow mode for testing policies

## Architecture

```mermaid
classDiagram
    class Filter {
        +RBACConfig config_
        +Engine engine_
        +decodeHeaders() FilterHeadersStatus
        +evaluate() bool
    }

    class RBACConfig {
        +RBAC rules_
        +string shadow_rules_stat_prefix_
        +ShadowRBAC shadow_rules_
    }

    class Engine {
        +RBAC rbac_
        +allowed() bool
    }

    class RBAC {
        +Action action_
        +map~string,Policy~ policies_
    }

    class Policy {
        +vector~Permission~ permissions_
        +vector~Principal~ principals_
    }

    class Permission {
        +PermissionMatcher matcher_
    }

    class Principal {
        +PrincipalMatcher matcher_
    }

    Filter --> RBACConfig
    Filter --> Engine
    Engine --> RBAC
    RBAC --> Policy
    Policy --> Permission
    Policy --> Principal
```

## Request Flow

```mermaid
sequenceDiagram
    participant Client
    participant FM as Filter Manager
    participant RBAC as RBAC Filter
    participant Engine as RBAC Engine
    participant Router

    Client->>FM: HTTP Request
    FM->>RBAC: decodeHeaders(headers, end_stream)

    RBAC->>Engine: evaluate(action=ALLOW, policies)

    alt Policy Evaluation
        loop For Each Policy
            Engine->>Engine: Match Principals
            alt Principal Matches
                Engine->>Engine: Match Permissions
                alt Permission Matches
                    Engine-->>RBAC: Policy Matches
                else Permission Doesn't Match
                    Engine->>Engine: Try Next Policy
                end
            else Principal Doesn't Match
                Engine->>Engine: Try Next Policy
            end
        end
    end

    alt Action: ALLOW
        alt Any Policy Matched
            RBAC->>FM: Continue
            FM->>Router: Process request
        else No Policy Matched
            RBAC->>FM: sendLocalReply(403, "RBAC: access denied")
            FM->>Client: 403 Forbidden
        end

    else Action: DENY
        alt Any Policy Matched
            RBAC->>FM: sendLocalReply(403, "RBAC: access denied")
            FM->>Client: 403 Forbidden
        else No Policy Matched
            RBAC->>FM: Continue
            FM->>Router: Process request
        end
    end

    opt Shadow Mode Enabled
        RBAC->>Engine: evaluate(shadow_rules)
        Engine-->>RBAC: Shadow result
        RBAC->>RBAC: Log shadow result (no action)
    end
```

## Policy Evaluation Logic

```mermaid
flowchart TD
    A[Request] --> B{RBAC Action Type?}

    B -->|ALLOW| C[Evaluate ALLOW Policies]
    B -->|DENY| D[Evaluate DENY Policies]
    B -->|LOG| E[Evaluate LOG Policies]

    C --> F{Any Policy<br/>Matches?}
    F -->|Yes| G[Allow Request]
    F -->|No| H[Deny Request - 403]

    D --> I{Any Policy<br/>Matches?}
    I -->|Yes| J[Deny Request - 403]
    I -->|No| K[Allow Request]

    E --> L{Any Policy<br/>Matches?}
    L -->|Yes| M[Log: Would Deny]
    L -->|No| N[Log: Would Allow]
    M --> O[Continue Request]
    N --> O

    subgraph "Policy Match Logic"
        P[Policy] --> Q[Match Principals]
        Q -->|AND| R[Match Permissions]
        R -->|Result| S{Both Match?}
        S -->|Yes| T[Policy Matches]
        S -->|No| U[Policy Doesn't Match]
    end
```

## Principal Matching

```mermaid
flowchart TD
    A[Principal Matcher] --> B{Matcher Type?}

    B -->|any| C[Always Match]

    B -->|authenticated| D[Check TLS Certificate]
    D --> D1{Has Valid<br/>Certificate?}
    D1 -->|Yes| D2[Match Principal Name]
    D1 -->|No| D3[No Match]

    B -->|source_ip| E[Check Source IP]
    E --> E1{IP in CIDR<br/>Range?}
    E1 -->|Yes| E2[Match]
    E1 -->|No| E3[No Match]

    B -->|direct_remote_ip| F[Check Direct Source IP]
    F --> F1{Matches?}

    B -->|header| G[Check Header Value]
    G --> G1{Header Matches?}

    B -->|metadata| H[Check Dynamic Metadata]
    H --> H1{Metadata Matches?}

    B -->|filter_state| I[Check Filter State]
    I --> I1{State Matches?}

    B -->|url_path| J[Check Request Path]
    J --> J1{Path Matches?}

    B -->|and_ids| K[Match ALL Principals]
    K --> K1{All Match?}

    B -->|or_ids| L[Match ANY Principal]
    L --> L1{Any Matches?}

    B -->|not_id| M[Invert Match]
    M --> M1[Negate Result]

    C --> Z[Matched]
    D2 --> Z
    E2 --> Z
    F1 --> Z
    G1 --> Z
    H1 --> Z
    I1 --> Z
    J1 --> Z
    K1 --> Z
    L1 --> Z
    M1 --> Z
```

## Permission Matching

```mermaid
flowchart TD
    A[Permission Matcher] --> B{Matcher Type?}

    B -->|any| C[Always Match]

    B -->|destination_ip| D[Check Destination IP]
    D --> D1{IP Matches?}

    B -->|destination_port| E[Check Destination Port]
    E --> E1{Port Matches?}

    B -->|header| F[Check Header]
    F --> F1{Header Matches?}

    B -->|url_path| G[Check Path]
    G --> G1{Path Matches<br/>Pattern?}

    B -->|metadata| H[Check Metadata]
    H --> H1{Metadata Matches?}

    B -->|requested_server_name| I[Check SNI]
    I --> I1{SNI Matches?}

    B -->|and_rules| J[Match ALL Permissions]
    J --> J1{All Match?}

    B -->|or_rules| K[Match ANY Permission]
    K --> K1{Any Matches?}

    B -->|not_rule| L[Invert Match]
    L --> L1[Negate Result]

    C --> Z[Matched]
    D1 --> Z
    E1 --> Z
    F1 --> Z
    G1 --> Z
    H1 --> Z
    I1 --> Z
    J1 --> Z
    K1 --> Z
    L1 --> Z
```

## Policy Combination

```mermaid
graph TD
    subgraph "Complex Policy: Admin Access"
        A[Policy: admin_access] --> B[Principals: AND]
        B --> C[Has Valid Certificate]
        B --> D[Certificate Contains 'admin']
        B --> E[Source IP from Office Network]

        A --> F[Permissions: AND]
        F --> G[Path starts with '/admin']
        F --> H[Method is GET/POST/PUT/DELETE]
    end

    subgraph "Complex Policy: API Access"
        I[Policy: api_access] --> J[Principals: OR]
        J --> K[API Key Header Present]
        J --> L[OAuth Token Valid]

        I --> M[Permissions: AND]
        M --> N[Path starts with '/api']
        M --> O[NOT Path contains '/internal']
    end
```

## Shadow Mode

```mermaid
sequenceDiagram
    participant Request
    participant RBAC as RBAC Filter
    participant Production as Production Rules
    participant Shadow as Shadow Rules
    participant Stats

    Request->>RBAC: Incoming request

    RBAC->>Production: Evaluate production rules
    Production-->>RBAC: Result: Allow/Deny

    par Shadow Evaluation
        RBAC->>Shadow: Evaluate shadow rules
        Shadow-->>RBAC: Shadow Result
        RBAC->>Stats: Increment shadow stats
        Note over RBAC,Stats: shadow_allowed<br/>shadow_denied
    end

    alt Production Result: Allow
        RBAC-->>Request: Continue (200 OK)
    else Production Result: Deny
        RBAC-->>Request: 403 Forbidden
    end

    Note over Shadow: Shadow result doesn't<br/>affect actual request
```

## Configuration Example - Basic

```yaml
name: envoy.filters.http.rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: ALLOW
    policies:
      "admin-access":
        permissions:
          - and_rules:
              rules:
                - header:
                    name: ":path"
                    string_match:
                      prefix: "/admin"
                - header:
                    name: ":method"
                    string_match:
                      exact: "GET"
        principals:
          - authenticated:
              principal_name:
                exact: "admin-user"

      "internal-network":
        permissions:
          - any: true
        principals:
          - source_ip:
              address_prefix: "10.0.0.0"
              prefix_len: 8
```

## Configuration Example - Complex

```yaml
name: envoy.filters.http.rbac
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.rbac.v3.RBAC
  rules:
    action: ALLOW
    policies:
      "service-admin":
        permissions:
          - and_rules:
              rules:
                # Must access /admin paths
                - url_path:
                    path:
                      prefix: "/admin"
                # Must use POST, PUT, or DELETE
                - or_rules:
                    rules:
                      - header: { name: ":method", string_match: { exact: "POST" } }
                      - header: { name: ":method", string_match: { exact: "PUT" } }
                      - header: { name: ":method", string_match: { exact: "DELETE" } }
        principals:
          - and_ids:
              ids:
                # Must have valid mTLS certificate
                - authenticated:
                    principal_name:
                      suffix: ".admin.example.com"
                # Must be from office network
                - source_ip:
                    address_prefix: "192.168.0.0"
                    prefix_len: 16

      "api-users":
        permissions:
          - and_rules:
              rules:
                # API paths
                - url_path:
                    path:
                      prefix: "/api"
                # But NOT internal APIs
                - not_rule:
                    url_path:
                      path:
                        prefix: "/api/internal"
        principals:
          - or_ids:
              ids:
                # Valid API key
                - header:
                    name: "x-api-key"
                    present_match: true
                # Or valid OAuth token
                - metadata:
                    filter: "envoy.filters.http.jwt_authn"
                    path:
                      - key: "jwt_payload"
                    value:
                      string_match:
                        prefix: "user:"

  # Shadow rules for testing
  shadow_rules:
    action: ALLOW
    policies:
      "new-policy-test":
        permissions:
          - any: true
        principals:
          - header:
              name: "x-new-auth"
              present_match: true
  shadow_rules_stat_prefix: "rbac_shadow"
```

## Use Cases by Matcher Type

```mermaid
mindmap
  root((RBAC Matchers))
    Principals
      source_ip
        "IP Whitelisting"
        "Geo-blocking"
        "Office Access"
      authenticated
        "mTLS Required"
        "Certificate Validation"
        "Service Identity"
      header
        "API Key Auth"
        "Custom Auth Headers"
        "Client Identification"
      metadata
        "JWT Claims"
        "Dynamic Auth Data"
        "Filter State"

    Permissions
      url_path
        "Route Protection"
        "API Versioning"
        "Resource Access"
      destination_port
        "Port-based Access"
        "Service Selection"
      header
        "Method Restriction"
        "Content-Type Checks"
        "Custom Rules"
      requested_server_name
        "SNI-based Routing"
        "Multi-tenant Access"
```

## Key Features

### 1. Multiple Matcher Types
- Source IP and CIDR ranges
- TLS certificate properties
- HTTP headers
- URL paths
- Dynamic metadata
- Filter state

### 2. Logical Combinations
- AND: All conditions must match
- OR: Any condition must match
- NOT: Invert condition

### 3. Action Types
- **ALLOW**: Whitelist approach
- **DENY**: Blacklist approach
- **LOG**: Shadow mode for testing

### 4. Policy Structure
- Named policies for clarity
- Principals: Who can access
- Permissions: What can be accessed

### 5. Shadow Mode
- Test policies without affecting traffic
- Compare production vs shadow results
- Safe policy rollout

## Statistics

| Stat | Type | Description |
|------|------|-------------|
| rbac.allowed | Counter | Requests allowed by RBAC |
| rbac.denied | Counter | Requests denied by RBAC |
| rbac.shadow_allowed | Counter | Would be allowed (shadow) |
| rbac.shadow_denied | Counter | Would be denied (shadow) |

## Common Use Cases

### 1. API Authorization
Control access to API endpoints based on authentication

### 2. Admin Panel Protection
Restrict admin routes to specific IPs or certificates

### 3. Internal Service Communication
Allow only mTLS-authenticated services

### 4. Geographic Restrictions
Block or allow based on source IP regions

### 5. Method-Based Access Control
Restrict write operations to specific principals

### 6. Multi-Tenant Access
Isolate tenant access using metadata

## Best Practices

1. **Use ALLOW action** - Whitelist is more secure than blacklist
2. **Combine with authentication** - RBAC works best after authn
3. **Use shadow mode** - Test policies before enforcement
4. **Keep policies simple** - Complex policies are hard to debug
5. **Use descriptive policy names** - Makes debugging easier
6. **Monitor denied requests** - Identify legitimate vs malicious
7. **Document policies** - Maintain policy documentation
8. **Use metadata matching** - Leverage JWT claims and filter state
9. **Test thoroughly** - Cover all principal/permission combinations
10. **Version control configs** - Track policy changes

## Debugging RBAC Policies

```mermaid
flowchart TD
    A[Request Denied] --> B[Check RBAC Stats]
    B --> C{rbac.denied<br/>Incrementing?}
    C -->|No| D[Check if RBAC<br/>Filter Active]
    C -->|Yes| E[Enable Debug Logging]

    E --> F[Check Policy Evaluation]
    F --> G{Which Policy<br/>Checked?}
    G --> H[Check Principal Match]
    G --> I[Check Permission Match]

    H --> J{Principal<br/>Matched?}
    J -->|No| K[Debug Principal<br/>Conditions]
    J -->|Yes| L[Check Why Permission<br/>Failed]

    I --> M{Permission<br/>Matched?}
    M -->|No| N[Debug Permission<br/>Conditions]
    M -->|Yes| O[Check Action Type]

    K --> P[Verify:<br/>- Source IP<br/>- Headers<br/>- Certificate<br/>- Metadata]
    N --> Q[Verify:<br/>- Path<br/>- Method<br/>- Headers<br/>- Port]

    O --> R{Action Type?}
    R -->|ALLOW| S[Should Match at Least<br/>One Policy]
    R -->|DENY| T[Matched DENY Policy]
```

## Performance Considerations

1. **Policy Evaluation Order**: Policies evaluated sequentially
2. **Complex Matchers**: AND/OR/NOT can be expensive
3. **Metadata Lookups**: Dynamic metadata access has overhead
4. **Number of Policies**: More policies = more evaluation time
5. **Caching**: Certificate and metadata are not re-evaluated per request

## Security Considerations

1. **Default Deny**: Use ALLOW action for security
2. **Certificate Validation**: Always validate TLS certificates
3. **IP Spoofing**: Use direct_remote_ip for X-Forwarded-For protection
4. **Header Injection**: Validate headers from trusted sources only
5. **Metadata Trust**: Only trust metadata from authenticated filters

## Related Filters

- **ext_authz**: External authorization service
- **jwt_authn**: JWT validation before RBAC
- **lua**: Custom authorization logic

## References

- [Envoy RBAC Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/rbac_filter)
- [RBAC Network Filter](https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/rbac_filter)
