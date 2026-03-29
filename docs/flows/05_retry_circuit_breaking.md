# Retry Logic and Circuit Breaking Flow

## Overview

This document details Envoy's retry mechanisms and circuit breaking patterns, showing how Envoy handles failures, prevents cascading failures, and implements resilience patterns.

## Retry Decision Flow

```mermaid
flowchart TD
    A[Request Failed] --> B{Retry Policy<br/>Configured?}
    B -->|No| Z[Return Error]
    B -->|Yes| C{Retry Conditions<br/>Match?}

    C -->|No| Z
    C -->|Yes| D{Max Retries<br/>Exceeded?}

    D -->|Yes| Z
    D -->|No| E{Retry Budget<br/>Available?}

    E -->|No| Z
    E -->|Yes| F{Timeout<br/>Remaining?}

    F -->|No| Z
    F -->|Yes| G[Apply Retry Backoff]

    G --> H[Select Host for Retry]
    H --> I{Same Host<br/>Allowed?}

    I -->|No| J[Select Different Host]
    I -->|Yes| K[May use same host]

    J --> L[Mark Previous Host]
    K --> L

    L --> M[Initiate Retry Request]
    M --> N{Retry Result?}

    N -->|Success| O[Return Success]
    N -->|Failure| P{Can Retry<br/>Again?}

    P -->|Yes| D
    P -->|No| Z
```

## Complete Retry Sequence

```mermaid
sequenceDiagram
    participant Client
    participant Router
    participant RetryState
    participant LB as Load Balancer
    participant Host1 as Upstream Host 1
    participant Host2 as Upstream Host 2

    Client->>Router: Request
    Router->>Router: Check retry policy
    Router->>LB: Select host (attempt 1)
    LB-->>Router: Host1

    Router->>Host1: Send request
    Host1-->>Router: 503 Service Unavailable

    Router->>RetryState: Check if retriable
    RetryState->>RetryState: Match failure: 5xx
    RetryState-->>Router: Retriable

    Router->>RetryState: Check retry budget
    RetryState-->>Router: Budget available

    Router->>RetryState: Apply backoff (25ms)
    Note over Router: Wait 25ms

    Router->>LB: Select host (attempt 2, exclude Host1)
    LB-->>Router: Host2

    Router->>Host2: Send request
    Host2-->>Router: 200 OK

    Router->>Client: 200 OK (retry succeeded)
    Router->>RetryState: Update stats
```

## Retry Policy Configuration

```mermaid
classDiagram
    class RetryPolicy {
        +uint32_t num_retries_
        +Duration per_try_timeout_
        +vector~RetryOn~ retry_on_
        +RetryBackOff backoff_
        +RetryPriority priority_
        +RetryHostPredicate host_predicate_
        +RetryBudget budget_
    }

    class RetryOn {
        <<enumeration>>
        5xx
        gateway_error
        reset
        connect_failure
        retriable_4xx
        refused_stream
        retriable_status_codes
        retriable_headers
    }

    class RetryBackOff {
        +Duration base_interval_
        +Duration max_interval_
        +calculateBackoff() Duration
    }

    class RetryBudget {
        +double budget_percent_
        +uint32_t min_retry_concurrency_
        +canRetry() bool
    }

    class RetryHostPredicate {
        +shouldSelectAnotherHost() bool
        +onHostAttempted() void
    }

    RetryPolicy --> RetryOn
    RetryPolicy --> RetryBackOff
    RetryPolicy --> RetryBudget
    RetryPolicy --> RetryHostPredicate
```

## Retry Conditions

```mermaid
flowchart TD
    A[Response Received] --> B{Check Retry<br/>Conditions}

    B --> C{5xx?}
    C -->|Yes| D[5xx condition]
    D --> E{In retry_on?}
    E -->|Yes| F[Retriable]

    B --> G{Gateway Error?}
    G -->|Yes| H[502, 503, 504]
    H --> E

    B --> I{Connect Failure?}
    I -->|Yes| J[Connection failed]
    J --> E

    B --> K{Reset?}
    K -->|Yes| L[Stream reset]
    L --> E

    B --> M{Refused Stream?}
    M -->|Yes| N[HTTP/2 REFUSED_STREAM]
    N --> E

    B --> O{Retriable 4xx?}
    O -->|Yes| P[409 Conflict]
    P --> E

    B --> Q{Status Code Match?}
    Q -->|Yes| R[Custom status codes]
    R --> E

    E -->|No| S[Not Retriable]
    F --> T[Attempt Retry]
    S --> U[Return Error]
```

## Retry Backoff Algorithm

```mermaid
sequenceDiagram
    participant Router
    participant Backoff

    Note over Router: Initial attempt failed

    Router->>Backoff: Calculate backoff (attempt 1)
    Backoff->>Backoff: backoff = base_interval * (2^0)<br/>= 25ms
    Backoff-->>Router: 25ms

    Note over Router: Wait 25ms, retry fails

    Router->>Backoff: Calculate backoff (attempt 2)
    Backoff->>Backoff: backoff = base_interval * (2^1)<br/>= 50ms
    Backoff->>Backoff: Add jitter: ±25%
    Backoff-->>Router: 43ms (with jitter)

    Note over Router: Wait 43ms, retry fails

    Router->>Backoff: Calculate backoff (attempt 3)
    Backoff->>Backoff: backoff = base_interval * (2^2)<br/>= 100ms
    Backoff->>Backoff: Check max_interval: 100ms
    Backoff->>Backoff: Add jitter
    Backoff-->>Router: 87ms

    Note over Router: Wait 87ms, retry

    Note over Backoff: Exponential backoff with jitter<br/>prevents thundering herd
```

## Retry Budget Mechanism

```mermaid
flowchart TD
    A[Request Failed] --> B[Check Retry Budget]
    B --> C[Calculate Concurrent Retries]

    C --> D{concurrent_retries /<br/>active_requests}

    D --> E{< budget_percent?}
    E -->|Yes| F{>= min_retry_concurrency?}
    F -->|Yes| G[Allow Retry]
    F -->|No| H{active_requests high<br/>enough?}
    H -->|Yes| I[Deny Retry]
    H -->|No| G

    E -->|No| I

    G --> J[Increment retry counter]
    J --> K[Attempt Retry]

    I --> L[Deny Retry]
    L --> M[Prevent Retry Storm]

    subgraph "Example Calculation"
        N[active_requests: 100]
        O[concurrent_retries: 8]
        P[budget_percent: 10%]
        Q[Ratio: 8/100 = 8%]
        R[8% < 10%: Allow Retry]
    end
```

## Host Selection for Retry

```mermaid
sequenceDiagram
    participant Router
    participant RetryState
    participant LB as Load Balancer
    participant HostSet

    Router->>RetryState: Need retry
    RetryState->>RetryState: Get attempted hosts<br/>[Host1, Host2]

    RetryState->>LB: selectHost(exclude_hosts)
    LB->>HostSet: Get available hosts
    HostSet-->>LB: [Host1, Host2, Host3, Host4]

    LB->>LB: Remove attempted hosts<br/>[Host3, Host4]

    alt Different host available
        LB->>LB: Apply load balancing<br/>to remaining hosts
        LB-->>Router: Host3
    else No different host
        alt retry_host_predicate allows
            LB-->>Router: Host1 (reuse)
        else strict retry
            LB-->>Router: No host available
            Router->>Router: Fail request
        end
    end
```

## Circuit Breaker Architecture

```mermaid
classDiagram
    class CircuitBreaker {
        +uint32_t max_connections_
        +uint32_t max_pending_requests_
        +uint32_t max_requests_
        +uint32_t max_retries_
        +uint32_t max_connection_pools_
        +ResourceManager resource_manager_
        +checkResource() bool
    }

    class ResourceManager {
        +AtomicCounter connections_
        +AtomicCounter pending_requests_
        +AtomicCounter requests_
        +AtomicCounter retries_
        +requestResource() bool
        +releaseResource() void
    }

    class ThresholdManager {
        +Threshold max_
        +AtomicCounter current_
        +canAllocate() bool
    }

    CircuitBreaker --> ResourceManager
    ResourceManager --> ThresholdManager
```

## Circuit Breaker Decision Flow

```mermaid
flowchart TD
    A[New Request] --> B[Check Circuit Breaker]

    B --> C{Max Connections<br/>Check}
    C -->|current >= max| D[Reject: Too many connections]
    C -->|current < max| E{Max Pending<br/>Requests Check}

    E -->|current >= max| F[Reject: Too many pending]
    E -->|current < max| G{Max Requests<br/>Check}

    G -->|current >= max| H[Reject: Too many requests]
    G -->|current < max| I{Max Retries<br/>Check}

    I -->|current >= max| J[Reject: Too many retries]
    I -->|current < max| K[Allow Request]

    K --> L[Increment Counters]
    L --> M[Process Request]
    M --> N[Decrement Counters]

    D --> O[503 Service Unavailable]
    F --> O
    H --> O
    J --> O

    O --> P[Update CB Stats]
```

## Circuit Breaker State Tracking

```mermaid
stateDiagram-v2
    [*] --> Closed: Normal operation

    state Closed {
        [*] --> MonitoringFailures
        MonitoringFailures --> MonitoringFailures: Track requests
    }

    Closed --> Open: Threshold exceeded

    state Open {
        [*] --> RejectingRequests
        RejectingRequests --> RejectingRequests: Reject immediately
        RejectingRequests --> WaitingTimeout: Timeout
    }

    Open --> HalfOpen: Timeout elapsed

    state HalfOpen {
        [*] --> TestingRecovery
        TestingRecovery --> TestingRecovery: Allow limited requests
    }

    HalfOpen --> Closed: Success threshold met
    HalfOpen --> Open: Failure detected

    note right of Open
        Fast fail mode
        Reduce load on struggling service
    end note

    note right of HalfOpen
        Limited traffic allowed
        Test if service recovered
    end note
```

## Per-Try Timeout vs Global Timeout

```mermaid
sequenceDiagram
    participant Client
    participant Router
    participant Attempt1 as Attempt 1
    participant Attempt2 as Attempt 2
    participant Attempt3 as Attempt 3

    Client->>Router: Request<br/>Global timeout: 3s

    Note over Router: Start global timer

    Router->>Attempt1: Try 1<br/>Per-try timeout: 1s
    Note over Attempt1: Wait 1s...
    Attempt1-->>Router: Timeout

    Router->>Router: Retry (0.5s elapsed)

    Router->>Attempt2: Try 2<br/>Per-try timeout: 1s
    Note over Attempt2: Wait 1s...
    Attempt2-->>Router: Timeout

    Router->>Router: Retry (2s elapsed)

    Router->>Attempt3: Try 3<br/>Per-try timeout: 1s
    Note over Attempt3: Wait 0.5s...

    alt Within Global Timeout
        Attempt3-->>Router: Success (2.5s total)
        Router->>Client: 200 OK
    else Global Timeout
        Note over Router: 3s elapsed
        Router->>Attempt3: Cancel
        Router->>Client: 504 Gateway Timeout
    end
```

## Hedged Requests (Parallel Retries)

```mermaid
sequenceDiagram
    participant Client
    participant Router
    participant Upstream1
    participant Upstream2
    participant Upstream3

    Client->>Router: Request

    Router->>Upstream1: Primary request
    Note over Router: Start hedge timer

    Note over Router: Hedge delay elapsed (50ms)

    Router->>Upstream2: Hedge request 1
    Note over Router: Another hedge delay

    Router->>Upstream3: Hedge request 2

    Note over Upstream1,Upstream3: Racing for response

    Upstream2-->>Router: 200 OK (first!)
    Router->>Router: Cancel other requests
    Router->>Upstream1: RST_STREAM
    Router->>Upstream3: RST_STREAM

    Router->>Client: 200 OK (from Upstream2)

    Note over Router: Hedging: Lower p99 latency<br/>at cost of increased load
```

## Retry Plugin Architecture

```mermaid
classDiagram
    class RetryPolicy {
        +shouldRetry() bool
    }

    class RetryPriority {
        <<interface>>
        +determinePriorityLoad() uint32_t
        +onHostAttempted() void
    }

    class PreviousPrioritiesRetryPriority {
        +attempted_priorities_
        +determinePriorityLoad() uint32_t
    }

    class RetryHostPredicate {
        <<interface>>
        +shouldSelectAnotherHost() bool
        +onHostAttempted() void
    }

    class PreviousHostsRetryPredicate {
        +attempted_hosts_
        +shouldSelectAnotherHost() bool
    }

    class CanaryRetryPredicate {
        +shouldSelectAnotherHost() bool
    }

    RetryPolicy --> RetryPriority
    RetryPolicy --> RetryHostPredicate
    RetryPriority <|-- PreviousPrioritiesRetryPriority
    RetryHostPredicate <|-- PreviousHostsRetryPredicate
    RetryHostPredicate <|-- CanaryRetryPredicate
```

## Retry Configuration Examples

### Basic Retry

```yaml
route_config:
  virtual_hosts:
    - name: backend
      domains: ["*"]
      routes:
        - match: { prefix: "/" }
          route:
            cluster: backend
            retry_policy:
              retry_on: "5xx,reset,connect-failure"
              num_retries: 3
              per_try_timeout: 2s
```

### Advanced Retry with Backoff

```yaml
retry_policy:
  retry_on: "5xx,gateway-error,reset,connect-failure,refused-stream"
  num_retries: 5
  per_try_timeout: 1s
  retry_back_off:
    base_interval: 0.1s
    max_interval: 1s
  retry_host_predicate:
    - name: envoy.retry_host_predicates.previous_hosts
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.retry.host.previous_hosts.v3.PreviousHostsPredicate
  host_selection_retry_max_attempts: 5
```

### Retry Budget

```yaml
retry_policy:
  num_retries: 3
  retry_on: "5xx"
  retry_budget:
    budget_percent: 20.0       # Max 20% of requests can be retries
    min_retry_concurrency: 5   # Always allow at least 5 concurrent retries
```

### Hedged Request

```yaml
route:
  cluster: backend
  hedge_policy:
    initial_requests: 1
    additional_request_chance:
      numerator: 50
      denominator: HUNDRED
    hedge_on_per_try_timeout: true
```

## Circuit Breaker Configuration

```yaml
clusters:
  - name: backend
    type: STRICT_DNS
    lb_policy: ROUND_ROBIN
    circuit_breakers:
      thresholds:
        - priority: DEFAULT
          max_connections: 1000
          max_pending_requests: 1000
          max_requests: 1000
          max_retries: 3
          track_remaining: true
        - priority: HIGH
          max_connections: 2000
          max_pending_requests: 2000
          max_requests: 2000
          max_retries: 5
```

## Statistics

```yaml
# Retry stats
cluster.backend.upstream_rq_retry                  # Total retries
cluster.backend.upstream_rq_retry_success          # Successful retries
cluster.backend.upstream_rq_retry_overflow         # Retries denied by budget

# Circuit breaker stats
cluster.backend.circuit_breakers.default.cx_open           # Connections open
cluster.backend.circuit_breakers.default.cx_pool_full      # Connection pool full
cluster.backend.circuit_breakers.default.rq_pending_open   # Pending requests
cluster.backend.circuit_breakers.default.rq_open           # Active requests
cluster.backend.circuit_breakers.default.rq_retry_open     # Active retries
cluster.backend.circuit_breakers.default.remaining_cx      # Remaining connection slots
cluster.backend.circuit_breakers.default.remaining_rq      # Remaining request slots
```

## Retry vs Circuit Breaker Decision Tree

```mermaid
flowchart TD
    A[Request Failed] --> B{Circuit Breaker<br/>Open?}

    B -->|Yes| C[Immediate Failure<br/>503 Service Unavailable]
    B -->|No| D{Retry Policy<br/>Configured?}

    D -->|No| E[Return Error]
    D -->|Yes| F{Retriable<br/>Condition?}

    F -->|No| E
    F -->|Yes| G{Circuit Breaker<br/>Allow Retry?}

    G -->|No| H[CB: Max retries reached<br/>503]
    G -->|Yes| I{Retry Budget<br/>OK?}

    I -->|No| J[Budget exhausted<br/>Return error]
    I -->|Yes| K[Attempt Retry]

    K --> L{Retry Success?}
    L -->|Yes| M[Return Success]
    L -->|No| N{More Retries<br/>Allowed?}

    N -->|Yes| G
    N -->|No| O[Max retries exceeded<br/>Return error]
```

## Best Practices

### Retry Configuration
1. **Set reasonable num_retries**: Typically 2-3
2. **Configure per_try_timeout**: Should be less than global timeout
3. **Use retry budgets**: Prevent retry storms (20% is typical)
4. **Enable backoff**: Exponential backoff with jitter
5. **Match failure types**: Only retry on retriable errors

### Circuit Breaker Settings
1. **Set realistic limits**: Based on service capacity
2. **Monitor metrics**: Track CB openings
3. **Test under load**: Verify CB triggers appropriately
4. **Use priority levels**: Different limits for different traffic
5. **Enable tracking**: Use `track_remaining: true`

### Combined Strategy
1. **Circuit breakers first**: Prevent overwhelming services
2. **Retry after CB check**: Only if CB allows
3. **Budget limits**: Prevent retry amplification
4. **Monitor both**: Track retry and CB stats
5. **Test failure scenarios**: Verify resilience

## Key Takeaways

### Retry Mechanism
- **Automatic retry** on retriable failures
- **Exponential backoff** with jitter
- **Retry budgets** prevent storms
- **Host selection** avoids bad hosts
- **Per-try timeouts** bound attempt duration

### Circuit Breaking
- **Resource limits** protect services
- **Fast fail** when limits reached
- **Multiple thresholds** for different resources
- **Priority-based** limits
- **Metrics tracking** for observability

### Resilience Pattern
1. **Circuit breaker** prevents overload
2. **Retry logic** handles transient failures
3. **Timeouts** bound waiting time
4. **Budgets** prevent amplification
5. **Health checks** detect problems early

## Related Flows
- [Cluster Management](03_cluster_load_balancing.md)
- [HTTP Request Flow](02_http_request_flow.md)
- [Health Checking](08_health_checking.md)
- [Upstream Connection Management](06_upstream_connection_management.md)
