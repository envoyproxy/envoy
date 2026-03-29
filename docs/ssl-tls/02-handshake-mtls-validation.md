# Part 2: SSL/TLS — Handshake, mTLS, and Certificate Validation

## SslSocket — The TLS Transport Socket

```mermaid
classDiagram
    class SslSocket {
        -ctx_ : ContextImplSharedPtr
        -ssl_ : bssl::UniquePtr~SSL~
        -info_ : SslHandshakerImpl
        -state_ : InitialState (Client/Server)
        +doRead(buffer) IoResult
        +doWrite(buffer, end_stream) IoResult
        +onConnected() void
        +ssl() ConnectionInfoConstSharedPtr
        +closeSocket(type) void
        +setTransportSocketCallbacks(callbacks) void
    }
    class SslHandshakerImpl {
        -ssl_ : bssl::UniquePtr~SSL~
        -extended_socket_info_ : SslExtendedSocketInfoImpl
        +doHandshake() Network::PostIoAction
        +ssl() SSL*
    }
    class SslExtendedSocketInfoImpl {
        -cert_validation_result_ : ValidateStatus
        +setCertificateValidationStatus(status)
        +certificateValidationStatus() ValidateStatus
        +createValidateResultCallback() ValidateResultCallbackPtr
        +onCertificateValidationCompleted(succeeded, async)
    }

    SslSocket --> SslHandshakerImpl
    SslHandshakerImpl --> SslExtendedSocketInfoImpl
```

## TLS Handshake Flow

### Server-Side Handshake

```mermaid
sequenceDiagram
    participant Client
    participant Conn as ConnectionImpl
    participant SS as SslSocket
    participant HS as SslHandshakerImpl
    participant BIO as IoHandle BIO
    participant SSL as BoringSSL

    Client->>Conn: TCP connected
    Conn->>SS: onConnected()
    SS->>SS: SSL_set_accept_state()
    
    Client->>Conn: Data available (fd event)
    Conn->>SS: doRead(buffer)
    SS->>SS: handshake complete? No
    SS->>HS: doHandshake()
    HS->>SSL: SSL_do_handshake()
    
    Note over SSL,BIO: BoringSSL reads ClientHello\nvia BIO → IoHandle
    
    SSL->>SSL: select_certificate_cb()\n→ SNI cert selection
    
    alt Certificate validation needed
        SSL-->>HS: SSL_ERROR_WANT_CERTIFICATE_VERIFY
        HS-->>SS: PostIoAction::KeepOpen
        Note over SS: Wait for async cert validation
    end
    
    SSL->>BIO: Write ServerHello, Certificate, etc.
    BIO->>Client: TLS handshake records
    
    Client->>BIO: ClientKeyExchange, Finished
    BIO->>SSL: Read handshake data
    SSL-->>HS: SSL_do_handshake() returns 1 (success)
    HS->>SS: onSuccess(ssl)
    SS->>Conn: raiseEvent(Connected)
    
    Note over Conn: Connection is now TLS-secured
    Note over Conn: Subsequent doRead/doWrite\ngo through SSL_read/SSL_write
```

### Client-Side Handshake

```mermaid
sequenceDiagram
    participant Conn as ConnectionImpl
    participant SS as SslSocket
    participant SSL as BoringSSL
    participant Server

    Conn->>SS: onConnected()
    SS->>SS: SSL_set_connect_state()
    SS->>SS: Set SNI from TransportSocketOptions
    
    alt Session resumption available
        SS->>SSL: SSL_set_session(cached_session)
    end
    
    SS->>SSL: SSL_do_handshake()
    SSL->>Server: ClientHello (SNI, ALPN, etc.)
    
    Server->>SSL: ServerHello, Certificate
    SSL->>SSL: Verify server certificate
    Note over SSL: CertValidator::doVerifyCertChain()
    
    alt Client certificate required (mTLS)
        SSL->>SSL: Select client certificate
        SSL->>Server: Certificate, CertificateVerify
    end
    
    Server->>SSL: Finished
    SSL-->>SS: Handshake success
    SS->>Conn: raiseEvent(Connected)
    
    alt New session ticket received
        SSL->>SS: newSessionKey callback
        SS->>SS: Cache SSL_SESSION in session_keys_
    end
```

## Handshake Error Handling

```mermaid
flowchart TD
    A["SSL_do_handshake()"] --> B{Return value}
    
    B -->|"1 (success)"| C["Handshake complete\nonSuccess(ssl)\nraiseEvent(Connected)"]
    
    B -->|"SSL_ERROR_WANT_READ"| D["Need more data\nPostIoAction::KeepOpen\nWait for next fd event"]
    
    B -->|"SSL_ERROR_WANT_WRITE"| E["Write buffer full\nPostIoAction::KeepOpen\nRetry on write event"]
    
    B -->|"SSL_ERROR_WANT_CERTIFICATE_VERIFY"| F["Async cert validation\nPostIoAction::KeepOpen\nResume after validation"]
    
    B -->|"SSL_ERROR_WANT_X509_LOOKUP"| G["Need certificate lookup\nPostIoAction::KeepOpen"]
    
    B -->|"SSL_ERROR_PENDING_CERTIFICATE"| H["Pending cert selection\nPostIoAction::KeepOpen"]
    
    B -->|"SSL_ERROR_SSL\nor other error"| I["Handshake failed\nraiseEvent(RemoteClose)\nPostIoAction::Close"]
```

## mTLS (Mutual TLS)

### Architecture

```mermaid
graph TB
    subgraph "Server Side (Envoy Downstream)"
        SCtx["ServerContextImpl"]
        CV["CertValidator\n(DefaultCertValidator)"]
        RequireClient["require_client_certificate\n= true"]
    end
    
    subgraph "Client Side"
        ClientCert["Client Certificate\n+ Private Key"]
        ClientCA["Trust Store\n(CA certs)"]
    end
    
    subgraph "Validation"
        ChainVerify["Certificate chain\nverification"]
        SANCheck["SAN matching\n(DNS, URI, IP, email)"]
        SPKIPin["SPKI hash pinning"]
        CRLCheck["CRL/OCSP checking"]
    end
    
    SCtx --> RequireClient
    RequireClient -->|"SSL_VERIFY_PEER |\nSSL_VERIFY_FAIL_IF_NO_PEER_CERT"| CV
    ClientCert -->|"Presented during\nhandshake"| CV
    CV --> ChainVerify
    CV --> SANCheck
    CV --> SPKIPin
    CV --> CRLCheck
```

### mTLS Handshake Sequence

```mermaid
sequenceDiagram
    participant Client
    participant Envoy as Envoy (Server)
    participant CV as CertValidator

    Client->>Envoy: ClientHello
    Envoy->>Client: ServerHello, Certificate
    Envoy->>Client: CertificateRequest
    Note over Envoy: require_client_certificate = true
    
    Client->>Envoy: Certificate (client cert chain)
    Client->>Envoy: CertificateVerify (signed)
    Client->>Envoy: Finished
    
    Envoy->>CV: doVerifyCertChain(cert, depth, ssl)
    
    CV->>CV: X509_verify_cert() — chain validation
    CV->>CV: Check SAN against match_typed_subject_alt_names
    CV->>CV: Check SPKI hash against verify_certificate_spki
    CV->>CV: Check CRL if configured
    
    alt Validation succeeded
        CV-->>Envoy: Accepted
        Envoy->>Client: Finished (handshake complete)
    else Validation failed
        CV-->>Envoy: Rejected
        Envoy->>Client: Alert: handshake_failure
    end
```

## Certificate Validation

```mermaid
classDiagram
    class CertValidator {
        <<interface>>
        +addClientValidationContext(ssl_ctx, require_client_cert)
        +doVerifyCertChain(cert_chain, callbacks, transport_options, ssl, is_server) ValidationResults
        +initializeSslContexts(contexts, provides_certificates)
        +updateDigestForSessionId(md, ssl_ctx)
    }
    class DefaultCertValidator {
        -config_ : Envoy::Ssl::CertificateValidationContextConfig
        -stats_ : SslStats
        -trusted_ca_ : X509_STORE
        -subject_alt_name_matchers_ : vector
        -verify_certificate_hash_list_ : vector
        -verify_certificate_spki_list_ : vector
        -crl_ : X509_CRL
        +addClientValidationContext(ssl_ctx, require)
        +doVerifyCertChain(chain, callbacks, options, ssl, is_server)
        -verifyCertChainByCustomValidators(chain, info)
        -matchSubjectAltName(cert) bool
        -verifyCertificateHashList(cert) bool
        -verifyCertificateSpkiList(cert) bool
    }

    DefaultCertValidator ..|> CertValidator
```

### Validation Steps

```mermaid
flowchart TD
    A["doVerifyCertChain()"] --> B["X509_verify_cert()\nChain verification\nagainst trusted CA"]
    B --> C{Chain valid?}
    C -->|No| D["Reject: chain verification failed"]
    C -->|Yes| E["Extract leaf certificate"]
    
    E --> F{SAN matchers configured?}
    F -->|Yes| G["matchSubjectAltName(cert)"]
    G -->|No match| H["Reject: SAN mismatch"]
    G -->|Match| I["Continue"]
    F -->|No| I
    
    I --> J{Certificate hash list?}
    J -->|Yes| K["verifyCertificateHashList(cert)"]
    K -->|No match| L["Reject: hash mismatch"]
    K -->|Match| M["Continue"]
    J -->|No| M
    
    M --> N{SPKI list?}
    N -->|Yes| O["verifyCertificateSpkiList(cert)"]
    O -->|No match| P["Reject: SPKI mismatch"]
    O -->|Match| Q["Accept"]
    N -->|No| Q
    
    Q --> R["Certificate validation succeeded"]
```

### SAN Matching Types

```mermaid
graph TD
    subgraph "match_typed_subject_alt_names"
        DNS["DNS SAN\n(e.g., *.example.com)"]
        URI["URI SAN\n(e.g., spiffe://cluster.local/ns/default/sa/app)"]
        IP["IP SAN\n(e.g., 10.0.0.1)"]
        Email["Email SAN\n(e.g., admin@example.com)"]
    end
    
    subgraph "Matchers"
        Exact["Exact match"]
        Prefix["Prefix match"]
        Suffix["Suffix match"]
        Contains["Contains match"]
        Regex["Regex match"]
    end
    
    DNS --> Exact & Suffix
    URI --> Exact & Prefix
    IP --> Exact
    Email --> Exact
```

## tls_inspector Listener Filter

The `tls_inspector` inspects the TLS ClientHello without completing the handshake, extracting SNI and ALPN for filter chain matching.

```mermaid
sequenceDiagram
    participant Client
    participant LF as tls_inspector Filter
    participant SSL as BoringSSL (partial)
    participant FCM as Filter Chain Manager

    Client->>LF: TCP data (beginning of TLS ClientHello)
    LF->>LF: onAccept() → StopIteration
    LF->>LF: onData(raw_bytes)
    
    LF->>SSL: BIO_new_mem_buf(raw_bytes)
    LF->>SSL: SSL_do_handshake()
    
    Note over SSL: select_certificate_cb fires\n→ extract SNI from ClientHello
    SSL->>LF: onServername("api.example.com")
    LF->>LF: socket.setRequestedServerName("api.example.com")
    
    Note over SSL: info_callback fires\n→ extract ALPN
    SSL->>LF: onALPN(["h2", "http/1.1"])
    LF->>LF: socket.setRequestedApplicationProtocols(["h2", "http/1.1"])
    
    opt JA3/JA4 fingerprinting enabled
        LF->>LF: createJA3Hash() / createJA4Hash()
        LF->>LF: Store fingerprint in filter state
    end
    
    LF->>LF: socket.setDetectedTransportProtocol("tls")
    LF-->>FCM: Continue → filter chain matching with SNI + ALPN
    
    Note over FCM: FCM selects filter chain\nbased on SNI and ALPN
```

```mermaid
flowchart TD
    A["tls_inspector receives ClientHello"] --> B["Parse without completing handshake"]
    B --> C["Extract SNI"]
    B --> D["Extract ALPN"]
    B --> E["Detect TLS protocol"]
    B --> F["Optionally compute JA3/JA4"]
    
    C --> G["Set on socket:\nrequested_server_name"]
    D --> H["Set on socket:\napplication_protocols"]
    E --> I["Set on socket:\ntransport_protocol = 'tls'"]
    F --> J["Store in filter state"]
    
    G --> K["Filter Chain Matching\n(select correct filter chain\nbased on SNI + ALPN)"]
    H --> K
    I --> K
```

## I/O Through SslSocket

```mermaid
flowchart TD
    subgraph "doRead(buffer)"
        R1["Check handshake complete"]
        R1 -->|"No"| R2["doHandshake()\n→ may return KeepOpen"]
        R1 -->|"Yes"| R3["SSL_read()\n→ decrypt data into buffer"]
        R3 --> R4{SSL_ERROR?}
        R4 -->|"NONE"| R5["Return bytes read"]
        R4 -->|"WANT_READ"| R6["Return: no action"]
        R4 -->|"ZERO_RETURN"| R7["Peer closed\nraiseEvent(RemoteClose)"]
        R4 -->|"ERROR"| R8["Connection error"]
    end
    
    subgraph "doWrite(buffer, end_stream)"
        W1["Check handshake complete"]
        W1 -->|"No"| W2["doHandshake()"]
        W1 -->|"Yes"| W3["SSL_write(buffer)\n→ encrypt and send"]
        W3 --> W4{All bytes written?}
        W4 -->|"Yes, end_stream"| W5["SSL_shutdown()"]
        W4 -->|"Yes"| W6["Return"]
        W4 -->|"Partial"| W7["Return bytes_to_retry"]
    end
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `source/common/tls/ssl_socket.h/cc` | `SslSocket` | TLS transport socket |
| `source/common/tls/ssl_handshaker.h/cc` | `SslHandshakerImpl`, `SslExtendedSocketInfoImpl` | Handshake wrapper |
| `source/common/tls/io_handle_bio.h/cc` | BIO over IoHandle | Custom BIO for BoringSSL |
| `source/common/tls/cert_validator/default_validator.h/cc` | `DefaultCertValidator` | Certificate validation |
| `source/common/tls/cert_validator/cert_validator.h` | `CertValidator` | Validator interface |
| `source/extensions/filters/listener/tls_inspector/tls_inspector.h/cc` | `Filter` | ClientHello inspection |

---

**Previous:** [Part 1 — TLS Architecture & Contexts](01-architecture-contexts.md)  
**Next:** [Part 3 — SDS, Rotation, OCSP, and Session Resumption](03-sds-rotation-ocsp.md)
