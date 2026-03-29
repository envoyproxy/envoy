# Rate Limit Filter

## Overview

The Rate Limit filter integrates with an external rate limit service to enforce rate limits on HTTP requests. It supports global rate limiting across multiple Envoy instances by calling a centralized rate limit service that implements the RateLimit v3 API.

## Key Responsibilities

- Call external rate limit service
- Generate rate limit descriptors from request attributes
- Handle rate limit responses (OK, Over Limit)
- Support stage-based rate limiting
- Configure rate limit domain
- Handle failure modes
- Add rate limit response headers

## Architecture

```mermaid
classDiagram
    class Filter {
        +FilterConfig config_
        +Client client_
        +vector~Descriptor~ descriptors_
        +decodeHeaders() FilterHeadersStatus
        +complete() void
    }

    class FilterConfig {
        +string domain_
        +uint32_t stage_
        +string timeout_
        +bool failure_mode_deny_
        +RateLimitStats stats_
    }

    class Client {
        <<interface>>
        +limit() void
        +cancel() void
    }

    class GrpcClientImpl {
        +Grpc::AsyncClient client_
        +limit() void
        +onSuccess() void
        +onFailure() void
    }

    class Descriptor {
        +vector~Entry~ entries_
    }

    class Entry {
        +string key_
        +string value_
    }

    Filter --> FilterConfig
    Filter --> Client
    Filter --> Descriptor
    Client <|-- GrpcClientImpl
    Descriptor --> Entry
```

## Request Flow

```mermaid
sequenceDiagram
    participant Client
    participant FM as Filter Manager
    participant RL as RateLimit Filter
    participant RLS as Rate Limit Service
    participant Router

    Client->>FM: HTTP Request
    FM->>RL: decodeHeaders(headers, end_stream)

    RL->>RL: Generate descriptors
    Note over RL: From route config<br/>request attributes

    alt Descriptors Generated
        RL->>RLS: ShouldRateLimit(domain, descriptors)

        alt Under Limit
            RLS-->>RL: OK (no limit exceeded)
            opt Add Response Headers
                RL->>RL: X-RateLimit-Limit<br/>X-RateLimit-Remaining<br/>X-RateLimit-Reset
            end
            RL->>FM: Continue
            FM->>Router: Process request

        else Over Limit
            RLS-->>RL: OVER_LIMIT
            opt Add Response Headers
                RL->>RL: X-RateLimit-Limit<br/>X-RateLimit-Remaining: 0<br/>X-RateLimit-Reset<br/>Retry-After
            end
            RL->>FM: sendLocalReply(429, "Rate limited")
            FM->>Client: 429 Too Many Requests

        else Service Error
            RLS-->>RL: Error/Timeout

            alt Failure Mode Deny
                RL->>FM: sendLocalReply(500)
                FM->>Client: 500 Internal Server Error
            else Failure Mode Allow
                RL->>FM: Continue (bypass rate limit)
                FM->>Router: Process request
            end
        end

    else No Descriptors
        Note over RL: No rate limit config for route
        RL->>FM: Continue
        FM->>Router: Process request
    end
```

## Descriptor Generation

```mermaid
flowchart TD
    A[Request] --> B[Get Route Config]
    B --> C{Rate Limit<br/>Actions?}
    C -->|No| Z[Skip - No Descriptors]
    C -->|Yes| D[For Each Action]

    D --> E{Action Type?}

    E -->|source_cluster| F[Add source cluster name]
    E -->|destination_cluster| G[Add destination cluster]
    E -->|request_headers| H[Extract header value]
    E -->|remote_address| I[Add client IP]
    E -->|generic_key| J[Add static key-value]
    E -->|header_value_match| K[Check header match]
    E -->|dynamic_metadata| L[Extract metadata]
    E -->|metadata| M[Extract route metadata]
    E -->|query_parameter| N[Extract query param]

    K -->|Match| O[Add descriptor entry]
    K -->|No Match| P[Skip entry]

    F --> Q[Add to Descriptors]
    G --> Q
    H --> Q
    I --> Q
    J --> Q
    O --> Q
    L --> Q
    M --> Q
    N --> Q

    Q --> R{More Actions?}
    R -->|Yes| D
    R -->|No| S[Send Descriptors to RLS]
    P --> R

    S --> T[Rate Limit Decision]
```

## Rate Limit Service Protocol

```mermaid
sequenceDiagram
    participant Envoy
    participant RLS as Rate Limit Service
    participant Redis as Backend (Redis)

    Envoy->>RLS: ShouldRateLimit(domain, descriptors)
    Note over Envoy,RLS: Domain: "apis"<br/>Descriptors: [<br/>  {entries: [{key: "path", value: "/api"}]},<br/>  {entries: [{key: "user_id", value: "123"}]}<br/>]

    RLS->>Redis: Check rate limit counters
    Redis-->>RLS: Current counts

    alt All Limits OK
        RLS-->>Envoy: RateLimitResponse
        Note over RLS,Envoy: overall_code: OK<br/>statuses: [OK, OK]<br/>response_headers_to_add: [<br/>  {key: "X-RateLimit-Limit", value: "100"},<br/>  {key: "X-RateLimit-Remaining", value: "75"}<br/>]

    else Any Limit Exceeded
        RLS-->>Envoy: RateLimitResponse
        Note over RLS,Envoy: overall_code: OVER_LIMIT<br/>statuses: [OK, OVER_LIMIT]<br/>response_headers_to_add: [<br/>  {key: "X-RateLimit-Limit", value: "10"},<br/>  {key: "X-RateLimit-Remaining", value: "0"},<br/>  {key: "X-RateLimit-Reset", value: "1234567890"},<br/>  {key: "Retry-After", value: "60"}<br/>]
    end
```

## Descriptor Hierarchy

```mermaid
flowchart TD
    A[Request /api/users?user_id=123] --> B[Generate Descriptors]

    B --> C[Descriptor 1:<br/>Global API Limit]
    C --> C1["[{key: 'api', value: 'true'}]"]

    B --> D[Descriptor 2:<br/>Path-based Limit]
    D --> D1["[{key: 'path', value: '/api'}]"]

    B --> E[Descriptor 3:<br/>User-specific Limit]
    E --> E1["[<br/>{key: 'path', value: '/api'},<br/>{key: 'user_id', value: '123'}<br/>]"]

    C1 --> F[Check against RLS]
    D1 --> F
    E1 --> F

    F --> G{All Pass?}
    G -->|Yes| H[Allow Request]
    G -->|No| I[429 Over Limit]
```

## Stage-Based Rate Limiting

```mermaid
sequenceDiagram
    participant Request
    participant Stage0 as Rate Limit (Stage 0)
    participant Stage1 as Rate Limit (Stage 1)
    participant Router

    Note over Stage0,Stage1: Multiple rate limit filters<br/>with different stages

    Request->>Stage0: decodeHeaders()
    Note over Stage0: Check routes with stage=0
    Stage0->>Stage0: Generate descriptors (stage 0)
    Stage0-->>Request: Check rate limit

    alt Stage 0 OK
        Request->>Stage1: decodeHeaders()
        Note over Stage1: Check routes with stage=1
        Stage1->>Stage1: Generate descriptors (stage 1)
        Stage1-->>Request: Check rate limit

        alt Stage 1 OK
            Request->>Router: Continue
        else Stage 1 Over Limit
            Stage1-->>Request: 429 Too Many Requests
        end

    else Stage 0 Over Limit
        Stage0-->>Request: 429 Too Many Requests
    end
```

## Configuration Example

```yaml
name: envoy.filters.http.ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ratelimit.v3.RateLimit
  domain: "apis"
  stage: 0
  failure_mode_deny: false
  timeout: 0.25s
  rate_limit_service:
    grpc_service:
      envoy_grpc:
        cluster_name: rate_limit_service
      timeout: 0.25s
    transport_api_version: V3
  enable_x_ratelimit_headers: DRAFT_VERSION_03
  disable_x_envoy_ratelimit_headers: false
```

## Route Configuration Example

```yaml
routes:
  - match:
      prefix: "/api"
    route:
      cluster: api_cluster
      rate_limits:
        - stage: 0
          actions:
            # Global API rate limit
            - generic_key:
                descriptor_value: "api"

            # Per-path rate limit
            - request_headers:
                header_name: ":path"
                descriptor_key: "path"

            # Per-user rate limit
            - request_headers:
                header_name: "x-user-id"
                descriptor_key: "user_id"

  - match:
      prefix: "/admin"
    route:
      cluster: admin_cluster
      rate_limits:
        - stage: 0
          actions:
            # Admin endpoint - stricter limits
            - generic_key:
                descriptor_value: "admin"

            - remote_address: {}
```

## Rate Limit Service Config Example

The rate limit service uses a configuration like:

```yaml
domain: apis
descriptors:
  # Global API limit: 1000 req/hour
  - key: api
    value: "true"
    rate_limit:
      unit: HOUR
      requests_per_unit: 1000

  # Path-specific limit: 100 req/minute
  - key: path
    value: "/api"
    rate_limit:
      unit: MINUTE
      requests_per_unit: 100

  # User-specific limit: 10 req/minute per user
  - key: path
    value: "/api"
    descriptors:
      - key: user_id
        rate_limit:
          unit: MINUTE
          requests_per_unit: 10
```

## Response Header Management

```mermaid
flowchart TD
    A[Rate Limit Response] --> B{enable_x_ratelimit_headers?}

    B -->|DRAFT_VERSION_03| C[Add Standard Headers]
    C --> C1["X-RateLimit-Limit: 100"]
    C --> C2["X-RateLimit-Remaining: 45"]
    C --> C3["X-RateLimit-Reset: 1234567890"]

    B -->|OFF| D[Skip Standard Headers]

    C --> E{disable_x_envoy_ratelimit_headers?}
    D --> E

    E -->|false| F[Add Envoy Headers]
    F --> F1["X-Envoy-RateLimit-Limit"]
    F --> F2["X-Envoy-RateLimit-Reset"]

    E -->|true| G[Skip Envoy Headers]

    F --> H{Over Limit?}
    G --> H

    H -->|Yes| I[Add Retry-After Header]
    I --> I1["Retry-After: 60"]
    H -->|No| J[No Retry-After]

    I1 --> K[Return to Client]
    J --> K
```

## Action Types

```mermaid
classDiagram
    class Action {
        <<interface>>
        +populateDescriptors() bool
    }

    class SourceClusterAction {
        +populateDescriptors() bool
        -adds source cluster name
    }

    class DestinationClusterAction {
        +populateDescriptors() bool
        -adds destination cluster name
    }

    class RequestHeadersAction {
        +string header_name_
        +string descriptor_key_
        +populateDescriptors() bool
    }

    class RemoteAddressAction {
        +populateDescriptors() bool
        -adds client IP address
    }

    class GenericKeyAction {
        +string descriptor_value_
        +string descriptor_key_
        +populateDescriptors() bool
    }

    class HeaderValueMatchAction {
        +HeaderMatcher matcher_
        +string descriptor_value_
        +populateDescriptors() bool
    }

    class DynamicMetadataAction {
        +string metadata_key_
        +string descriptor_key_
        +populateDescriptors() bool
    }

    Action <|-- SourceClusterAction
    Action <|-- DestinationClusterAction
    Action <|-- RequestHeadersAction
    Action <|-- RemoteAddressAction
    Action <|-- GenericKeyAction
    Action <|-- HeaderValueMatchAction
    Action <|-- DynamicMetadataAction
```

## Failure Handling

```mermaid
stateDiagram-v2
    [*] --> CheckingRateLimit
    CheckingRateLimit --> UnderLimit: OK Response
    CheckingRateLimit --> OverLimit: OVER_LIMIT Response
    CheckingRateLimit --> ServiceError: Timeout/Error

    ServiceError --> FailureDeny: failure_mode_deny=true
    ServiceError --> FailureAllow: failure_mode_deny=false

    UnderLimit --> [*]: Continue
    OverLimit --> [*]: 429 Response
    FailureDeny --> [*]: 500 Response
    FailureAllow --> [*]: Continue (bypass)

    note right of ServiceError
        Rate limit service
        unreachable or timeout
    end note

    note right of FailureDeny
        Fail closed
        Reject request
    end note

    note right of FailureAllow
        Fail open
        Allow request
    end note
```

## Key Features

### 1. External Rate Limit Service
- Centralized rate limiting across Envoy fleet
- Shared state via Redis or similar backend
- Consistent rate limiting

### 2. Flexible Descriptor Generation
- Multiple action types
- Hierarchical descriptors
- Dynamic descriptor values

### 3. Stage-Based Limiting
- Multiple rate limit filters in chain
- Different limits at different stages
- Fine-grained control

### 4. Response Headers
- Standard X-RateLimit-* headers
- Envoy-specific headers
- Retry-After header

### 5. Failure Modes
- Fail open (allow on error)
- Fail closed (deny on error)
- Configurable per filter

## Statistics

| Stat | Type | Description |
|------|------|-------------|
| ratelimit.ok | Counter | Requests under limit |
| ratelimit.over_limit | Counter | Requests over limit |
| ratelimit.error | Counter | Rate limit service errors |
| ratelimit.failure_mode_allowed | Counter | Allowed due to failure mode |

## Common Use Cases

### 1. API Rate Limiting
Limit API calls per user/client

### 2. DDoS Protection
Protect against volumetric attacks

### 3. Fair Usage
Ensure fair resource distribution

### 4. Cost Control
Limit expensive operations

### 5. Tiered Rate Limits
Different limits for different customer tiers

### 6. Burst Protection
Prevent traffic spikes

## Best Practices

1. **Set appropriate timeouts** - Balance accuracy and latency
2. **Use failure_mode_deny carefully** - Understand availability impact
3. **Implement hierarchical descriptors** - Global + specific limits
4. **Monitor rate limit service** - Critical dependency
5. **Use stages for complex scenarios** - Different limits at different points
6. **Configure appropriate limits** - Based on upstream capacity
7. **Add meaningful response headers** - Help clients understand limits
8. **Log rate limit events** - For analysis and tuning
9. **Test under load** - Ensure rate limit service can handle traffic
10. **Use local rate limit** - For per-instance limits

## Comparison: Global vs Local Rate Limiting

| Aspect | Global (this filter) | Local (local_ratelimit) |
|--------|----------------------|-------------------------|
| State | Shared across instances | Per-instance only |
| Latency | Higher (network call) | Lower (local check) |
| Consistency | Consistent across fleet | Per-instance limits |
| Complexity | Requires external service | Self-contained |
| Use Case | API rate limiting | Connection limiting |

## Related Filters

- **local_ratelimit**: Per-instance rate limiting
- **ext_authz**: Authorization before rate limiting
- **fault**: Combine with fault injection for testing

## References

- [Envoy Rate Limit Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/rate_limit_filter)
- [RateLimit Service API](https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/ratelimit/v3/rls.proto)
- [Lyft Rate Limit Service](https://github.com/envoyproxy/ratelimit)
