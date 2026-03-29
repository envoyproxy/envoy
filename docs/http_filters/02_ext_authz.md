# External Authorization (ext_authz) Filter

## Overview

The External Authorization (ext_authz) filter calls an external authorization service to check whether incoming requests are authorized. This filter enables centralized authentication and authorization decisions by delegating to an external service via gRPC or HTTP.

## Key Responsibilities

- Call external authorization service for each request
- Support both gRPC and HTTP protocols
- Handle synchronous and asynchronous authorization
- Inject headers from authorization response
- Support per-route configuration
- Implement failure modes (allow/deny)
- Request body buffering for authorization

## Architecture

```mermaid
classDiagram
    class Filter {
        +FilterConfig config_
        +Client client_
        +State state_
        +decodeHeaders() FilterHeadersStatus
        +decodeData() FilterDataStatus
        +onComplete() void
        +initiateCall() void
        +continueDecoding() void
    }

    class FilterConfig {
        +bool failure_mode_allow_
        +uint32_t max_request_bytes_
        +bool clear_route_cache_
        +Http::Code status_on_error_
        +ExtAuthzFilterStats stats_
    }

    class Client {
        <<interface>>
        +check() void
        +cancel() void
    }

    class GrpcClientImpl {
        +async_client_
        +check() void
        +onSuccess() void
        +onFailure() void
    }

    class HttpClientImpl {
        +cluster_manager_
        +check() void
        +onSuccess() void
        +onFailure() void
    }

    class ExtAuthzLoggingInfo {
        +latency_
        +bytes_sent_
        +bytes_received_
        +cluster_info_
    }

    Filter --> FilterConfig
    Filter --> Client
    Filter --> ExtAuthzLoggingInfo
    Client <|-- GrpcClientImpl
    Client <|-- HttpClientImpl
```

## Request Flow - gRPC Authorization

```mermaid
sequenceDiagram
    participant Client
    participant FM as Filter Manager
    participant ExtAuthz as ExtAuthz Filter
    participant AuthService as Authorization Service
    participant Router as Router Filter

    Client->>FM: HTTP Request
    FM->>ExtAuthz: decodeHeaders(headers, end_stream)

    alt With Request Body
        ExtAuthz->>FM: StopIterationAndBuffer
        FM->>ExtAuthz: decodeData(data, end_stream)
        Note over ExtAuthz: Buffer until max_request_bytes
    end

    ExtAuthz->>ExtAuthz: Build CheckRequest
    ExtAuthz->>AuthService: Check(CheckRequest)

    alt Authorization Approved
        AuthService-->>ExtAuthz: CheckResponse(OK)
        Note over ExtAuthz: status.code = 0

        opt Add Headers from Response
            ExtAuthz->>ExtAuthz: Add/Remove Headers
        end

        opt Clear Route Cache
            ExtAuthz->>FM: clearRouteCache()
        end

        ExtAuthz->>FM: Continue
        FM->>Router: decodeHeaders(modified_headers)
        Router->>Client: Proxied Response

    else Authorization Denied
        AuthService-->>ExtAuthz: CheckResponse(Denied)
        Note over ExtAuthz: status.code = 7 (PermissionDenied)

        ExtAuthz->>ExtAuthz: Extract deny response
        ExtAuthz->>FM: sendLocalReply(403, body, headers)
        FM->>Client: 403 Forbidden

    else Authorization Error
        AuthService-->>ExtAuthz: Error/Timeout

        alt Failure Mode Allow
            Note over ExtAuthz: failure_mode_allow = true
            ExtAuthz->>FM: Continue
            FM->>Router: decodeHeaders()
        else Failure Mode Deny
            Note over ExtAuthz: failure_mode_allow = false
            ExtAuthz->>FM: sendLocalReply(403/500)
            FM->>Client: Error Response
        end
    end
```

## Request Flow - HTTP Authorization

```mermaid
sequenceDiagram
    participant Envoy as Envoy Filter
    participant AuthHTTP as HTTP Auth Service
    participant Upstream

    Envoy->>AuthHTTP: POST /authorize
    Note over Envoy,AuthHTTP: Headers: Original request headers<br/>Body: Request body (optional)

    alt Authorized (200 OK)
        AuthHTTP-->>Envoy: 200 OK
        Note over AuthHTTP: Headers to inject:<br/>x-auth-user: john<br/>x-auth-roles: admin

        Envoy->>Envoy: Inject response headers
        Envoy->>Upstream: Forward with injected headers

    else Denied (401/403)
        AuthHTTP-->>Envoy: 401/403 + Body + Headers
        Envoy->>Envoy: Use auth service response
        Envoy-->>Envoy: 401/403 with custom body

    else Auth Service Error
        AuthHTTP-->>Envoy: 5xx / Timeout
        alt Failure Mode Allow
            Envoy->>Upstream: Continue (Allow)
        else Failure Mode Deny
            Envoy-->>Envoy: 403 Forbidden
        end
    end
```

## State Machine

```mermaid
stateDiagram-v2
    [*] --> NotStarted
    NotStarted --> Calling: initiateCall()
    Calling --> Calling: decodeData (buffering)
    Calling --> Complete: onComplete(OK)
    Calling --> Complete: onComplete(Denied)
    Calling --> Complete: onComplete(Error)
    Complete --> [*]

    note right of Calling
        Filter iteration stopped
        Request buffered if needed
    end note

    note right of Complete
        Either continue or
        send local reply
    end note
```

## Authorization Decision Flow

```mermaid
flowchart TD
    A[Request Arrives] --> B{Filter Enabled<br/>for Route?}
    B -->|No| Z[Skip Check - Continue]
    B -->|Yes| C{With Request<br/>Body?}
    C -->|Yes| D[Buffer Body<br/>up to max_request_bytes]
    C -->|No| E[Build CheckRequest]
    D --> E
    E --> F[Call Auth Service]
    F --> G{Response Type?}

    G -->|OK| H[Extract Headers to Add]
    H --> I{Clear Route<br/>Cache?}
    I -->|Yes| J[Clear Cache]
    I -->|No| K[Continue Decoding]
    J --> K

    G -->|Denied| L[Extract Deny Response]
    L --> M{Custom Response<br/>Configured?}
    M -->|Yes| N[Use Custom Response]
    M -->|No| O[Default 403]
    N --> P[Send Local Reply]
    O --> P

    G -->|Error/Timeout| Q{Failure Mode?}
    Q -->|Allow| K
    Q -->|Deny| R[Send Error Response]

    K --> Z
    P --> ZZ[End - Response Sent]
    R --> ZZ
```

## CheckRequest Structure (gRPC)

```mermaid
classDiagram
    class CheckRequest {
        +AttributeContext attributes
    }

    class AttributeContext {
        +Request request
        +Source source
        +Destination destination
        +ContextExtensions context_extensions
        +MetadataContext metadata_context
    }

    class Request {
        +string id
        +string method
        +map headers
        +string path
        +string host
        +string scheme
        +string query
        +string body
        +int64 size
        +string protocol
    }

    class Source {
        +Address address
        +string principal
        +Certificate certificate
    }

    class Destination {
        +Address address
        +string principal
    }

    CheckRequest --> AttributeContext
    AttributeContext --> Request
    AttributeContext --> Source
    AttributeContext --> Destination
```

## CheckResponse Processing

```mermaid
flowchart TD
    A[Receive CheckResponse] --> B{Status Code?}

    B -->|0 OK| C[Process OK Response]
    C --> D{Headers to Add?}
    D -->|Yes| E[Add Headers to Request]
    D -->|No| F[Check Headers to Remove]
    E --> F
    F --> G{Headers to Remove?}
    G -->|Yes| H[Remove Specified Headers]
    G -->|No| I{Dynamic Metadata?}
    H --> I
    I -->|Yes| J[Inject Dynamic Metadata]
    I -->|No| K[Continue Request]
    J --> K

    B -->|7 PermissionDenied| L[Process Denied Response]
    L --> M{Custom Deny Response?}
    M -->|Yes| N[Extract Status Code]
    M -->|No| O[Use 403 Forbidden]
    N --> P{Custom Headers?}
    O --> P
    P -->|Yes| Q[Add Deny Headers]
    P -->|No| R{Custom Body?}
    Q --> R
    R -->|Yes| S[Use Custom Body]
    R -->|No| T[Default Deny Message]
    S --> U[Send Local Reply]
    T --> U

    B -->|Other| V[Error Case]
    V --> W{Check Failure Mode}
    W -->|Allow| K
    W -->|Deny| X[Send Error Response]
```

## Header Mutation Rules

```mermaid
flowchart TD
    A[Authorization Response] --> B[Headers to Add/Set]
    B --> C{Validate Headers?}
    C -->|Yes| D{Valid Header?}
    C -->|No| E[Skip Validation]
    D -->|No| F[Reject Response]
    D -->|Yes| G{Mutation Rules<br/>Configured?}
    E --> G
    G -->|Yes| H{Mutation Allowed?}
    G -->|No| I[Allow All Mutations]
    H -->|No| J[Skip Header]
    H -->|Yes| K{Mutation Type?}
    I --> K

    K -->|append| L[Append to Existing]
    K -->|add_if_absent| M[Add if Not Present]
    K -->|overwrite| N[Replace Existing]
    K -->|remove| O[Remove Header]

    L --> P[Apply Mutation]
    M --> P
    N --> P
    O --> P
    J --> Q[Log Omitted Header]
    P --> R[Continue]
    Q --> R
    F --> S[Increment Invalid Counter]
```

## Per-Route Configuration

```mermaid
flowchart TD
    A[Request] --> B[Match Route]
    B --> C{Per-Route<br/>Config Exists?}
    C -->|No| D[Use Global Config]
    C -->|Yes| E{Disabled on<br/>Route?}
    E -->|Yes| Z[Skip Authorization]
    E -->|No| F[Merge Configs]
    F --> G{Custom Context<br/>Extensions?}
    G -->|Yes| H[Add to CheckRequest]
    G -->|No| I{Custom Auth<br/>Service?}
    H --> I
    I -->|Yes| J[Create Per-Route Client]
    I -->|No| K[Use Global Client]
    J --> L[Make Auth Call]
    K --> L
    D --> L
    L --> M[Authorization Check]
```

## Configuration Example - gRPC

```yaml
name: envoy.filters.http.ext_authz
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
  grpc_service:
    envoy_grpc:
      cluster_name: ext_authz_cluster
    timeout: 0.5s
  failure_mode_allow: false
  with_request_body:
    max_request_bytes: 8192
    allow_partial_message: true
  clear_route_cache: true
  status_on_error:
    code: 403
  metadata_context_namespaces:
    - envoy.filters.http.jwt_authn
  filter_enabled:
    runtime_key: ext_authz.enabled
    default_value:
      numerator: 100
      denominator: HUNDRED
```

## Configuration Example - HTTP

```yaml
name: envoy.filters.http.ext_authz
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.ext_authz.v3.ExtAuthz
  http_service:
    server_uri:
      uri: "http://auth-service:9191"
      cluster: ext_authz_cluster
      timeout: 0.5s
    authorization_request:
      allowed_headers:
        patterns:
          - exact: "authorization"
          - prefix: "x-"
      headers_to_add:
        - key: "x-envoy-auth"
          value: "true"
    authorization_response:
      allowed_upstream_headers:
        patterns:
          - exact: "x-user-id"
          - prefix: "x-auth-"
  failure_mode_allow: false
```

## Key Features

### 1. Protocol Support
- **gRPC**: Full-featured, bi-directional streaming support
- **HTTP**: RESTful authorization endpoint

### 2. Request Body Buffering
- Buffer request body before authorization
- Configurable maximum size
- Allow partial messages

### 3. Failure Modes
- **Failure Mode Allow**: Continue on auth service failure
- **Failure Mode Deny**: Block on auth service failure

### 4. Header Manipulation
- Add headers from auth response
- Remove specified headers
- Mutation rules validation

### 5. Per-Route Configuration
- Override auth service per route
- Custom context extensions
- Disable authorization on specific routes

### 6. Dynamic Metadata
- Inject metadata from auth response
- Available to downstream filters

## Statistics

| Stat | Type | Description |
|------|------|-------------|
| ext_authz.ok | Counter | Successful authorizations |
| ext_authz.denied | Counter | Denied authorizations |
| ext_authz.error | Counter | Authorization service errors |
| ext_authz.disabled | Counter | Checks skipped (disabled) |
| ext_authz.failure_mode_allowed | Counter | Allowed due to failure mode |

## Common Use Cases

### 1. JWT Validation
Validate JWT tokens with external service

### 2. API Key Authentication
Check API keys against central database

### 3. OAuth2 Token Validation
Validate OAuth2 access tokens

### 4. Role-Based Access Control
External RBAC policy engine

### 5. Request Logging/Auditing
Log all requests to external service

### 6. Content-Based Authorization
Authorize based on request body content

## Best Practices

1. **Set appropriate timeouts** - Balance security and latency
2. **Use failure_mode_allow carefully** - Understand security implications
3. **Limit request body size** - Prevent memory exhaustion
4. **Enable caching** - Reduce auth service load (if stateless)
5. **Monitor auth service performance** - Critical path dependency
6. **Use per-route config** - Disable for public endpoints
7. **Implement retry logic** - In auth service for transient failures
8. **Use connection pooling** - Reuse connections to auth service

## Security Considerations

1. **TLS/mTLS**: Always use encrypted transport to auth service
2. **Timeout handling**: Set reasonable timeouts to prevent DoS
3. **Failure mode**: Carefully choose failure_mode_allow vs deny
4. **Header validation**: Validate headers from auth response
5. **Request body**: Be cautious with max_request_bytes

## Related Filters

- **jwt_authn**: JWT validation (alternative to ext_authz)
- **rbac**: Local RBAC enforcement
- **oauth2**: OAuth2 flow handling

## References

- [Envoy ext_authz Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/ext_authz_filter)
- [External Authorization API](https://www.envoyproxy.io/docs/envoy/latest/api-v3/service/auth/v3/external_auth.proto)
