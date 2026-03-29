# Health Check Filter

## Overview

The Health Check filter responds to health check requests without forwarding them to upstream services. This is useful for load balancer health probes, Kubernetes liveness/readiness probes, and monitoring systems. The filter can respond based on Envoy's health status, cluster health, or custom logic.

## Key Responsibilities

- Intercept health check requests
- Respond without upstream calls
- Check Envoy server health status
- Validate cluster health
- Support pass-through mode
- Cache health check responses
- Provide custom health check endpoints

## Architecture

```mermaid
classDiagram
    class HealthCheckFilter {
        +HealthCheckConfig config_
        +ClusterMinHealthyPercentages cluster_min_healthy_
        +decodeHeaders() FilterHeadersStatus
        +isHealthCheckRequest() bool
        +computeHealthCheckResponse() bool
    }

    class HealthCheckConfig {
        +bool pass_through_mode_
        +Http::HeaderUtility::HeaderDataPtr headers_
        +uint64_t cache_time_ms_
        +string cluster_min_healthy_percentages_
    }

    class ClusterHealthChecker {
        +checkClusterHealth() bool
        +getHealthyPercentage() double
    }

    HealthCheckFilter --> HealthCheckConfig
    HealthCheckFilter --> ClusterHealthChecker
```

## Request Flow - Health Check Intercept

```mermaid
sequenceDiagram
    participant LB as Load Balancer
    participant Envoy as Health Check Filter
    participant Server as Envoy Server
    participant Upstream

    LB->>Envoy: GET /healthz
    Envoy->>Envoy: Match health check path/headers

    alt Is Health Check Request
        Envoy->>Server: Check Envoy server health

        alt Envoy Healthy
            alt Check Clusters
                Envoy->>Envoy: Check cluster health percentages
                alt Clusters Healthy
                    Envoy-->>LB: 200 OK
                    Note over Envoy,LB: Body: "OK"
                else Clusters Unhealthy
                    Envoy-->>LB: 503 Service Unavailable
                    Note over Envoy,LB: Cluster degraded
                end
            else No Cluster Check
                Envoy-->>LB: 200 OK
            end

        else Envoy Unhealthy
            Envoy-->>LB: 503 Service Unavailable
            Note over Envoy,LB: Envoy in draining/failed state
        end

        Note over Upstream: Request never reaches upstream

    else Not Health Check Request
        Envoy->>Upstream: Forward request normally
        Upstream-->>Envoy: Response
        Envoy-->>LB: Response
    end
```

## Request Matching Flow

```mermaid
flowchart TD
    A[Request Arrives] --> B{Match Headers?}
    B -->|No| Z[Not Health Check<br/>Continue to Upstream]
    B -->|Yes| C{pass_through_mode?}

    C -->|true| D{Envoy Healthy?}
    D -->|Yes| E[Add x-envoy-immediate-health-check-fail header]
    E --> Z
    D -->|No| F[Immediate 503 Response]

    C -->|false| G[Intercept Request]
    G --> H{Check Cache}
    H -->|Cache Valid| I[Use Cached Response]
    H -->|Cache Expired| J[Compute Health]

    J --> K{Envoy Server<br/>Healthy?}
    K -->|No| L[Return 503]
    K -->|Yes| M{Cluster Check<br/>Configured?}

    M -->|No| N[Return 200 OK]
    M -->|Yes| O[Check Cluster Health]

    O --> P{Clusters Meet<br/>Threshold?}
    P -->|Yes| N
    P -->|No| L

    I --> Q[Return Cached Response]
    N --> R[Cache Response]
    L --> R
    R --> S[Return to Client]
    Q --> S
```

## Health Status Decision

```mermaid
stateDiagram-v2
    [*] --> Checking
    Checking --> EnvoyHealthCheck: Check Envoy Status
    EnvoyHealthCheck --> Healthy: Server Live & Active
    EnvoyHealthCheck --> Unhealthy: Server Draining/Failed
    Healthy --> ClusterHealthCheck: Cluster Check Enabled
    Healthy --> Return200: No Cluster Check
    ClusterHealthCheck --> CheckPercentages: For Each Cluster
    CheckPercentages --> Return200: All Above Threshold
    CheckPercentages --> Return503: Any Below Threshold
    Unhealthy --> Return503
    Return200 --> [*]: 200 OK
    Return503 --> [*]: 503 Unavailable

    note right of EnvoyHealthCheck
        Check Envoy server state
        - LIVE
        - DRAINING
        - FAILED
    end note

    note right of ClusterHealthCheck
        Check configured clusters
        against min healthy %
    end note
```

## Configuration Example - Basic

```yaml
name: envoy.filters.http.health_check
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
  pass_through_mode: false
  headers:
    - name: ":path"
      string_match:
        exact: "/healthz"
  cache_time: 1s
```

## Configuration Example - Advanced

```yaml
name: envoy.filters.http.health_check
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck

  # Don't forward health checks to upstream
  pass_through_mode: false

  # Match health check requests
  headers:
    - name: ":path"
      string_match:
        exact: "/healthz"
    - name: "user-agent"
      string_match:
        prefix: "health-checker"

  # Cache health check responses for 5 seconds
  cache_time: 5s

  # Require minimum healthy percentages for clusters
  cluster_min_healthy_percentages:
    backend_cluster: 50.0    # 50% of backend must be healthy
    database_cluster: 75.0   # 75% of database must be healthy
    cache_cluster: 25.0      # 25% of cache must be healthy
```

## Multiple Health Check Endpoints

```yaml
routes:
  # Liveness probe - just check if Envoy is alive
  - match:
      path: "/healthz/live"
    direct_response:
      status: 200
      body:
        inline_string: "LIVE"

  # Readiness probe - check if ready to serve traffic
  - match:
      path: "/healthz/ready"
    route:
      cluster: health_check_cluster
    typed_per_filter_config:
      envoy.filters.http.health_check:
        "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
        pass_through_mode: false
        headers:
          - name: ":path"
            string_match:
              exact: "/healthz/ready"
        cluster_min_healthy_percentages:
          backend: 50.0

  # Startup probe - check if application started
  - match:
      path: "/healthz/startup"
    direct_response:
      status: 200
      body:
        inline_string: "STARTED"
```

## Pass-Through Mode

```mermaid
sequenceDiagram
    participant LB as Load Balancer
    participant HC as Health Check Filter
    participant Upstream

    Note over HC: pass_through_mode: true

    LB->>HC: GET /healthz
    HC->>HC: Check Envoy health

    alt Envoy Healthy
        HC->>HC: Add x-envoy-immediate-health-check-fail: false
        HC->>Upstream: Forward request with header
        Upstream-->>HC: Response
        HC-->>LB: Response from upstream

    else Envoy Unhealthy
        HC-->>LB: 503 (immediate)
        Note over Upstream: Request not forwarded
    end
```

## Cluster Health Checking

```mermaid
flowchart TD
    A[Health Check Request] --> B[Get Configured Clusters]
    B --> C{Cluster: backend}

    C --> D[Get Cluster Info]
    D --> E[Count Healthy Hosts]
    E --> F[Count Total Hosts]
    F --> G[Calculate Percentage]
    G --> H{Healthy >= 50%?}

    H -->|Yes| I{Cluster: database}
    H -->|No| Z[Return 503]

    I --> J[Calculate Percentage]
    J --> K{Healthy >= 75%?}
    K -->|Yes| L{Cluster: cache}
    K -->|No| Z

    L --> M[Calculate Percentage]
    M --> N{Healthy >= 25%?}
    N -->|Yes| O[All Checks Pass]
    N -->|No| Z

    O --> P[Return 200 OK]
```

## Caching Behavior

```mermaid
sequenceDiagram
    participant Client1
    participant Client2
    participant HC as Health Check Filter
    participant Cache

    Note over Cache: cache_time: 5s

    Client1->>HC: Health check request @ T=0s
    HC->>HC: Compute health (expensive)
    HC->>Cache: Store result for 5s
    HC-->>Client1: 200 OK

    Client2->>HC: Health check request @ T=2s
    HC->>Cache: Check cache
    Cache-->>HC: Hit (valid for 3s more)
    HC-->>Client2: 200 OK (cached)

    Client1->>HC: Health check request @ T=6s
    HC->>Cache: Check cache
    Cache-->>HC: Miss (expired)
    HC->>HC: Compute health again
    HC->>Cache: Store new result
    HC-->>Client1: 200 OK
```

## Kubernetes Integration

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: envoy-proxy
spec:
  containers:
    - name: envoy
      image: envoyproxy/envoy:latest
      ports:
        - containerPort: 10000
      livenessProbe:
        httpGet:
          path: /healthz/live
          port: 9901  # Admin port
        initialDelaySeconds: 30
        periodSeconds: 10

      readinessProbe:
        httpGet:
          path: /healthz/ready
          port: 10000  # Listener port
        initialDelaySeconds: 5
        periodSeconds: 5

      startupProbe:
        httpGet:
          path: /healthz/startup
          port: 9901
        initialDelaySeconds: 0
        periodSeconds: 5
        failureThreshold: 30
```

## Load Balancer Configuration

```mermaid
graph LR
    A[Load Balancer] --> B[GET /healthz every 5s]
    B --> C{Response?}
    C -->|200 OK| D[Mark Healthy]
    C -->|503| E[Mark Unhealthy]
    C -->|Timeout| E
    D --> F[Route Traffic]
    E --> G[Remove from Pool]
    G -->|Wait 30s| B
```

## Health Check Response Customization

```yaml
name: envoy.filters.http.health_check
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.health_check.v3.HealthCheck
  pass_through_mode: false
  headers:
    - name: ":path"
      string_match:
        exact: "/health"

# Note: Response customization typically done via direct_response route
routes:
  - match:
      path: "/health"
    direct_response:
      status: 200
      body:
        inline_string: |
          {
            "status": "healthy",
            "version": "1.0.0",
            "uptime": "3d 5h 23m"
          }
```

## Common Use Cases

### 1. Load Balancer Health Probes
```yaml
headers:
  - name: ":path"
    string_match:
      exact: "/health"
  - name: "user-agent"
    string_match:
      prefix: "ELB-HealthChecker"
```

### 2. Kubernetes Liveness/Readiness
```yaml
# Different endpoints for different probes
- match: { path: "/healthz/live" }   # Liveness
- match: { path: "/healthz/ready" }  # Readiness
```

### 3. Monitoring Systems
```yaml
headers:
  - name: ":path"
    string_match:
      exact: "/status"
cache_time: 10s  # Reduce load from frequent checks
```

### 4. Canary Deployments
```yaml
# Check if new version is healthy before routing traffic
cluster_min_healthy_percentages:
  canary_cluster: 100.0  # All canary instances must be healthy
```

## Statistics

| Stat | Type | Description |
|------|------|-------------|
| health_check.ok | Counter | Health check requests returned 200 |
| health_check.degraded | Counter | Health check requests returned 503 |

## Best Practices

1. **Use separate endpoints** - Different checks for liveness vs readiness
2. **Set appropriate cache_time** - Balance freshness vs load
3. **Don't forward to upstream** - Use pass_through_mode: false
4. **Check critical clusters only** - Avoid checking all clusters
5. **Set reasonable thresholds** - 50-75% typically sufficient
6. **Monitor health check metrics** - Track degraded responses
7. **Test failure scenarios** - Ensure probes work correctly
8. **Use admin endpoint** - For Envoy-specific health
9. **Document health check paths** - For operations teams
10. **Consider geo-distribution** - Different thresholds per region

## Admin Health Check Endpoint

Envoy provides a built-in health check endpoint on the admin interface:

```bash
# Check Envoy server health
curl http://localhost:9901/healthcheck/ok

# Response: 200 OK if healthy, 503 if not

# Check if Envoy is ready (not draining)
curl http://localhost:9901/ready

# Fail health checks (put in drain mode)
curl -X POST http://localhost:9901/healthcheck/fail

# Pass health checks again
curl -X POST http://localhost:9901/healthcheck/ok
```

## Troubleshooting

```mermaid
flowchart TD
    A[Health Check Issue] --> B{Issue Type?}

    B -->|Always 503| C[Check Envoy Status]
    C --> C1[Is Envoy draining?<br/>Check /server_info]

    B -->|Always 200 but<br/>Clusters Down| D[Check Configuration]
    D --> D1[Verify cluster_min_healthy_percentages<br/>configured]

    B -->|Requests Forwarded<br/>to Upstream| E[Check pass_through_mode]
    E --> E1[Should be false for<br/>health check filter]

    B -->|Not Matching| F[Check Header Match]
    F --> F1[Verify path and<br/>user-agent matching]

    B -->|High Load| G[Check Cache Time]
    G --> G1[Increase cache_time<br/>to reduce checks]
```

## Header Matching Examples

```yaml
# Match by path only
headers:
  - name: ":path"
    string_match:
      exact: "/healthz"

# Match by path and user-agent
headers:
  - name: ":path"
    string_match:
      exact: "/health"
  - name: "user-agent"
    string_match:
      prefix: "kube-probe"

# Match by custom header
headers:
  - name: "x-health-check"
    string_match:
      exact: "true"

# Match multiple paths
headers:
  - name: ":path"
    string_match:
      safe_regex:
        regex: "^/(health|healthz|ping)$"
```

## Comparison: Health Check Methods

| Method | Use Case | Complexity | Upstream Load |
|--------|----------|------------|---------------|
| Health Check Filter | Health probes | Low | None |
| Admin Endpoint | Envoy status | Very Low | None |
| Direct Response | Simple checks | Very Low | None |
| Pass-Through | Upstream health | Medium | High |

## Related Features

- **Admin Interface**: Built-in health endpoints
- **Cluster Health**: Upstream health checking
- **Outlier Detection**: Automatic unhealthy host removal

## References

- [Envoy Health Check Filter Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/health_check_filter)
- [Admin Interface](https://www.envoyproxy.io/docs/envoy/latest/operations/admin)
- [Kubernetes Probes](https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/)
