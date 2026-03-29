# Fault Injection Filter

## Overview

The Fault Injection filter is used to inject faults (delays and aborts) into HTTP requests for testing the resilience and reliability of microservices. It enables chaos engineering practices by simulating network issues, service failures, and latency problems without modifying application code.

## Key Responsibilities

- Inject delays to simulate network latency
- Abort requests to simulate service failures
- Support percentage-based fault injection
- Enable per-route fault configuration
- Support header-based fault activation
- Rate limit fault injection
- Inject faults based on downstream clusters

## Architecture

```mermaid
classDiagram
    class FaultFilter {
        +FaultConfig config_
        +Runtime runtime_
        +decodeHeaders() FilterHeadersStatus
        +decodeData() FilterDataStatus
        +maybeDelay() bool
        +maybeAbort() bool
    }

    class FaultConfig {
        +AbortConfig abort_
        +DelayConfig delay_
        +uint64_t max_active_faults_
        +string upstream_cluster_
    }

    class AbortConfig {
        +uint32_t percentage_
        +uint32_t http_status_
    }

    class DelayConfig {
        +uint32_t percentage_
        +Duration fixed_delay_
        +HeaderDelay header_delay_
    }

    FaultFilter --> FaultConfig
    FaultConfig --> AbortConfig
    FaultConfig --> DelayConfig
```

## Request Flow - Delay Injection

```mermaid
sequenceDiagram
    participant Client
    participant FM as Filter Manager
    participant Fault as Fault Filter
    participant Router
    participant Upstream

    Client->>FM: HTTP Request
    FM->>Fault: decodeHeaders(headers, end_stream)

    Fault->>Fault: Check if delay should be injected
    Note over Fault: Check percentage,<br/>headers, runtime

    alt Inject Delay
        Fault->>Fault: Start delay timer (e.g., 5 seconds)
        Note over Fault: Return StopIteration<br/>Request buffered

        Note over Fault: Wait for delay duration...

        Fault->>FM: Continue after delay
        FM->>Router: decodeHeaders() -- after 5s
        Router->>Upstream: Forward request
        Upstream-->>Router: Response
        Router-->>Client: Response (delayed)

    else No Delay
        Fault->>FM: Continue
        FM->>Router: decodeHeaders()
        Router->>Upstream: Forward request
        Upstream-->>Router: Response
        Router-->>Client: Response (normal)
    end
```

## Request Flow - Abort Injection

```mermaid
sequenceDiagram
    participant Client
    participant FM as Filter Manager
    participant Fault as Fault Filter
    participant Router
    participant Upstream

    Client->>FM: HTTP Request
    FM->>Fault: decodeHeaders(headers, end_stream)

    Fault->>Fault: Check if abort should be injected
    Note over Fault: Check percentage,<br/>headers, runtime

    alt Inject Abort
        Fault->>FM: sendLocalReply(503, "fault filter abort")
        FM->>Client: 503 Service Unavailable
        Note over Router,Upstream: Request never reaches upstream

    else No Abort
        Fault->>FM: Continue
        FM->>Router: decodeHeaders()
        Router->>Upstream: Forward request
        Upstream-->>Router: Response
        Router-->>Client: Response
    end
```

## Fault Decision Flow

```mermaid
flowchart TD
    A[Request Arrives] --> B{Fault Filter<br/>Enabled?}
    B -->|No| Z[Continue - No Fault]
    B -->|Yes| C{Check Upstream<br/>Cluster Match}

    C -->|Doesn't Match| Z
    C -->|Matches| D{Check Active<br/>Faults Limit}
    D -->|Limit Reached| Z
    D -->|Under Limit| E{Check Abort<br/>Configuration}

    E -->|Configured| F{Abort Percentage<br/>Check}
    F -->|Random % < Threshold| G{Header Override?}
    F -->|Random % >= Threshold| H{Check Delay<br/>Configuration}

    G -->|x-envoy-fault-abort<br/>present| I[Inject Abort]
    G -->|No Header| H

    H -->|Configured| J{Delay Percentage<br/>Check}
    J -->|Random % < Threshold| K{Header Override?}
    J -->|Random % >= Threshold| Z

    K -->|x-envoy-fault-delay<br/>present| L[Inject Delay]
    K -->|No Header| Z

    E -->|Not Configured| H

    I --> M[Return Error Response]
    L --> N[Start Timer]
    N --> O[Wait for Duration]
    O --> P[Continue Request]
```

## Delay Types

```mermaid
flowchart TD
    A[Delay Configuration] --> B{Delay Type?}

    B -->|Fixed Delay| C[fixed_delay: 5s]
    C --> C1[Same delay for all requests]

    B -->|Header Delay| D[header_delay]
    D --> D1[Read from x-envoy-fault-delay-request]
    D1 --> D2{Header Present?}
    D2 -->|Yes| D3[Use header value]
    D2 -->|No| D4[Use default or skip]

    B -->|Rate Limited| E[rate_limit percentage]
    E --> E1[Apply delay only to<br/>percentage of requests]

    C1 --> F[Apply Delay]
    D3 --> F
    D4 --> G[No Delay]
    E1 --> F
```

## Percentage-Based Activation

```mermaid
sequenceDiagram
    participant Request1
    participant Request2
    participant Request3
    participant Fault as Fault Filter
    participant Random

    Note over Fault: Config: 30% abort rate

    Request1->>Fault: Incoming request
    Fault->>Random: Generate random number (0-100)
    Random-->>Fault: 25
    Note over Fault: 25 < 30, inject fault
    Fault-->>Request1: 503 Abort

    Request2->>Fault: Incoming request
    Fault->>Random: Generate random number (0-100)
    Random-->>Fault: 55
    Note over Fault: 55 >= 30, no fault
    Fault-->>Request2: Continue normally

    Request3->>Fault: Incoming request
    Fault->>Random: Generate random number (0-100)
    Random-->>Fault: 15
    Note over Fault: 15 < 30, inject fault
    Fault-->>Request3: 503 Abort
```

## Configuration Example - Basic

```yaml
name: envoy.filters.http.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  # Abort 10% of requests with 503
  abort:
    http_status: 503
    percentage:
      numerator: 10
      denominator: HUNDRED

  # Add 5 second delay to 20% of requests
  delay:
    fixed_delay: 5s
    percentage:
      numerator: 20
      denominator: HUNDRED
```

## Configuration Example - Advanced

```yaml
name: envoy.filters.http.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault

  # Delay configuration
  delay:
    # 3 second fixed delay
    fixed_delay: 3s
    # Apply to 25% of requests
    percentage:
      numerator: 25
      denominator: HUNDRED
    # Allow header override
    header_delay: {}

  # Abort configuration
  abort:
    # Return 503 Service Unavailable
    http_status: 503
    # Apply to 5% of requests
    percentage:
      numerator: 5
      denominator: HUNDRED
    # Allow header override
    header_abort: {}

  # Only inject faults for specific upstream cluster
  upstream_cluster: "backend_service"

  # Limit concurrent active faults
  max_active_faults: 100

  # Enable via runtime
  delay_percent_runtime: "fault.delay.fixed_delay_percent"
  abort_percent_runtime: "fault.abort.abort_percent"

  # Response rate limiting
  response_rate_limit:
    fixed_limit:
      limit_kbps: 10
    percentage:
      numerator: 50
      denominator: HUNDRED
```

## Route-Level Configuration

```yaml
routes:
  - match:
      prefix: "/api/v1"
    route:
      cluster: api_v1_cluster
    typed_per_filter_config:
      envoy.filters.http.fault:
        "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
        delay:
          fixed_delay: 2s
          percentage:
            numerator: 50
            denominator: HUNDRED

  - match:
      prefix: "/api/v2"
    route:
      cluster: api_v2_cluster
    typed_per_filter_config:
      envoy.filters.http.fault:
        "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
        abort:
          http_status: 429
          percentage:
            numerator: 10
            denominator: HUNDRED
```

## Header-Based Fault Injection

```mermaid
sequenceDiagram
    participant Client
    participant Fault as Fault Filter
    participant Upstream

    Note over Client: Testing with explicit fault injection

    Client->>Fault: Request + x-envoy-fault-delay-request: 10000
    Note over Fault: Header value in milliseconds

    Fault->>Fault: Override config delay with 10s
    Note over Fault: Wait 10 seconds...

    Fault->>Upstream: Forward request after delay
    Upstream-->>Fault: Response
    Fault-->>Client: Response (10s delayed)

    Note over Client: Testing abort

    Client->>Fault: Request + x-envoy-fault-abort-request: 503
    Fault->>Fault: Abort with status 503
    Fault-->>Client: 503 Service Unavailable
    Note over Upstream: Request never sent
```

## Upstream Cluster Filtering

```mermaid
flowchart TD
    A[Request] --> B[Fault Filter]
    B --> C{upstream_cluster<br/>configured?}

    C -->|No| D[Apply to All Routes]
    D --> E[Check Fault Conditions]

    C -->|Yes| F{Route's cluster<br/>matches?}
    F -->|No| G[Skip Fault Injection]
    F -->|Yes| E

    E --> H{Should Inject?}
    H -->|Yes| I[Inject Fault]
    H -->|No| G

    I --> J[Delay or Abort]
    G --> K[Continue Normally]
```

## Statistics

```mermaid
graph LR
    A[Fault Filter Stats] --> B[fault.request_count]
    A --> C[fault.active_faults]
    A --> D[fault.delays_injected]
    A --> E[fault.aborts_injected]
    A --> F[fault.response_rl_injected]

    B --> B1[Total requests processed]
    C --> C1[Current active faults]
    D --> D1[Total delays injected]
    E --> E1[Total aborts injected]
    F --> F1[Response rate limits applied]
```

## Configuration Example - Chaos Testing Scenarios

```yaml
# Scenario 1: Simulate Slow Backend (High Latency)
name: envoy.filters.http.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  delay:
    fixed_delay: 10s
    percentage:
      numerator: 100  # All requests
      denominator: HUNDRED

---
# Scenario 2: Simulate Service Failures
name: envoy.filters.http.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  abort:
    http_status: 503
    percentage:
      numerator: 50  # 50% failure rate
      denominator: HUNDRED

---
# Scenario 3: Simulate Timeout Scenarios
name: envoy.filters.http.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  delay:
    fixed_delay: 31s  # Longer than typical timeout
    percentage:
      numerator: 20
      denominator: HUNDRED

---
# Scenario 4: Simulate Rate Limiting
name: envoy.filters.http.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  abort:
    http_status: 429
    percentage:
      numerator: 30
      denominator: HUNDRED

---
# Scenario 5: Simulate Bandwidth Throttling
name: envoy.filters.http.fault
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.fault.v3.HTTPFault
  response_rate_limit:
    fixed_limit:
      limit_kbps: 50  # Limit to 50 KB/s
    percentage:
      numerator: 100
      denominator: HUNDRED
```

## Runtime Configuration

```yaml
# Enable/disable faults dynamically without config changes
runtime:
  layers:
    - name: admin
      admin_layer: {}
    - name: static
      static_layer:
        # Override delay percentage
        fault.delay.fixed_delay_percent: 30

        # Override abort percentage
        fault.abort.abort_percent: 10

        # Disable faults completely
        fault.http.enabled: false
```

## Key Features

### 1. Multiple Fault Types
- Fixed delays
- HTTP aborts with custom status codes
- Response bandwidth throttling

### 2. Flexible Activation
- Percentage-based injection
- Header-based override
- Runtime dynamic configuration
- Per-route configuration

### 3. Targeting
- Upstream cluster filtering
- Route-specific faults
- Active fault limiting

### 4. Testing Capabilities
- Chaos engineering support
- Resilience testing
- Timeout validation
- Circuit breaker testing

## Common Use Cases

### 1. Chaos Engineering
Test system resilience by injecting random faults

### 2. Timeout Testing
Validate timeout configurations and handling

### 3. Retry Logic Testing
Verify retry policies work correctly

### 4. Circuit Breaker Testing
Trigger circuit breakers to test behavior

### 5. Load Testing
Simulate degraded backend performance

### 6. Failover Testing
Test service failover mechanisms

### 7. Client Resilience
Test client retry and error handling

## Best Practices

1. **Start with low percentages** - 1-5% in production
2. **Use in staging first** - Test fault configs safely
3. **Enable header overrides** - For controlled testing
4. **Monitor impact** - Track error rates and latency
5. **Use runtime config** - Enable/disable without redeploy
6. **Target specific clusters** - Avoid widespread impact
7. **Limit active faults** - Prevent resource exhaustion
8. **Document test scenarios** - Track what you're testing
9. **Combine with observability** - Monitor fault impact
10. **Use timeboxed experiments** - Don't leave faults enabled

## Testing Patterns

```mermaid
flowchart TD
    A[Chaos Testing Pattern] --> B[Baseline Measurement]
    B --> C[Inject Small Fault<br/>1-5%]
    C --> D[Monitor Metrics]
    D --> E{System<br/>Healthy?}
    E -->|Yes| F[Increase Percentage]
    E -->|No| G[Identify Issues]
    F --> H{Target %<br/>Reached?}
    H -->|No| D
    H -->|Yes| I[Document Results]
    G --> J[Fix Issues]
    J --> K[Retest]
    K --> D
    I --> L[Disable Faults]
```

## Safety Considerations

1. **Never 100% in production** - Always allow some success
2. **Set max_active_faults** - Prevent memory issues
3. **Use canary deployments** - Test faults on subset
4. **Monitor downstream impact** - Check cascade effects
5. **Have kill switch** - Quick way to disable faults
6. **Alert on fault injection** - Know when faults are active
7. **Document procedures** - Clear enable/disable process
8. **Test fault configs** - In non-production first

## Debugging with Fault Filter

```bash
# Inject delay for specific request
curl -H "x-envoy-fault-delay-request: 5000" https://api.example.com/test

# Inject abort for specific request
curl -H "x-envoy-fault-abort-request: 503" https://api.example.com/test

# Combine both headers
curl -H "x-envoy-fault-delay-request: 3000" \ -H "x-envoy-fault-abort-request: 500" \ https://api.example.com/test

# Check if fault was injected (look for x-envoy-fault-delay-request header in response)
curl -v https://api.example.com/test
```

## Related Filters

- **router**: Works before router to affect routing
- **ratelimit**: Can be combined for comprehensive testing
- **ext_proc**: Can inject faults based on external logic

## References

- [Envoy Fault Injection Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/fault_filter)
- [Chaos Engineering Principles](https://principlesofchaos.org/)
- [Netflix Chaos Monkey](https://netflix.github.io/chaosmonkey/)
