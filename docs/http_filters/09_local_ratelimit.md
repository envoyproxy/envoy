# Local Rate Limit Filter

## Overview

The Local Rate Limit filter implements rate limiting on a per-Envoy-instance basis using a token bucket algorithm. Unlike the global rate limit filter which calls an external service, this filter performs rate limiting locally, making it faster but limited to per-instance quotas. It's ideal for protecting against sudden traffic spikes and implementing connection/request limits.

## Key Responsibilities

- Per-instance rate limiting using token bucket
- Support multiple rate limit descriptors
- Local state management (no external service)
- Fast rate limit decisions (no network calls)
- Configurable response on rate limit
- Integration with virtual hosts and routes
- Statistical tracking

## Architecture

```mermaid
classDiagram
    class LocalRateLimitFilter {
        +FilterConfig config_
        +TokenBucket bucket_
        +decodeHeaders() FilterHeadersStatus
        +checkRateLimit() bool
    }

    class FilterConfig {
        +TokenBucket token_bucket_
        +string stat_prefix_
        +bool enabled_
        +uint32_t http_status_
        +vector~HeaderValueOption~ response_headers_
    }

    class TokenBucket {
        +uint32_t max_tokens_
        +Duration fill_interval_
        +uint32_t tokens_per_fill_
        +uint32_t current_tokens_
        +consume() bool
    }

    class LocalRateLimiterImpl {
        +map~string,TokenBucket~ descriptors_
        +requestAllowed() bool
    }

    LocalRateLimitFilter --> FilterConfig
    FilterConfig --> TokenBucket
    LocalRateLimitFilter --> LocalRateLimiterImpl
    LocalRateLimiterImpl --> TokenBucket
```

## Request Flow

```mermaid
sequenceDiagram
    participant Client1
    participant Client2
    participant LRL as Local RateLimit Filter
    participant TokenBucket
    participant Router

    Note over TokenBucket: Initial: 100 tokens<br/>Refill: 10 tokens/second

    Client1->>LRL: Request 1
    LRL->>TokenBucket: Try consume 1 token
    TokenBucket-->>LRL: Success (99 tokens remaining)
    LRL->>Router: Continue
    Router-->>Client1: 200 OK

    Client2->>LRL: Request 2
    LRL->>TokenBucket: Try consume 1 token
    TokenBucket-->>LRL: Success (98 tokens remaining)
    LRL->>Router: Continue
    Router-->>Client2: 200 OK

    Note over TokenBucket: Many requests...

    Client1->>LRL: Request N
    LRL->>TokenBucket: Try consume 1 token
    TokenBucket-->>LRL: Failed (0 tokens)
    LRL-->>Client1: 429 Too Many Requests
    Note over Client1: Rate limited!

    Note over TokenBucket: Wait 1 second...<br/>Refill 10 tokens

    Client2->>LRL: Request N+1
    LRL->>TokenBucket: Try consume 1 token
    TokenBucket-->>LRL: Success (9 tokens)
    LRL->>Router: Continue
    Router-->>Client2: 200 OK
```

## Token Bucket Algorithm

```mermaid
flowchart TD
    A[Request Arrives] --> B[Get Current Time]
    B --> C[Calculate Tokens to Add]
    C --> D[elapsed = now - last_fill_time]
    D --> E[tokens_to_add = elapsed * tokens_per_fill]
    E --> F[current_tokens = min<br/>current_tokens + tokens_to_add,<br/>max_tokens]
    F --> G{current_tokens >= 1?}
    G -->|Yes| H[Consume 1 Token]
    H --> I[current_tokens -= 1]
    I --> J[last_fill_time = now]
    J --> K[Allow Request]
    G -->|No| L[Reject Request - 429]
```

## Token Bucket State Over Time

```mermaid
graph LR
    A[Time 0s<br/>Tokens: 100] -->|10 requests| B[Tokens: 90]
    B -->|Wait 1s<br/>+10 tokens| C[Tokens: 100]
    C -->|50 requests| D[Tokens: 50]
    D -->|50 requests| E[Tokens: 0]
    E -->|New request| F[RATE LIMITED]
    E -->|Wait 1s<br/>+10 tokens| G[Tokens: 10]
    G -->|10 requests| H[Tokens: 0]
    H -->|Wait 1s| I[Tokens: 10]
```

## Configuration Example - Basic

```yaml
name: envoy.filters.http.local_ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
  stat_prefix: http_local_rate_limiter
  token_bucket:
    max_tokens: 100
    tokens_per_fill: 10
    fill_interval: 1s
  filter_enabled:
    runtime_key: local_rate_limit.enabled
    default_value:
      numerator: 100
      denominator: HUNDRED
  filter_enforced:
    runtime_key: local_rate_limit.enforced
    default_value:
      numerator: 100
      denominator: HUNDRED
  response_headers_to_add:
    - append_action: OVERWRITE_IF_EXISTS_OR_ADD
      header:
        key: "x-local-rate-limit"
        value: "true"
  status_code: 429
```

## Configuration Example - Advanced

```yaml
name: envoy.filters.http.local_ratelimit
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
  stat_prefix: http_local_rate_limiter

  # Token bucket configuration
  token_bucket:
    max_tokens: 1000        # Bucket capacity
    tokens_per_fill: 100    # Tokens added per interval
    fill_interval: 1s       # Refill every second

  # Effective rate: 100 requests/second with burst up to 1000

  # Enable rate limiting
  filter_enabled:
    runtime_key: local_rate_limit.enabled
    default_value:
      numerator: 100
      denominator: HUNDRED

  # Enforce rate limiting (can be used for shadow mode)
  filter_enforced:
    runtime_key: local_rate_limit.enforced
    default_value:
      numerator: 100
      denominator: HUNDRED

  # Custom status code
  status_code: 429

  # Add response headers
  response_headers_to_add:
    - append_action: OVERWRITE_IF_EXISTS_OR_ADD
      header:
        key: "x-ratelimit-limit"
        value: "100"
    - append_action: OVERWRITE_IF_EXISTS_OR_ADD
      header:
        key: "x-ratelimit-remaining"
        value: "%DYNAMIC_METADATA(envoy.filters.http.local_ratelimit:token_bucket)%"

  # Descriptors for different rate limits
  descriptors:
    - entries:
        - key: "path"
          value: "/api"
      token_bucket:
        max_tokens: 500
        tokens_per_fill: 50
        fill_interval: 1s

    - entries:
        - key: "path"
          value: "/admin"
      token_bucket:
        max_tokens: 100
        tokens_per_fill: 10
        fill_interval: 1s
```

## Per-Route Configuration

```yaml
routes:
  - match:
      prefix: "/api/v1"
    route:
      cluster: api_v1_cluster
    typed_per_filter_config:
      envoy.filters.http.local_ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
        token_bucket:
          max_tokens: 200
          tokens_per_fill: 20
          fill_interval: 1s

  - match:
      prefix: "/api/v2"
    route:
      cluster: api_v2_cluster
    typed_per_filter_config:
      envoy.filters.http.local_ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
        token_bucket:
          max_tokens: 500
          tokens_per_fill: 50
          fill_interval: 1s

  - match:
      prefix: "/public"
    route:
      cluster: public_cluster
    typed_per_filter_config:
      envoy.filters.http.local_ratelimit:
        "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
        token_bucket:
          max_tokens: 5000
          tokens_per_fill: 500
          fill_interval: 1s
```

## Shadow Mode (Monitoring)

```mermaid
sequenceDiagram
    participant Client
    participant LRL as Local RateLimit
    participant Stats
    participant Router

    Note over LRL: filter_enabled: 100%<br/>filter_enforced: 0%<br/>(Shadow Mode)

    Client->>LRL: Request
    LRL->>LRL: Check rate limit
    alt Would Be Rate Limited
        LRL->>Stats: Increment shadow stats
        Note over Stats: http_local_rate_limit.shadow_limited
        LRL->>Router: Continue (allow)
        Router-->>Client: 200 OK
    else Under Limit
        LRL->>Stats: Increment ok stats
        LRL->>Router: Continue
        Router-->>Client: 200 OK
    end

    Note over LRL,Stats: Observe behavior without<br/>impacting production traffic
```

## Descriptor-Based Rate Limiting

```mermaid
flowchart TD
    A[Request] --> B[Extract Descriptor]
    B --> C{Match Descriptor?}

    C -->|path=/api| D[Use API Rate Limit]
    D --> D1[100 req/s, burst 500]

    C -->|path=/admin| E[Use Admin Rate Limit]
    E --> E1[10 req/s, burst 100]

    C -->|path=/public| F[Use Public Rate Limit]
    F --> F1[1000 req/s, burst 5000]

    C -->|No Match| G[Use Default Rate Limit]
    G --> G1[Global limit]

    D1 --> H[Check Token Bucket]
    E1 --> H
    F1 --> H
    G1 --> H

    H --> I{Tokens Available?}
    I -->|Yes| J[Allow Request]
    I -->|No| K[429 Rate Limited]
```

## Rate Limit Decision Flow

```mermaid
flowchart TD
    A[Request Arrives] --> B{Filter Enabled?}
    B -->|No| Z[Allow - No Rate Limiting]
    B -->|Yes| C[Get/Create Descriptor]
    C --> D[Get Token Bucket]
    D --> E[Refill Tokens]
    E --> F{Tokens Available?}
    F -->|Yes| G[Consume Token]
    F -->|No| H{Filter Enforced?}

    G --> I[Increment OK Stats]
    I --> Z

    H -->|Yes| J[Increment Rate Limited Stats]
    J --> K[Add Response Headers]
    K --> L[Return 429]

    H -->|No| M[Increment Shadow Stats]
    M --> N[Log Would Be Limited]
    N --> Z
```

## Burst Handling

```mermaid
graph TD
    subgraph "Scenario: Burst Traffic"
        A[Bucket: 1000 tokens<br/>Refill: 100/s] -->|Burst of<br/>500 requests| B[500 tokens remain]
        B -->|Another 500<br/>requests| C[0 tokens - Bucket empty]
        C -->|More requests| D[Rate Limited: 429]
        C -->|Wait 1s<br/>+100 tokens| E[100 tokens available]
        E -->|100 requests| F[0 tokens again]
        F -->|Steady state:<br/>100 req/s| G[Sustainable rate]
    end
```

## Statistics

| Stat | Type | Description |
|------|------|-------------|
| http_local_rate_limit.enabled | Counter | Requests with rate limiting enabled |
| http_local_rate_limit.ok | Counter | Requests allowed |
| http_local_rate_limit.rate_limited | Counter | Requests rate limited |
| http_local_rate_limit.shadow_limited | Counter | Would be limited (shadow mode) |

## Common Use Cases

### 1. Connection Limiting
Limit connections per Envoy instance

```yaml
token_bucket:
  max_tokens: 10000      # Max concurrent connections
  tokens_per_fill: 1000  # New connections per second
  fill_interval: 1s
```

### 2. API Request Limiting
Limit requests per endpoint

```yaml
descriptors:
  - entries:
      - key: "path"
        value: "/api/search"
    token_bucket:
      max_tokens: 50
      tokens_per_fill: 10
      fill_interval: 1s
```

### 3. Burst Protection
Allow bursts but limit sustained rate

```yaml
token_bucket:
  max_tokens: 1000    # Allow burst of 1000
  tokens_per_fill: 100  # But sustained at 100/s
  fill_interval: 1s
```

### 4. DDoS Protection
Basic protection against volumetric attacks

### 5. Resource Protection
Protect expensive endpoints

### 6. Fair Usage
Prevent single client from monopolizing resources

## Token Bucket Calculation Examples

```mermaid
mindmap
  root((Token Bucket<br/>Examples))
    Strict Limiting
      max_tokens: 100
      tokens_per_fill: 100
      fill_interval: 1s
      "= 100 req/s, no burst"
    Moderate Burst
      max_tokens: 500
      tokens_per_fill: 100
      fill_interval: 1s
      "= 100 req/s, burst 500"
    Large Burst
      max_tokens: 10000
      tokens_per_fill: 100
      fill_interval: 1s
      "= 100 req/s, burst 10k"
    Per Minute
      max_tokens: 1000
      tokens_per_fill: 1000
      fill_interval: 60s
      "= 1000 req/min"
```

## Best Practices

1. **Set max_tokens for bursts** - Allow temporary traffic spikes
2. **Monitor shadow mode first** - Test limits before enforcing
3. **Use per-route limits** - Different limits for different endpoints
4. **Set appropriate fill_interval** - Usually 1s for simplicity
5. **Add informative headers** - Help clients understand limits
6. **Monitor rate_limited stat** - Adjust limits if too strict
7. **Combine with global rate limit** - For fleet-wide limits
8. **Test under load** - Verify limits work as expected
9. **Document rate limits** - Inform API consumers
10. **Use runtime overrides** - Adjust without restart

## Comparison: Local vs Global Rate Limiting

| Aspect | Local (this filter) | Global (ratelimit) |
|--------|---------------------|---------------------|
| State | Per-instance | Shared across fleet |
| Latency | Very low (local) | Higher (network call) |
| Accuracy | Per-instance only | Fleet-wide accurate |
| Complexity | Simple | Requires external service |
| Use Case | Instance protection | User/API key quotas |
| Failure Mode | N/A (local) | Configurable |
| Scaling | Scales with instances | Depends on service |

## Combined Local + Global Rate Limiting

```yaml
http_filters:
  # 1. Local rate limit (per-instance protection)
  - name: envoy.filters.http.local_ratelimit
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.local_ratelimit.v3.LocalRateLimit
      token_bucket:
        max_tokens: 10000
        tokens_per_fill: 1000
        fill_interval: 1s

  # 2. Global rate limit (per-user quotas)
  - name: envoy.filters.http.ratelimit
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.ratelimit.v3.RateLimit
      domain: "api_quotas"
      rate_limit_service:
        grpc_service:
          envoy_grpc:
            cluster_name: ratelimit_service

  # 3. Router (must be last)
  - name: envoy.filters.http.router
```

## Troubleshooting

```mermaid
flowchart TD
    A[Rate Limit Issue] --> B{Issue Type?}

    B -->|Too Many 429s| C[Limits Too Strict]
    C --> C1[Increase max_tokens<br/>or tokens_per_fill]

    B -->|No Rate Limiting| D[Check Configuration]
    D --> D1[Verify filter_enabled<br/>and filter_enforced]

    B -->|Inconsistent| E[Different Per-Route Configs]
    E --> E1[Review route-level<br/>overrides]

    B -->|Too Lenient| F[Insufficient Protection]
    F --> F1[Decrease limits<br/>or add descriptors]
```

## Testing Rate Limits

```bash
# Test rate limit with curl
for i in {1..150}; do
  curl -w "%{http_code}\n" -o /dev/null -s http://api.example.com/test
done

# Expected output:
# 200 (first 100 requests)
# 429 (next 50 requests)

# Test with Apache Bench
ab -n 1000 -c 10 http://api.example.com/test

# Test with wrk
wrk -t12 -c100 -d30s http://api.example.com/test
```

## Related Filters

- **ratelimit**: Global rate limiting with external service
- **ext_authz**: Can integrate rate limiting logic
- **rbac**: Can be combined for access control

## References

- [Envoy Local Rate Limit Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/local_rate_limit_filter)
- [Token Bucket Algorithm](https://en.wikipedia.org/wiki/Token_bucket)
