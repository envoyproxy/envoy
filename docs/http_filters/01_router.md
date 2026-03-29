# Router Filter

## Overview

The Router filter is the most critical HTTP filter in Envoy. It is responsible for routing requests to upstream clusters based on route configuration. This filter must be the last filter in the HTTP filter chain and performs the actual proxying of the request to the selected upstream host.

## Key Responsibilities

- Route selection based on request headers and path
- Upstream cluster selection
- Load balancing across upstream hosts
- Retry logic and timeout management
- Request shadowing (traffic mirroring)
- Connection pooling
- Upstream protocol selection (HTTP/1.1, HTTP/2, HTTP/3)

## Architecture

```mermaid
classDiagram
    class Filter {
        +FilterConfig config_
        +Http::StreamDecoderFilterCallbacks* callbacks_
        +UpstreamRequest upstream_request_
        +decodeHeaders() FilterHeadersStatus
        +decodeData() FilterDataStatus
        +onUpstreamHeaders() void
        +onUpstreamData() void
        +onUpstreamComplete() void
        +retry() void
    }

    class FilterConfig {
        +bool start_child_span_
        +bool suppress_envoy_headers_
        +bool strict_check_headers_
        +FilterStats stats_
    }

    class UpstreamRequest {
        +GenPool* conn_pool_
        +Http::RequestEncoder* request_encoder_
        +encodeHeaders() void
        +encodeData() void
        +onPoolReady() void
        +onPoolFailure() void
        +onResetStream() void
    }

    class RouteEntry {
        +timeout() milliseconds
        +retryPolicy() RetryPolicy
        +cluster() string
        +virtualHost() VirtualHost
    }

    Filter --> FilterConfig
    Filter --> UpstreamRequest
    Filter --> RouteEntry
    UpstreamRequest --> ConnectionPool
```

## Request Flow

```mermaid
sequenceDiagram
    participant Client
    participant FilterManager as Filter Manager
    participant Router as Router Filter
    participant RouteTable as Route Table
    participant ConnPool as Connection Pool
    participant Upstream

    Client->>FilterManager: HTTP Request
    FilterManager->>Router: decodeHeaders(headers, end_stream)
    Router->>RouteTable: selectRoute(headers)
    RouteTable-->>Router: RouteEntry

    alt Route Found
        Router->>Router: Create UpstreamRequest
        Router->>ConnPool: newStream(callbacks)

        alt Connection Available
            ConnPool-->>Router: onPoolReady(encoder, host)
            Router->>Upstream: encodeHeaders(headers)

            opt Has Body
                FilterManager->>Router: decodeData(data, end_stream)
                Router->>Upstream: encodeData(data)
            end

            Upstream-->>Router: Response Headers
            Router->>FilterManager: encodeHeaders(response_headers)
            Upstream-->>Router: Response Body
            Router->>FilterManager: encodeData(response_data)

        else Connection Failed
            ConnPool-->>Router: onPoolFailure(reason)
            Router->>Router: Check Retry Policy

            alt Should Retry
                Router->>ConnPool: newStream(callbacks)
            else No Retry
                Router->>FilterManager: Send 503 Error
                FilterManager->>Client: 503 Service Unavailable
            end
        end

    else No Route
        Router->>FilterManager: Send 404 Error
        FilterManager->>Client: 404 Not Found
    end
```

## Retry Logic Flow

```mermaid
flowchart TD
    A[Request Failed] --> B{Retry Policy<br/>Configured?}
    B -->|No| Z[Return Error]
    B -->|Yes| C{Max Retries<br/>Exceeded?}
    C -->|Yes| Z
    C -->|No| D{Budget<br/>Available?}
    D -->|No| Z
    D -->|Yes| E{Failure Type<br/>Retryable?}
    E -->|No| Z
    E -->|Yes| F[Apply Backoff]
    F --> G[Select New Host]
    G --> H{Same Host<br/>Allowed?}
    H -->|No| I[Exclude Previous Host]
    H -->|Yes| J[Allow Same Host]
    I --> K[Initiate Retry]
    J --> K
    K --> L{Retry Succeeds?}
    L -->|Yes| M[Return Response]
    L -->|No| A
```

## Timeout Management

```mermaid
stateDiagram-v2
    [*] --> RequestStarted
    RequestStarted --> WaitingForConnection: Connect to Upstream
    WaitingForConnection --> SendingRequest: Connection Ready
    WaitingForConnection --> ConnectTimeout: Per-Try Timeout
    SendingRequest --> WaitingForResponse: Request Sent
    WaitingForResponse --> ReceivingResponse: Headers Received
    ReceivingResponse --> Complete: Full Response
    WaitingForConnection --> Retry: Connection Failed
    WaitingForResponse --> Retry: Per-Try Timeout
    ReceivingResponse --> Retry: Stream Reset
    Retry --> WaitingForConnection: Retry Allowed
    Retry --> Failed: Max Retries Exceeded
    ConnectTimeout --> Failed
    Complete --> [*]
    Failed --> [*]

    note right of WaitingForConnection
        Per-Try Timeout Active
    end note

    note right of WaitingForResponse
        Global Timeout Active
    end note
```

## Hedging (Parallel Requests)

```mermaid
sequenceDiagram
    participant Router
    participant Primary as Primary Request
    participant Hedge1 as Hedge Request 1
    participant Hedge2 as Hedge Request 2
    participant Upstream1
    participant Upstream2
    participant Upstream3

    Router->>Primary: Initial Request
    Primary->>Upstream1: Request

    Note over Router,Primary: Hedge timeout elapsed

    Router->>Hedge1: Hedged Request
    Hedge1->>Upstream2: Request

    Note over Router,Hedge1: Another hedge timeout

    Router->>Hedge2: Hedged Request
    Hedge2->>Upstream3: Request

    Upstream2-->>Hedge1: Response (First!)
    Hedge1-->>Router: Success
    Router->>Primary: Cancel
    Router->>Hedge2: Cancel
    Primary->>Upstream1: Reset Stream
    Hedge2->>Upstream3: Reset Stream
```

## Shadow/Mirroring Flow

```mermaid
sequenceDiagram
    participant Client
    participant Router
    participant Primary as Primary Upstream
    participant Shadow as Shadow Upstream

    Client->>Router: Request

    par Primary Request
        Router->>Primary: Forward Request
        Primary-->>Router: Response
        Router-->>Client: Response
    and Shadow Request (Fire-and-Forget)
        Router->>Shadow: Mirrored Request
        Note over Router,Shadow: Response ignored
        Shadow-->>Router: Response (Discarded)
    end
```

## Load Balancing Context

```mermaid
flowchart TD
    A[Router Filter] --> B[Create LoadBalancerContext]
    B --> C{Hash Policy<br/>Defined?}
    C -->|Yes| D[Compute Hash]
    C -->|No| E[Use Random]
    D --> F{Retry?}
    E --> F
    F -->|Yes| G[Mark Previous Hosts]
    F -->|No| H[No Exclusions]
    G --> I[LoadBalancer.chooseHost]
    H --> I
    I --> J{Priority Set<br/>Selection}
    J --> K{Host Selection<br/>Algorithm}
    K -->|Round Robin| L[Next Host]
    K -->|Least Request| M[Host with Min Requests]
    K -->|Random| N[Random Host]
    K -->|Ring Hash| O[Hash Ring Lookup]
    L --> P[Selected Host]
    M --> P
    N --> P
    O --> P
```

## Configuration Example

```yaml
name: envoy.filters.http.router
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router
  dynamic_stats: true
  start_child_span: true
  upstream_log:
    - name: envoy.access_loggers.file
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
        path: /var/log/envoy/upstream.log
  suppress_envoy_headers: false
  strict_check_headers:
    - x-envoy-retry-on
    - x-envoy-max-retries
```

## Key Features

### 1. Connection Pooling
- Reuses connections to upstream hosts
- Configurable per-cluster
- Supports HTTP/1.1, HTTP/2, and HTTP/3

### 2. Retry Logic
- Configurable retry policies (5xx, gateway-error, reset, etc.)
- Exponential backoff
- Retry budgets to prevent retry storms
- Host selection during retries

### 3. Timeout Management
- Global request timeout
- Per-try timeout (for each retry attempt)
- Idle timeout
- Connect timeout

### 4. Request Hedging
- Send parallel requests to multiple hosts
- Use first successful response
- Cancel remaining requests

### 5. Traffic Shadowing
- Mirror requests to shadow cluster
- Fire-and-forget (responses ignored)
- Percentage-based sampling

### 6. Stats and Observability
- Per-cluster stats
- Per-upstream stats
- Latency histograms
- Retry counters

## Statistics

The router filter emits extensive statistics:

| Stat | Type | Description |
|------|------|-------------|
| upstream_rq_total | Counter | Total requests |
| upstream_rq_2xx | Counter | 2xx responses |
| upstream_rq_5xx | Counter | 5xx responses |
| upstream_rq_time | Histogram | Request latency |
| upstream_rq_retry | Counter | Retry attempts |
| upstream_rq_retry_success | Counter | Successful retries |
| upstream_cx_total | Counter | Total connections |
| upstream_cx_active | Gauge | Active connections |

## Common Use Cases

### 1. Basic Routing
Route requests to different clusters based on path or headers.

### 2. Canary Deployments
Route a percentage of traffic to a new version using weighted clusters.

### 3. Circuit Breaking
Automatically stop sending traffic to unhealthy hosts.

### 4. A/B Testing
Route traffic based on user attributes or cookies.

### 5. Traffic Mirroring
Test new services with production traffic without affecting users.

## Best Practices

1. **Always place router filter last** - It terminates the filter chain
2. **Configure appropriate timeouts** - Prevent cascading failures
3. **Use retry budgets** - Avoid retry storms
4. **Enable connection pooling** - Reduce latency and resource usage
5. **Monitor router stats** - Track success rates and latencies
6. **Use hedging carefully** - Can increase upstream load
7. **Configure circuit breakers** - Protect upstream services

## Related Filters

- **ext_authz**: Authentication/authorization before routing
- **ratelimit**: Rate limiting before routing
- **fault**: Inject faults for testing
- **buffer**: Buffer entire request before routing

## References

- [Envoy Router Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/router_filter)
- [Route Configuration](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/route/v3/route_components.proto)
