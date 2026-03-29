# CORS (Cross-Origin Resource Sharing) Filter

## Overview

The CORS filter handles Cross-Origin Resource Sharing requests and responses. It adds appropriate CORS headers to responses based on configuration and handles preflight OPTIONS requests. This filter enables web applications running in browsers to make requests to Envoy-proxied services from different origins.

## Key Responsibilities

- Handle preflight OPTIONS requests
- Add CORS response headers
- Validate origin headers
- Support credentials and cookies
- Configure allowed methods and headers
- Set max age for preflight caching
- Handle expose headers

## Architecture

```mermaid
classDiagram
    class CorsFilter {
        +CorsPolicy policy_
        +decodeHeaders() FilterHeadersStatus
        +encodeHeaders() FilterHeadersStatus
        +isOriginAllowed() bool
    }

    class CorsPolicy {
        +vector~string~ allow_origins_
        +vector~string~ allow_methods_
        +vector~string~ allow_headers_
        +vector~string~ expose_headers_
        +string max_age_
        +bool allow_credentials_
        +bool enabled_
    }

    class OriginMatcher {
        +match() bool
    }

    CorsFilter --> CorsPolicy
    CorsFilter --> OriginMatcher
```

## Request Flow - Preflight Request

```mermaid
sequenceDiagram
    participant Browser
    participant Envoy as CORS Filter
    participant Upstream

    Note over Browser: User triggers cross-origin<br/>request from example.com<br/>to api.service.com

    Browser->>Envoy: OPTIONS /api/resource
    Note over Browser,Envoy: Headers:<br/>Origin: https://example.com<br/>Access-Control-Request-Method: POST<br/>Access-Control-Request-Headers: content-type

    Envoy->>Envoy: Check if Origin is allowed
    alt Origin Allowed
        Envoy->>Envoy: Check if Method is allowed
        alt Method Allowed
            Envoy->>Envoy: Check if Headers are allowed
            alt Headers Allowed
                Envoy-->>Browser: 200 OK
                Note over Envoy,Browser: Headers:<br/>Access-Control-Allow-Origin: https://example.com<br/>Access-Control-Allow-Methods: GET,POST,PUT<br/>Access-Control-Allow-Headers: content-type<br/>Access-Control-Max-Age: 3600<br/>Access-Control-Allow-Credentials: true
                Note over Browser: Cache preflight response<br/>for max-age seconds
            else Headers Not Allowed
                Envoy-->>Browser: 200 OK (no CORS headers)
                Note over Browser: Browser blocks request
            end
        else Method Not Allowed
            Envoy-->>Browser: 200 OK (no CORS headers)
        end
    else Origin Not Allowed
        Envoy-->>Browser: 200 OK (no CORS headers)
    end

    Note over Browser: Preflight successful,<br/>send actual request

    Browser->>Envoy: POST /api/resource
    Note over Browser,Envoy: Headers:<br/>Origin: https://example.com<br/>Content-Type: application/json

    Envoy->>Upstream: POST /api/resource
    Upstream-->>Envoy: 200 OK + Response
    Envoy->>Envoy: Add CORS headers
    Envoy-->>Browser: 200 OK + Response
    Note over Envoy,Browser: Headers:<br/>Access-Control-Allow-Origin: https://example.com<br/>Access-Control-Allow-Credentials: true<br/>Access-Control-Expose-Headers: X-Custom
```

## Request Flow - Simple Request

```mermaid
sequenceDiagram
    participant Browser
    participant Envoy as CORS Filter
    participant Upstream

    Note over Browser: Simple GET request<br/>(no preflight needed)

    Browser->>Envoy: GET /api/data
    Note over Browser,Envoy: Headers:<br/>Origin: https://example.com

    Envoy->>Upstream: GET /api/data
    Upstream-->>Envoy: 200 OK + Data

    Envoy->>Envoy: Check Origin
    alt Origin Allowed
        Envoy->>Envoy: Add CORS headers
        Envoy-->>Browser: 200 OK + Data
        Note over Envoy,Browser: Headers:<br/>Access-Control-Allow-Origin: https://example.com<br/>Access-Control-Expose-Headers: X-Custom
    else Origin Not Allowed
        Envoy-->>Browser: 200 OK + Data (no CORS headers)
        Note over Browser: Browser blocks access to response
    end
```

## CORS Decision Flow

```mermaid
flowchart TD
    A[Request Arrives] --> B{Has Origin<br/>Header?}
    B -->|No| Z[Skip CORS Processing]
    B -->|Yes| C{Is OPTIONS<br/>Request?}

    C -->|Yes - Preflight| D{Has Access-Control-<br/>Request-Method?}
    D -->|No| Z
    D -->|Yes| E[Validate Preflight]

    E --> F{Origin<br/>Allowed?}
    F -->|No| G[No CORS Headers]
    F -->|Yes| H{Method<br/>Allowed?}
    H -->|No| G
    H -->|Yes| I{Headers<br/>Allowed?}
    I -->|No| G
    I -->|Yes| J[Add Preflight Response Headers]
    J --> J1[Access-Control-Allow-Origin]
    J1 --> J2[Access-Control-Allow-Methods]
    J2 --> J3[Access-Control-Allow-Headers]
    J3 --> J4[Access-Control-Max-Age]
    J4 --> J5[Access-Control-Allow-Credentials]
    J5 --> K[Return 200 OK]

    C -->|No - Actual Request| L[Forward to Upstream]
    L --> M[Get Response]
    M --> N{Origin<br/>Allowed?}
    N -->|No| O[No CORS Headers]
    N -->|Yes| P[Add Response Headers]
    P --> P1[Access-Control-Allow-Origin]
    P1 --> P2[Access-Control-Expose-Headers]
    P2 --> P3[Access-Control-Allow-Credentials]
    P3 --> Q[Return Response]

    G --> K
    O --> Q
    Z --> R[Continue Normally]
```

## Origin Matching

```mermaid
flowchart TD
    A[Origin Header] --> B{Match Type?}

    B -->|Exact Match| C["exact: 'https://example.com'"]
    C --> C1{Exact Match?}
    C1 -->|Yes| M[Allowed]
    C1 -->|No| N[Not Allowed]

    B -->|Prefix Match| D["prefix: 'https://example'"]
    D --> D1{Starts With?}
    D1 -->|Yes| M
    D1 -->|No| N

    B -->|Suffix Match| E["suffix: '.example.com'"]
    E --> E1{Ends With?}
    E1 -->|Yes| M
    E1 -->|No| N

    B -->|Regex Match| F["safe_regex: '.*\\.example\\.com'"]
    F --> F1{Regex Match?}
    F1 -->|Yes| M
    F1 -->|No| N

    B -->|Wildcard| G["allow_origin: ['*']"]
    G --> H{Credentials<br/>Enabled?}
    H -->|Yes| I[Error: Invalid Config]
    H -->|No| M

    B -->|Allow Any| J["allow_origin_string_match:<br/>- safe_regex: '.*'"]
    J --> K{Credentials?}
    K -->|Yes| L[Return Actual Origin]
    K -->|No| M
```

## Configuration Example - Basic

```yaml
name: envoy.filters.http.cors
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.Cors
```

Note: Basic filter config. Actual CORS policy is typically configured per route/virtual host.

## Route-Level CORS Configuration

```yaml
virtual_hosts:
  - name: api
    domains: ["api.example.com"]
    cors:
      allow_origin_string_match:
        - exact: "https://example.com"
        - exact: "https://app.example.com"
        - safe_regex:
            regex: "https://.*\\.example\\.com"
      allow_methods: "GET, POST, PUT, DELETE, OPTIONS"
      allow_headers: "content-type, x-requested-with, authorization"
      expose_headers: "x-custom-header, x-request-id"
      max_age: "3600"
      allow_credentials: true

    routes:
      - match:
          prefix: "/api"
        route:
          cluster: api_cluster
```

## Configuration Example - Per-Route Override

```yaml
routes:
  - match:
      prefix: "/public"
    route:
      cluster: public_cluster
    typed_per_filter_config:
      envoy.filters.http.cors:
        "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
        allow_origin_string_match:
          - exact: "*"  # Allow all origins for public API
        allow_methods: "GET, OPTIONS"
        allow_headers: "content-type"
        max_age: "7200"

  - match:
      prefix: "/admin"
    route:
      cluster: admin_cluster
    typed_per_filter_config:
      envoy.filters.http.cors:
        "@type": type.googleapis.com/envoy.extensions.filters.http.cors.v3.CorsPolicy
        allow_origin_string_match:
          - exact: "https://admin.example.com"
        allow_methods: "GET, POST, PUT, DELETE, OPTIONS"
        allow_headers: "content-type, authorization"
        allow_credentials: true
        max_age: "600"
```

## Preflight Request Handling

```mermaid
stateDiagram-v2
    [*] --> CheckOrigin
    CheckOrigin --> ValidateMethod: Origin OK
    CheckOrigin --> Reject: Origin Not Allowed
    ValidateMethod --> ValidateHeaders: Method OK
    ValidateMethod --> Reject: Method Not Allowed
    ValidateHeaders --> BuildResponse: Headers OK
    ValidateHeaders --> Reject: Headers Not Allowed
    BuildResponse --> [*]: 200 OK with CORS headers
    Reject --> [*]: 200 OK without CORS headers

    note right of CheckOrigin
        Check Origin header against
        allow_origin_string_match
    end note

    note right of ValidateMethod
        Check Access-Control-Request-Method
        against allow_methods
    end note

    note right of ValidateHeaders
        Check Access-Control-Request-Headers
        against allow_headers
    end note
```

## Response Headers

```mermaid
mindmap
  root((CORS Headers))
    Preflight Response
      Access-Control-Allow-Origin
        "Allowed origin or *"
      Access-Control-Allow-Methods
        "GET, POST, PUT, DELETE"
      Access-Control-Allow-Headers
        "content-type, authorization"
      Access-Control-Max-Age
        "Cache duration in seconds"
      Access-Control-Allow-Credentials
        "true or omitted"

    Actual Response
      Access-Control-Allow-Origin
        "Matched origin"
      Access-Control-Expose-Headers
        "Headers accessible to browser"
      Access-Control-Allow-Credentials
        "true or omitted"
```

## Simple vs Preflighted Requests

```mermaid
flowchart TD
    A[Browser Request] --> B{Simple Request?}

    B -->|Yes| C[Send Request Directly]
    C --> D[Conditions:<br/>- Method: GET, POST, HEAD<br/>- Headers: Simple headers only<br/>- Content-Type: specific types]

    B -->|No - Preflighted| E[Send OPTIONS Preflight]
    E --> F[Conditions:<br/>- Custom methods: PUT, DELETE, PATCH<br/>- Custom headers<br/>- Custom Content-Type]

    C --> G[Server adds CORS headers]
    F --> H[Server responds to OPTIONS]
    H --> I{Preflight<br/>Successful?}
    I -->|Yes| J[Send Actual Request]
    I -->|No| K[Browser blocks request]
    J --> G
```

## Configuration Example - Advanced

```yaml
virtual_hosts:
  - name: multi_tenant_api
    domains: ["*.api.example.com"]
    cors:
      # Allow multiple specific origins
      allow_origin_string_match:
        - exact: "https://app1.example.com"
        - exact: "https://app2.example.com"
        - prefix: "https://partner-"
        - safe_regex:
            regex: "https://tenant-[0-9]+\\.example\\.com"

      # Allow common HTTP methods
      allow_methods: "GET, POST, PUT, DELETE, PATCH, OPTIONS"

      # Allow custom headers
      allow_headers: >-
        authorization,
        content-type,
        x-requested-with,
        x-api-key,
        x-client-version

      # Expose custom response headers
      expose_headers: >-
        x-request-id,
        x-response-time,
        x-rate-limit-remaining

      # Cache preflight for 1 hour
      max_age: "3600"

      # Allow credentials (cookies, auth)
      allow_credentials: true

      # Enable CORS filter
      filter_enabled:
        default_value:
          numerator: 100
          denominator: HUNDRED
```

## Key Features

### 1. Origin Validation
- Exact match
- Prefix/suffix match
- Regex match
- Wildcard support

### 2. Preflight Handling
- Automatic OPTIONS response
- Method validation
- Header validation
- Max age caching

### 3. Credentials Support
- Cookie support
- Authorization headers
- Proper origin handling

### 4. Flexible Configuration
- Per-route overrides
- Virtual host defaults
- Runtime feature flags

### 5. Header Management
- Allow methods
- Allow headers
- Expose headers

## Common Use Cases

### 1. Web Application API
Allow web apps to call backend APIs

### 2. Multi-Tenant SaaS
Different origins per tenant

### 3. Mobile Web Apps
Progressive web apps calling APIs

### 4. Microservices Dashboard
Admin UI calling multiple services

### 5. Third-Party Integrations
Allow partner applications

### 6. Development Environments
Allow localhost for development

## Best Practices

1. **Avoid wildcards in production** - Specify exact origins
2. **Don't use * with credentials** - Security violation
3. **Set appropriate max_age** - Balance security and performance
4. **Limit allowed methods** - Only what's needed
5. **Validate request headers** - Allow only required headers
6. **Use HTTPS for origins** - Secure communication
7. **Monitor CORS errors** - Track client issues
8. **Test with browsers** - CORS is browser-enforced
9. **Document allowed origins** - For client developers
10. **Use per-route config** - Different rules for different endpoints

## Common Issues and Solutions

```mermaid
flowchart TD
    A[CORS Error in Browser] --> B{Error Type?}

    B -->|No Access-Control-Allow-Origin| C[Origin not in allow list]
    C --> C1[Add origin to config]

    B -->|Credentials issue| D[allow_credentials not set]
    D --> D1[Set allow_credentials: true]
    D --> D2[Cannot use * with credentials]

    B -->|Method not allowed| E[Method not in allow_methods]
    E --> E1[Add method to allow_methods]

    B -->|Header not allowed| F[Header not in allow_headers]
    F --> F1[Add header to allow_headers]

    B -->|Preflight fails| G[OPTIONS request blocked]
    G --> G1[Check allow_methods includes OPTIONS]
    G --> G2[Check origin validation]

    B -->|Cannot read response header| H[Header not in expose_headers]
    H --> H1[Add header to expose_headers]
```

## Security Considerations

1. **Validate origins strictly** - Don't use overly broad patterns
2. **Avoid wildcard with credentials** - Spec violation, browsers reject
3. **HTTPS only** - HTTP origins are insecure
4. **Limit exposed headers** - Don't expose sensitive data
5. **Monitor origin patterns** - Detect malicious origins
6. **Rate limit OPTIONS** - Prevent DoS on preflight
7. **Validate origin format** - Prevent header injection

## Testing CORS

```bash
# Test preflight request
curl -X OPTIONS https://api.example.com/resource \ -H "Origin: https://example.com" \ -H "Access-Control-Request-Method: POST" \ -H "Access-Control-Request-Headers: content-type" \ -v

# Test actual request
curl -X POST https://api.example.com/resource \ -H "Origin: https://example.com" \ -H "Content-Type: application/json" \ -d '{"test": "data"}' \ -v
```

## Related Filters

- **jwt_authn**: Authentication before CORS
- **rbac**: Authorization with CORS
- **router**: Final routing after CORS

## References

- [Envoy CORS Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/cors_filter)
- [MDN CORS Documentation](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS)
- [CORS Specification](https://fetch.spec.whatwg.org/#http-cors-protocol)
