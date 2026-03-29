# JWT Authentication Filter

## Overview

The JWT Authentication (jwt_authn) filter validates JSON Web Tokens (JWT) in HTTP requests. It verifies the token signature, validates claims, and extracts payload data. This filter integrates with JSON Web Key Sets (JWKS) for public key management and supports multiple JWT providers.

## Key Responsibilities

- Extract JWTs from headers, query parameters, or cookies
- Verify JWT signatures using JWKS
- Validate JWT claims (issuer, audience, expiration)
- Extract and inject JWT payload as metadata
- Support multiple JWT providers
- Cache JWKS for performance
- Handle JWT refresh flows

## Architecture

```mermaid
classDiagram
    class Filter {
        +FilterConfig config_
        +Context context_
        +State state_
        +decodeHeaders() FilterHeadersStatus
        +onComplete() void
        +setExtractedData() void
    }

    class FilterConfig {
        +JwtAuthnFilterStats stats_
        +Matcher matcher_
        +vector~JwtProvider~ providers_
        +createVerifier() Verifier
    }

    class Context {
        +Request request_
        +Verifier verifier_
        +startVerify() void
    }

    class Verifier {
        <<interface>>
        +verify() void
        +onComplete() void
    }

    class JwtProvider {
        +string issuer_
        +vector~string~ audiences_
        +JwksCache jwks_cache_
        +string forward_payload_header_
        +vector~string~ from_headers_
        +vector~string~ from_params_
    }

    class JwksCache {
        +JwksDataPtr jwks_data_
        +fetch() void
        +getJwksData() JwksDataPtr
    }

    class Matcher {
        +matchRequirement() RequirementPtr
    }

    Filter --> FilterConfig
    Filter --> Context
    FilterConfig --> JwtProvider
    FilterConfig --> Matcher
    Context --> Verifier
    JwtProvider --> JwksCache
```

## Request Flow

```mermaid
sequenceDiagram
    participant Client
    participant FM as Filter Manager
    participant JWT as JWT Filter
    participant Matcher
    participant Verifier
    participant JWKS as JWKS Provider
    participant Router

    Client->>FM: Request with JWT
    FM->>JWT: decodeHeaders(headers, end_stream)

    JWT->>Matcher: Match route requirement
    Matcher-->>JWT: Requirement (providers, allow_missing)

    alt No Requirement
        JWT->>FM: Continue
        FM->>Router: Continue processing
    else Requirement Exists
        JWT->>JWT: Extract JWT from headers/params

        alt JWT Not Found
            alt Allow Missing
                JWT->>FM: Continue (no verification)
            else Require JWT
                JWT->>FM: sendLocalReply(401, "JWT not found")
                FM->>Client: 401 Unauthorized
            end

        else JWT Found
            JWT->>Verifier: verify(jwt_token)

            alt JWKS Cached
                Verifier->>Verifier: Verify signature (cached key)
            else JWKS Not Cached
                Verifier->>JWKS: Fetch JWKS
                JWKS-->>Verifier: Public keys
                Verifier->>Verifier: Cache JWKS
                Verifier->>Verifier: Verify signature
            end

            alt Verification Success
                Verifier-->>JWT: onComplete(success, payload)
                JWT->>JWT: Extract claims
                JWT->>FM: Set dynamic metadata
                opt Forward Payload
                    JWT->>JWT: Add payload header
                end
                JWT->>FM: Continue
                FM->>Router: Continue processing
                Router-->>Client: Response

            else Verification Failed
                Verifier-->>JWT: onComplete(failure, error)
                JWT->>FM: sendLocalReply(401, error)
                FM->>Client: 401 Unauthorized
            end
        end
    end
```

## JWT Extraction Flow

```mermaid
flowchart TD
    A[Request Arrives] --> B[Match Route Requirement]
    B --> C{Requirement<br/>Found?}
    C -->|No| Z[Continue - No Auth Required]
    C -->|Yes| D{Providers<br/>Configured?}
    D -->|No| E[Error - Invalid Config]
    D -->|Yes| F[For each provider]

    F --> G{Check from_headers}
    G -->|Found| H[Extract from Header]
    G -->|Not Found| I{Check from_params}
    I -->|Found| J[Extract from Query Param]
    I -->|Not Found| K{Check from_cookies}
    K -->|Found| L[Extract from Cookie]
    K -->|Not Found| M{Allow Missing<br/>JWT?}

    M -->|Yes| Z
    M -->|No| N[401 - JWT Required]

    H --> O[Remove Bearer prefix]
    J --> O
    L --> O
    O --> P[Verify JWT]

    P --> Q{Valid?}
    Q -->|Yes| R[Extract Payload]
    Q -->|No| N

    R --> S[Set Metadata]
    S --> T{Forward Payload<br/>Header?}
    T -->|Yes| U[Add Header with Payload]
    T -->|No| V[Continue]
    U --> V
    V --> Z
```

## JWT Verification Process

```mermaid
flowchart TD
    A[JWT Token] --> B[Parse Header]
    B --> C{Valid Header?}
    C -->|No| Z[Verification Failed]
    C -->|Yes| D[Extract kid and alg]

    D --> E[Parse Payload]
    E --> F{Valid JSON?}
    F -->|No| Z
    F -->|Yes| G[Extract Claims]

    G --> H{Check iss claim}
    H -->|Invalid/Missing| Z
    H -->|Valid| I{Check aud claim}
    I -->|Invalid| Z
    I -->|Valid| J{Check exp claim}
    J -->|Expired| Z
    J -->|Valid| K{Check nbf claim}
    K -->|Not yet valid| Z
    K -->|Valid| L[Get JWKS]

    L --> M{JWKS Cached?}
    M -->|Yes| N[Use Cached JWKS]
    M -->|No| O[Fetch JWKS]
    O --> P{Fetch Success?}
    P -->|No| Z
    P -->|Yes| Q[Cache JWKS]
    Q --> N

    N --> R{Find Key by kid}
    R -->|Not Found| Z
    R -->|Found| S[Extract Public Key]

    S --> T{Algorithm Match?}
    T -->|No| Z
    T -->|Yes| U[Verify Signature]

    U --> V{Signature Valid?}
    V -->|No| Z
    V -->|Yes| W[Verification Success]
```

## JWKS Fetching and Caching

```mermaid
sequenceDiagram
    participant JWT as JWT Filter
    participant Cache as JWKS Cache
    participant Remote as Remote JWKS Endpoint
    participant AsyncClient

    JWT->>Cache: getJwksData(provider)

    alt JWKS Cached and Not Expired
        Cache-->>JWT: Cached JWKS Data
    else JWKS Not Cached or Expired
        Cache->>Cache: Check if fetch in progress
        alt Fetch Not in Progress
            Cache->>AsyncClient: HTTP GET jwks_uri
            AsyncClient->>Remote: GET /.well-known/jwks.json
            Remote-->>AsyncClient: JWKS Response
            AsyncClient-->>Cache: JWKS Data

            alt Valid JWKS
                Cache->>Cache: Parse and Store JWKS
                Cache->>Cache: Set expiration (TTL)
                Cache-->>JWT: Fresh JWKS Data
            else Invalid JWKS
                Cache-->>JWT: Error
            end

        else Fetch Already in Progress
            Cache->>Cache: Queue callback
            Note over Cache: Wait for in-flight request
            Cache-->>JWT: JWKS Data (when available)
        end
    end
```

## State Machine

```mermaid
stateDiagram-v2
    [*] --> Init
    Init --> Calling: startVerify()
    Calling --> Responded: onComplete(success)
    Calling --> Responded: onComplete(failure)
    Responded --> Complete
    Complete --> [*]

    note right of Init
        Initial state
        JWT not yet extracted
    end note

    note right of Calling
        Verification in progress
        Request may be stopped
    end note

    note right of Responded
        Verification complete
        Continue or reject
    end note
```

## Provider Matching

```mermaid
flowchart TD
    A[Incoming Request] --> B[Get Route Config]
    B --> C{Route Requirement<br/>Exists?}
    C -->|No| D[Check Filter-Level<br/>Requirement]
    D --> E{Filter Requirement<br/>Exists?}
    E -->|No| Z[No Auth Required]
    E -->|Yes| F[Use Filter Requirement]
    C -->|Yes| G[Use Route Requirement]

    F --> H[Get Provider Names]
    G --> H

    H --> I{Provider List<br/>Empty?}
    I -->|Yes| J{allow_missing_or_failed?}
    I -->|No| K[For Each Provider]

    K --> L{JWT Extraction<br/>Successful?}
    L -->|Yes| M[Verify with Provider]
    L -->|No| N[Try Next Provider]
    N --> K

    M --> O{Verification<br/>Success?}
    O -->|Yes| P[Success - Continue]
    O -->|No| N

    J -->|Yes| Z
    J -->|No| Q[401 - Auth Required]

    K --> R{All Providers<br/>Tried?}
    R -->|Yes| S{allow_failed?}
    S -->|Yes| Z
    S -->|No| Q

    P --> Z
```

## Configuration Example

```yaml
name: envoy.filters.http.jwt_authn
typed_config:
  "@type": type.googleapis.com/envoy.extensions.filters.http.jwt_authn.v3.JwtAuthentication
  providers:
    provider1:
      issuer: "https://auth.example.com"
      audiences:
        - "api.example.com"
      remote_jwks:
        http_uri:
          uri: "https://auth.example.com/.well-known/jwks.json"
          cluster: auth_cluster
          timeout: 5s
        cache_duration:
          seconds: 300
      from_headers:
        - name: "Authorization"
          value_prefix: "Bearer "
      from_params:
        - "access_token"
      forward_payload_header: "x-jwt-payload"
      payload_in_metadata: "jwt_payload"

    provider2:
      issuer: "https://auth2.example.com"
      audiences:
        - "api.example.com"
      local_jwks:
        inline_string: |
          {
            "keys": [
              {
                "kty": "RSA",
                "kid": "key-1",
                "n": "...",
                "e": "AQAB"
              }
            ]
          }
      from_cookies:
        - "jwt_token"

  rules:
    - match:
        prefix: "/api"
      requires:
        provider_name: "provider1"

    - match:
        prefix: "/admin"
      requires:
        requires_all:
          requirements:
            - provider_name: "provider1"
            - provider_name: "provider2"

    - match:
        prefix: "/public"
      requires:
        allow_missing_or_failed: {}
```

## Claim Validation

```mermaid
classDiagram
    class JWTClaims {
        +string iss
        +vector~string~ aud
        +int64 exp
        +int64 nbf
        +int64 iat
        +string sub
        +string jti
        +map custom_claims
    }

    class ClaimValidator {
        +validateIssuer() bool
        +validateAudience() bool
        +validateExpiration() bool
        +validateNotBefore() bool
    }

    class ProviderConfig {
        +string required_issuer
        +vector~string~ required_audiences
        +Duration clock_skew_seconds
    }

    ClaimValidator --> JWTClaims
    ClaimValidator --> ProviderConfig
```

## Multiple Provider Logic

```mermaid
sequenceDiagram
    participant Request
    participant JWT as JWT Filter
    participant P1 as Provider 1
    participant P2 as Provider 2
    participant P3 as Provider 3

    Request->>JWT: decodeHeaders()
    JWT->>JWT: Match route requirement

    Note over JWT: Requirement: provider1 OR provider2 OR provider3

    JWT->>P1: Try extract & verify
    P1-->>JWT: Failed (JWT not found in P1 location)

    JWT->>P2: Try extract & verify
    P2-->>JWT: Failed (signature invalid)

    JWT->>P3: Try extract & verify
    P3-->>JWT: Success

    JWT->>Request: Continue with P3 claims
```

## Payload Extraction

```mermaid
flowchart TD
    A[JWT Verified] --> B[Extract Payload JSON]
    B --> C{payload_in_metadata<br/>configured?}
    C -->|Yes| D[Store in Dynamic Metadata]
    D --> E[Namespace: provider name]
    E --> F{forward_payload_header<br/>configured?}
    C -->|No| F

    F -->|Yes| G[Base64 encode payload]
    G --> H[Add header with payload]
    H --> I{claim_to_headers<br/>configured?}
    F -->|No| I

    I -->|Yes| J[For each claim mapping]
    J --> K[Extract claim value]
    K --> L[Add as header]
    L --> M{More claims?}
    M -->|Yes| J
    M -->|No| N[Continue Request]
    I -->|No| N
```

## Key Features

### 1. Multiple Extraction Methods
- HTTP Headers (with Bearer prefix support)
- Query parameters
- Cookies

### 2. Multiple Providers
- Support multiple JWT issuers
- Per-route provider selection
- Provider combination logic (AND/OR)

### 3. JWKS Management
- Remote JWKS fetching
- Local/Inline JWKS
- JWKS caching with TTL
- Automatic refresh

### 4. Claim Validation
- Issuer (iss) validation
- Audience (aud) validation
- Expiration (exp) validation
- Not Before (nbf) validation
- Custom claim requirements

### 5. Payload Forwarding
- Forward as header
- Store in dynamic metadata
- Extract specific claims to headers

### 6. Flexible Requirements
- Per-route requirements
- Allow missing JWT
- Allow failed verification
- Require all providers
- Require any provider

## Statistics

| Stat | Type | Description |
|------|------|-------------|
| jwt_authn.allowed | Counter | Requests allowed (JWT valid) |
| jwt_authn.denied | Counter | Requests denied (JWT invalid) |
| jwt_authn.jwks_fetch_success | Counter | Successful JWKS fetches |
| jwt_authn.jwks_fetch_failed | Counter | Failed JWKS fetches |

## Common Use Cases

### 1. API Authentication
Validate JWT tokens for API access

### 2. Microservices Authentication
Validate service-to-service JWTs

### 3. Mobile App Authentication
Validate mobile app tokens

### 4. Single Sign-On (SSO)
Validate SSO tokens from identity provider

### 5. Multi-Tenant Applications
Different JWT providers per tenant

### 6. Gradual Migration
Support old and new JWT providers simultaneously

## Best Practices

1. **Use remote JWKS** - Automatic key rotation support
2. **Configure appropriate cache duration** - Balance freshness and performance
3. **Set reasonable timeouts** - For JWKS fetching
4. **Validate all critical claims** - iss, aud, exp at minimum
5. **Use allow_missing carefully** - Understand security implications
6. **Monitor JWKS fetch failures** - Critical for auth availability
7. **Configure clock skew** - Account for time synchronization issues
8. **Use forward_payload_header** - For downstream filters/services
9. **Implement key rotation** - Regular key updates in JWKS
10. **Test token expiration** - Ensure proper handling of expired tokens

## Security Considerations

1. **Always validate exp claim** - Prevent replay attacks
2. **Validate aud claim** - Prevent token reuse across services
3. **Use HTTPS for JWKS** - Prevent MITM attacks
4. **Implement rate limiting** - Prevent brute force attacks
5. **Monitor failed verifications** - Detect attack patterns
6. **Use strong algorithms** - RS256 or ES256, avoid HS256 with shared secrets
7. **Validate kid** - Prevent key confusion attacks

## Related Filters

- **ext_authz**: Alternative auth mechanism
- **rbac**: Authorization after authentication
- **oauth2**: Full OAuth2 flow support

## References

- [Envoy JWT Authentication Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/jwt_authn_filter)
- [JWT Specification (RFC 7519)](https://tools.ietf.org/html/rfc7519)
- [JWKS Specification (RFC 7517)](https://tools.ietf.org/html/rfc7517)
