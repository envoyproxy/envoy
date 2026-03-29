# Part 3: SSL/TLS — SDS, Rotation, OCSP, and Session Resumption

## Secret Discovery Service (SDS)

### What SDS Provides

SDS delivers TLS secrets to Envoy dynamically via xDS, eliminating the need to store certificates on disk and enabling zero-downtime certificate rotation.

```mermaid
graph TB
    subgraph "SDS Server"
        XDS["xDS Management Server"]
    end
    
    subgraph "Secrets Delivered"
        TLSCert["TLS Certificates\n(cert chain + private key)"]
        ValCtx["Validation Context\n(CA certs, CRL)"]
        SessionKeys["Session Ticket Keys"]
        Generic["Generic Secrets\n(HMAC keys, etc.)"]
    end
    
    subgraph "Envoy"
        SDS["SdsApi\n(per-secret subscription)"]
        SM["SecretManagerImpl"]
        SSF["ServerSslSocketFactory"]
        CSF["ClientSslSocketFactory"]
    end
    
    XDS -->|"StreamSecrets RPC"| SDS
    SDS --> TLSCert & ValCtx & SessionKeys & Generic
    TLSCert --> SM
    ValCtx --> SM
    SessionKeys --> SM
    SM --> SSF & CSF
```

### SDS Class Hierarchy

```mermaid
classDiagram
    class SdsApi {
        <<abstract>>
        #subscription_ : Config::Subscription
        #update_callback_manager_ : SecretCallbackManager
        +onConfigUpdate(resources, version) absl::Status
        #setSecret(secret)* void
        #loadFiles()
        #resolveSecret(secret)
    }
    class TlsCertificateSdsApi {
        -tls_certificate_ : TlsCertificate
        +setSecret(secret)
        +secret() TlsCertificate
    }
    class CertificateValidationContextSdsApi {
        -validation_context_ : CertificateValidationContext
        +setSecret(secret)
        +secret() CertificateValidationContext
    }
    class TlsSessionTicketKeysSdsApi {
        -session_ticket_keys_ : TlsSessionTicketKeys
        +setSecret(secret)
    }
    class GenericSecretSdsApi {
        -generic_secret_ : GenericSecret
        +setSecret(secret)
    }
    class SecretManagerImpl {
        -static_tls_certificate_providers_ : map
        -static_certificate_validation_context_providers_ : map
        -dynamic_tls_certificate_providers_ : DynamicSecretProviders
        +findOrCreateTlsCertificateProvider(sds_config, name)
        +findOrCreateCertificateValidationContextProvider(sds_config, name)
    }

    TlsCertificateSdsApi --|> SdsApi
    CertificateValidationContextSdsApi --|> SdsApi
    TlsSessionTicketKeysSdsApi --|> SdsApi
    GenericSecretSdsApi --|> SdsApi
    SecretManagerImpl --> SdsApi : "manages"
```

### SDS Update Flow

```mermaid
sequenceDiagram
    participant XDS as xDS Server
    participant Sub as gRPC Subscription
    participant SDS as TlsCertificateSdsApi
    participant SM as SecretManagerImpl
    participant CB as Update Callbacks
    participant SSF as ServerSslSocketFactory
    participant Ctx as ServerContextImpl

    XDS->>Sub: DiscoveryResponse\n(new certificate + key)
    Sub->>SDS: onConfigUpdate(resources, version)
    SDS->>SDS: setSecret(new_cert)
    SDS->>SDS: Parse cert chain, private key
    
    SDS->>CB: update_callback_manager_.runCallbacks()
    CB->>SSF: onAddOrUpdateSecret()
    SSF->>SSF: manager_.createSslServerContext(config_, ...)
    SSF->>Ctx: ServerContextImpl::create(new_config)
    
    Note over SSF: Swap ssl_ctx_ under mutex
    SSF->>SSF: ssl_ctx_ = new_context
    
    Note over SSF: New connections use new cert\nExisting connections keep old cert
```

## Certificate Rotation

### SDS-Based Rotation

```mermaid
flowchart TD
    A["Management server\npushes new cert via SDS"] --> B["SdsApi::onConfigUpdate()"]
    B --> C["setSecret(new_cert)"]
    C --> D["runCallbacks()"]
    D --> E["ServerSslSocketFactory::\nonAddOrUpdateSecret()"]
    E --> F["Create new ServerContextImpl\nwith new certificate"]
    F --> G["Swap ssl_ctx_ atomically"]
    
    G --> H["New connections → new cert"]
    G --> I["Existing connections → old cert\n(until they close)"]
```

### File-Based Rotation (Watched Directory)

```mermaid
sequenceDiagram
    participant FS as Filesystem
    participant Watch as WatchedDirectory
    participant SDS as TlsCertificateSdsApi
    participant SSF as ServerSslSocketFactory
    participant Ctx as ServerContextImpl

    Note over FS: Admin renames symlink\n(e.g., Kubernetes secret update)
    FS->>Watch: Filesystem event
    Watch->>SDS: onWatchUpdate()
    SDS->>SDS: loadFiles()
    Note over SDS: Read cert and key from new path
    SDS->>SDS: resolveSecret()
    Note over SDS: Inline file content into proto
    SDS->>SDS: update_callback_manager_.runCallbacks()
    SDS->>SSF: onAddOrUpdateSecret()
    SSF->>Ctx: Create new context with new files
    SSF->>SSF: Swap ssl_ctx_
```

### Rotation Guarantees

```mermaid
graph TD
    subgraph "During Rotation"
        Old["Old SSL_CTX\n(shared_ptr, ref-counted)"]
        New["New SSL_CTX"]
        
        E1["Existing connection 1\n→ holds ref to Old"]
        E2["Existing connection 2\n→ holds ref to Old"]
        N1["New connection 1\n→ uses New"]
        N2["New connection 2\n→ uses New"]
    end
    
    Old --> E1 & E2
    New --> N1 & N2
    
    Note["Old context is freed\nwhen last connection\nholding a ref closes"]
```

## OCSP Stapling

### What OCSP Stapling Does

OCSP (Online Certificate Status Protocol) stapling allows the server to include a pre-fetched OCSP response in the TLS handshake, proving the certificate hasn't been revoked without the client needing to contact the OCSP responder.

```mermaid
sequenceDiagram
    participant OCSP as OCSP Responder
    participant Admin as Operator / SDS
    participant Envoy as Envoy Server
    participant Client

    Admin->>OCSP: Request OCSP response for cert
    OCSP-->>Admin: Signed OCSP response
    Admin->>Envoy: Configure OCSP response\n(via SDS or file)
    
    Client->>Envoy: ClientHello\n(status_request extension)
    Envoy->>Envoy: Select certificate
    Envoy->>Envoy: SSL_set_ocsp_response(ocsp_response)
    Envoy->>Client: Certificate + OCSP staple
    
    Client->>Client: Verify OCSP response\n→ cert is not revoked
```

### OCSP Staple Policy

```mermaid
flowchart TD
    A["Certificate selected\nfor connection"] --> B{OCSP response available?}
    
    B -->|Yes| C["SSL_set_ocsp_response()\nStaple included in handshake"]
    
    B -->|No| D{ocsp_staple_policy?}
    D -->|LENIENT_STAPLING| E["Continue without staple\n(default)"]
    D -->|STRICT_STAPLING| F{is_must_staple cert?}
    F -->|Yes| G["Reject connection\n(must-staple requires OCSP)"]
    F -->|No| H["Continue without staple"]
    D -->|MUST_STAPLE| I["Reject connection"]
```

### OCSP Response Parsing

```mermaid
classDiagram
    class OcspResponseWrapperImpl {
        -raw_bytes_ : vector~uint8_t~
        -response_ : OcspResponse
        +create(raw_bytes, time) OcspResponseWrapperImplPtr
        +isExpired() bool
        +rawBytes() vector~uint8_t~
        +secondsUntilExpiration() seconds
    }
    class OcspResponse {
        +status_ : OcspResponseStatus
        +response_bytes_ : ResponseBytes
    }
    class SingleResponse {
        +cert_id_ : CertId
        +cert_status_ : CertStatus
        +this_update_ : time_point
        +next_update_ : time_point
    }

    OcspResponseWrapperImpl --> OcspResponse
    OcspResponse --> SingleResponse
```

## Session Resumption

### Server-Side: Session Tickets (Stateless)

```mermaid
sequenceDiagram
    participant Client
    participant Envoy as Envoy Server
    participant TK as Session Ticket Keys

    Note over Client,Envoy: Initial Handshake
    Client->>Envoy: ClientHello
    Envoy->>Client: ServerHello, Certificate, etc.
    Note over Client,Envoy: Full handshake completes
    
    Envoy->>TK: Encrypt session state with current key
    Envoy->>Client: NewSessionTicket(encrypted_ticket)
    
    Note over Client: Client stores ticket
    Note over Client,Envoy: Subsequent Connection
    
    Client->>Envoy: ClientHello + SessionTicket
    Envoy->>TK: Decrypt ticket with matching key
    
    alt Key found and valid
        TK-->>Envoy: Session state restored
        Envoy->>Client: ServerHello (abbreviated handshake)
        Note over Client,Envoy: No full handshake needed!\nSaved 1 round trip
    else Key expired/rotated
        Envoy->>Client: Full handshake (fallback)
    end
```

### Session Ticket Key Rotation

```mermaid
graph TD
    subgraph "Session Ticket Keys (ordered)"
        K1["Key 1 (newest)\n→ Encrypt + Decrypt"]
        K2["Key 2\n→ Decrypt only"]
        K3["Key 3 (oldest)\n→ Decrypt only"]
    end
    
    subgraph "Operations"
        Encrypt["New tickets encrypted\nwith Key 1 only"]
        Decrypt["Resumption decrypts\nwith any matching key"]
    end
    
    K1 --> Encrypt
    K1 --> Decrypt
    K2 --> Decrypt
    K3 --> Decrypt
    
    Note["SDS can rotate keys:\npush new key set → old tickets\nstill work until old keys drop off"]
```

```
File: source/common/tls/server_context_impl.cc

sessionTicketProcess(ssl, key_name, iv, ctx, hmac, encrypt):
    if encrypt:
        Use first (newest) key to encrypt
        Set key_name = first_key.name
    else:
        Find key by key_name in session_ticket_keys_
        if found:
            Decrypt with that key
        else:
            Return 0 (ticket invalid, full handshake)
```

### Client-Side: Session Caching

```mermaid
graph TD
    subgraph "ClientContextImpl"
        SK["session_keys_\n(deque<SSL_SESSION>)"]
        MAX["max_session_keys_\n(LRU eviction)"]
        SINGLE["session_keys_single_use_\n(TLS 1.3)"]
    end
    
    subgraph "Flow"
        NS["newSessionKey()\ncallback from BoringSSL"]
        NS -->|"Push front"| SK
        SK -->|"Exceeds max?"| EVICT["Pop back\n(evict oldest)"]
        
        RESUME["SSL_CTX_sess_set_get_cb()"]
        RESUME -->|"Return front session"| SSL["BoringSSL uses\nfor resumption"]
    end
    
    subgraph "TLS 1.3 Single-Use"
        SINGLE -->|"true"| ERASE["Erase session\nafter use"]
        SINGLE -->|"false"| KEEP["Reuse session\n(TLS 1.2 compat)"]
    end
```

### Server-Side: Session ID Cache (Stateful)

```mermaid
flowchart TD
    A["Session ID Caching"] --> B["SSL_CTX_set_session_id_context()\n→ context ID from server names hash"]
    B --> C["SSL_CTX_set_timeout()\n→ session timeout"]
    C --> D["BoringSSL internal cache\nkeyed by session ID"]
    
    E["disable_stateful_session_resumption"] -->|"true"| F["SSL_SESS_CACHE_OFF"]
    G["disable_stateless_session_resumption"] -->|"true"| H["SSL_OP_NO_TICKET"]
```

## Complete TLS Lifecycle

```mermaid
graph TB
    subgraph "1. Configuration"
        Proto["TLS proto config"] --> SDS_Config["SDS or static secrets"]
        SDS_Config --> SecretMgr["SecretManagerImpl"]
        SecretMgr --> CtxConfig["ContextConfigImpl"]
    end
    
    subgraph "2. Context Creation"
        CtxConfig --> CtxImpl["ServerContextImpl /\nClientContextImpl"]
        CtxImpl --> SSLCTX["SSL_CTX (BoringSSL)"]
    end
    
    subgraph "3. Socket Factory"
        SSLCTX --> Factory["ServerSslSocketFactory /\nClientSslSocketFactory"]
    end
    
    subgraph "4. Connection"
        Factory --> Socket["SslSocket created\nper connection"]
        Socket --> Handshake["TLS handshake\n(doHandshake)"]
        Handshake --> IO["SSL_read / SSL_write\n(doRead / doWrite)"]
    end
    
    subgraph "5. Rotation"
        SDS_Update["SDS update\nor file change"] --> NewCtx["New context created"]
        NewCtx --> SwapCtx["Factory swaps ssl_ctx_"]
        SwapCtx --> NewConn["New connections\nuse new context"]
    end
    
    subgraph "6. Session Resumption"
        SessionTicket["Session ticket\n(stateless)"]
        SessionID["Session ID cache\n(stateful)"]
        ClientCache["Client session cache"]
    end
```

## TLS Stats

```mermaid
graph TD
    subgraph "Connection Stats"
        S1["ssl.connection_error"]
        S2["ssl.handshake"]
        S3["ssl.session_reused"]
        S4["ssl.no_certificate"]
        S5["ssl.fail_verify_no_cert"]
        S6["ssl.fail_verify_error"]
        S7["ssl.fail_verify_san"]
        S8["ssl.fail_verify_cert_hash"]
    end
    
    subgraph "Protocol Stats"
        P1["ssl.versions.TLSv1.2"]
        P2["ssl.versions.TLSv1.3"]
        P3["ssl.curves.X25519"]
        P4["ssl.ciphers.AES128-GCM-SHA256"]
        P5["ssl.sigalgs.rsa_pss_rsae_sha256"]
    end
    
    subgraph "Management Stats"
        M1["ssl_context_update_by_sds"]
        M2["downstream_context_secrets_not_ready"]
        M3["upstream_context_secrets_not_ready"]
    end
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `source/common/secret/sds_api.h/cc` | `SdsApi`, `TlsCertificateSdsApi` | SDS subscription and secret management |
| `source/common/secret/secret_manager_impl.h` | `SecretManagerImpl` | Central secret registry |
| `source/common/secret/secret_provider_impl.h/cc` | Secret providers | Static and dynamic providers |
| `source/common/tls/server_ssl_socket.h/cc` | `ServerSslSocketFactory` | Factory with rotation support |
| `source/common/tls/client_ssl_socket.h/cc` | `ClientSslSocketFactory` | Factory with rotation support |
| `source/common/tls/ocsp/ocsp.h/cc` | `OcspResponseWrapperImpl` | OCSP response parsing |
| `source/common/tls/server_context_impl.h/cc` | Session tickets, OCSP stapling | Server-specific TLS features |
| `source/common/tls/client_context_impl.h/cc` | Session cache | Client session resumption |
| `source/common/tls/context_impl.h/cc` | `ContextImpl`, `TlsContext` | Base TLS context |

---

**Previous:** [Part 2 — Handshake, mTLS, and Certificate Validation](02-handshake-mtls-validation.md)  
**Back to:** [Part 1 — TLS Architecture & Contexts](01-architecture-contexts.md)
