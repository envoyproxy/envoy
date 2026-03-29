# TLS Context Implementation

## Overview

TLS contexts in Envoy manage SSL/TLS connections for both downstream (client-facing) and upstream (backend) connections. The TLS context contains certificates, private keys, trusted CA certificates, cipher suites, and TLS protocol versions. Envoy builds separate SSL contexts for different listeners and clusters, enabling fine-grained TLS configuration.

## Architecture

```mermaid
classDiagram
    class ContextManager {
        +map~string,ServerContext~ server_contexts_
        +map~string,ClientContext~ client_contexts_
        +createSslServerContext() ServerContextPtr
        +createSslClientContext() ClientContextPtr
    }

    class ServerContext {
        +SSL_CTX* ssl_ctx_
        +vector~CertificateInfo~ cert_chains_
        +vector~string~ alpn_protocols_
        +SessionContextID session_context_id_
        +newSsl() bssl::UniquePtr~SSL~
    }

    class ClientContext {
        +SSL_CTX* ssl_ctx_
        +CertificateInfo client_cert_
        +string server_name_indication_
        +newSsl() bssl::UniquePtr~SSL~
    }

    class TlsCertificate {
        +string cert_chain_
        +string private_key_
        +string password_
        +PrivateKeyMethodProvider* method_provider_
    }

    class CertificateValidationContext {
        +string trusted_ca_
        +vector~string~ verify_certificate_hash_
        +vector~string~ verify_certificate_spki_
        +bool allow_expired_certificate_
    }

    class CommonTlsContext {
        +TlsParams tls_params_
        +vector~TlsCertificate~ tls_certificates_
        +CertificateValidationContext validation_context_
        +vector~string~ alpn_protocols_
    }

    ContextManager --> ServerContext
    ContextManager --> ClientContext
    ServerContext --> CommonTlsContext
    ClientContext --> CommonTlsContext
    CommonTlsContext --> TlsCertificate
    CommonTlsContext --> CertificateValidationContext
```

## Downstream (Server) TLS Context Creation

```mermaid
sequenceDiagram
    participant Config as Configuration
    participant Manager as Context Manager
    participant Factory as SSL Context Factory
    participant BoringSSL as BoringSSL (SSL_CTX)
    participant SDS as Secret Discovery

    Config->>Manager: createSslServerContext(config)
    Manager->>Factory: Create factory with config

    alt Static Secrets
        Factory->>Factory: Load cert from file/inline
    else Dynamic Secrets (SDS)
        Factory->>SDS: Request secrets
        SDS-->>Factory: Certificate + Private Key
    end

    Factory->>BoringSSL: SSL_CTX_new(TLS_server_method())
    BoringSSL-->>Factory: SSL_CTX*

    Factory->>BoringSSL: SSL_CTX_set_options()
    Note over Factory,BoringSSL: Set TLS versions,<br/>cipher suites, options

    Factory->>BoringSSL: SSL_CTX_use_certificate_chain()
    Factory->>BoringSSL: SSL_CTX_use_PrivateKey()

    alt Client Certificate Required
        Factory->>BoringSSL: SSL_CTX_set_verify(SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT)
        Factory->>BoringSSL: SSL_CTX_load_verify_locations()
    end

    alt ALPN Configured
        Factory->>BoringSSL: SSL_CTX_set_alpn_select_cb()
    end

    Factory->>Manager: ServerContext
    Manager-->>Config: Context created
```

## Upstream (Client) TLS Context Creation

```mermaid
sequenceDiagram
    participant Cluster as Cluster Config
    participant Manager as Context Manager
    participant Factory as SSL Context Factory
    participant BoringSSL as BoringSSL (SSL_CTX)

    Cluster->>Manager: createSslClientContext(config)
    Manager->>Factory: Create factory

    Factory->>BoringSSL: SSL_CTX_new(TLS_client_method())
    BoringSSL-->>Factory: SSL_CTX*

    Factory->>BoringSSL: Set TLS versions & ciphers

    alt Client Certificate Auth (mTLS)
        Factory->>BoringSSL: SSL_CTX_use_certificate_chain()
        Factory->>BoringSSL: SSL_CTX_use_PrivateKey()
    end

    alt Verify Server Certificate
        Factory->>BoringSSL: SSL_CTX_set_verify(SSL_VERIFY_PEER)
        Factory->>BoringSSL: SSL_CTX_load_verify_locations()

        opt Custom Verification
            Factory->>BoringSSL: SSL_CTX_set_cert_verify_callback()
        end
    end

    alt SNI Configured
        Factory->>Factory: Store SNI for connection
    end

    alt ALPN Protocols
        Factory->>BoringSSL: SSL_CTX_set_alpn_protos()
    end

    Factory->>Manager: ClientContext
    Manager-->>Cluster: Context created
```

## SSL_CTX Structure and Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Created: SSL_CTX_new()
    Created --> Configured: Configure options
    Configured --> Configured: Add certificates
    Configured --> Configured: Set cipher suites
    Configured --> Configured: Set ALPN/SNI callbacks
    Configured --> Active: Ready for connections
    Active --> Active: SSL_new() per connection
    Active --> Updating: SDS update received
    Updating --> Active: Atomic context swap
    Active --> Destroyed: Shutdown
    Destroyed --> [*]: SSL_CTX_free()

    note right of Active
        Multiple SSL connections
        share same SSL_CTX
    end note

    note right of Updating
        New SSL_CTX created
        Old connections continue
        New connections use new context
    end note
```

## Certificate Chain Loading

```mermaid
flowchart TD
    A[TLS Certificate Config] --> B{Source Type?}

    B -->|Filename| C[Read from filesystem]
    B -->|Inline String| D[Use inline content]
    B -->|Inline Bytes| E[Decode base64]
    B -->|SDS| F[Fetch from SDS]

    C --> G[Parse PEM format]
    D --> G
    E --> G
    F --> G

    G --> H[Extract certificates]
    H --> I[First cert: End-entity]
    I --> J[Remaining certs: Chain]

    J --> K[Validate chain]
    K --> L{Valid?}
    L -->|No| M[Reject configuration]
    L -->|Yes| N[Load into SSL_CTX]

    N --> O[SSL_CTX_use_certificate_ASN1]
    O --> P[SSL_CTX_add1_chain_cert]
    P --> Q{More chain certs?}
    Q -->|Yes| P
    Q -->|No| R[Certificate loaded]
```

## Private Key Loading and Management

```mermaid
flowchart TD
    A[Private Key Config] --> B{Key Type?}

    B -->|RSA| C[Load RSA key]
    B -->|EC| D[Load EC key]
    B -->|PKCS8| E[Load PKCS8]

    C --> F{Encrypted?}
    D --> F
    E --> F

    F -->|Yes| G[Decrypt with password]
    F -->|No| H[Load directly]

    G --> I{Private Key Method<br/>Provider?}
    H --> I

    I -->|Yes| J[Use hardware/KMS]
    J --> K[Set SSL_CTX_set_private_key_method]

    I -->|No| L[Use in-memory key]
    L --> M[SSL_CTX_use_PrivateKey]

    K --> N[Validate key matches cert]
    M --> N

    N --> O{Valid?}
    O -->|Yes| P[Ready for TLS]
    O -->|No| Q[Configuration error]
```

## TLS Parameters Configuration

```mermaid
classDiagram
    class TlsParameters {
        +TlsProtocol tls_minimum_protocol_version
        +TlsProtocol tls_maximum_protocol_version
        +vector~string~ cipher_suites
        +vector~string~ ecdh_curves
        +vector~string~ signature_algorithms
    }

    class TlsProtocol {
        <<enumeration>>
        TLS_AUTO
        TLSv1_0
        TLSv1_1
        TLSv1_2
        TLSv1_3
    }

    class CipherSuites {
        +string name
        +bool fips_compliant
        +int security_level
    }

    TlsParameters --> TlsProtocol
    TlsParameters --> CipherSuites
```

## Downstream TLS Handshake Flow

```mermaid
sequenceDiagram
    participant Client
    participant Network as Network Filter
    participant TLS as TLS Inspector
    participant SSL as SSL Connection
    participant Callback as Handshake Callbacks
    participant FilterChain

    Client->>Network: TCP Connection
    Network->>TLS: Inspect first bytes
    TLS->>TLS: Detect TLS ClientHello
    TLS->>SSL: Create SSL* from SSL_CTX
    SSL->>Client: ServerHello

    Note over Client,SSL: TLS Handshake

    alt Client Certificate Required
        SSL->>Client: CertificateRequest
        Client->>SSL: Certificate
        SSL->>Callback: Verify client cert
        Callback-->>SSL: Valid/Invalid
    end

    Client->>SSL: Finished
    SSL->>SSL: Handshake complete

    SSL->>Callback: SSL_CTX_set_alpn_select_cb
    Callback->>Callback: Select ALPN protocol
    Callback-->>SSL: Selected protocol

    SSL->>FilterChain: Match filter chain
    Note over SSL,FilterChain: Based on SNI, ALPN,<br/>source IP, etc.

    FilterChain-->>Network: Selected filter chain
    Network->>Client: Application data ready
```

## Upstream TLS Connection

```mermaid
sequenceDiagram
    participant Router
    participant ConnPool as Connection Pool
    participant SSL as SSL Client
    participant Upstream

    Router->>ConnPool: Request connection
    ConnPool->>SSL: Create SSL* from ClientContext

    alt SNI Configured
        SSL->>SSL: SSL_set_tlsext_host_name()
    end

    alt ALPN Configured
        SSL->>SSL: Set ALPN protocols
    end

    SSL->>Upstream: ClientHello
    Upstream->>SSL: ServerHello + Certificate

    SSL->>SSL: Verify server certificate
    alt Verification Mode
        SSL->>SSL: Check trust chain
        SSL->>SSL: Verify hostname/SAN

        opt Custom Verification
            SSL->>Callback: Custom verify callback
            Callback-->>SSL: Accept/Reject
        end
    end

    alt mTLS - Server requests client cert
        Upstream->>SSL: CertificateRequest
        SSL->>Upstream: Client Certificate
    end

    SSL->>Upstream: Finished
    Upstream->>SSL: Finished

    SSL->>SSL: Verify ALPN match
    SSL->>ConnPool: Connection ready
    ConnPool-->>Router: Connection established
```

## Certificate Validation Context

```mermaid
flowchart TD
    A[Incoming Certificate] --> B[Load from SSL handshake]
    B --> C{Trusted CA<br/>Configured?}

    C -->|Yes| D[Verify against trusted CA]
    C -->|No| E[System CA store]

    D --> F{Valid chain?}
    E --> F

    F -->|No| Z[Reject]
    F -->|Yes| G{Certificate Hash<br/>Validation?}

    G -->|Configured| H[Compare SHA-256 hash]
    G -->|Not configured| I{SPKI Validation?}

    H --> J{Match?}
    J -->|No| Z
    J -->|Yes| I

    I -->|Configured| K[Verify SPKI pins]
    I -->|Not configured| L{SAN Validation?}

    K --> M{Match?}
    M -->|No| Z
    M -->|Yes| L

    L -->|Configured| N[Verify SAN matches]
    L -->|Use SNI| O[Verify against SNI]

    N --> P{Match?}
    O --> P
    P -->|No| Z
    P -->|Yes| Q{Expiration Check?}

    Q --> R{allow_expired?}
    R -->|true| S[Accept]
    R -->|false| T{Expired?}
    T -->|Yes| Z
    T -->|No| S
```

## Session Resumption and Caching

```mermaid
sequenceDiagram
    participant Client1
    participant Server as Envoy Server
    participant Cache as Session Cache
    participant Client2

    Note over Server: First connection

    Client1->>Server: ClientHello
    Server->>Client1: ServerHello + Session ID
    Note over Client1,Server: Full TLS handshake
    Server->>Cache: Store session
    Cache-->>Server: Session cached
    Server->>Client1: Finished
    Client1->>Server: Application data

    Note over Server: Connection closed

    Note over Client2: Resumption

    Client2->>Server: ClientHello + Session ID
    Server->>Cache: Lookup session
    Cache-->>Server: Session found
    Server->>Client2: ServerHello (abbreviated)
    Note over Client2,Server: Abbreviated handshake<br/>No certificate exchange
    Server->>Client2: Finished
    Client2->>Server: Application data
```

## Configuration Example - Downstream TLS

```yaml
listeners:
  - name: https_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 443
    filter_chains:
      - filter_chain_match:
          server_names: ["example.com"]
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext

            # Common TLS configuration
            common_tls_context:

              # TLS parameters
              tls_params:
                tls_minimum_protocol_version: TLSv1_2
                tls_maximum_protocol_version: TLSv1_3
                cipher_suites:
                  - ECDHE-ECDSA-AES128-GCM-SHA256
                  - ECDHE-RSA-AES128-GCM-SHA256
                  - ECDHE-ECDSA-AES256-GCM-SHA384
                  - ECDHE-RSA-AES256-GCM-SHA384
                ecdh_curves:
                  - X25519
                  - P-256

              # Server certificates
              tls_certificates:
                - certificate_chain:
                    filename: /etc/ssl/certs/server-cert.pem
                  private_key:
                    filename: /etc/ssl/private/server-key.pem

              # Client certificate validation (mTLS)
              validation_context:
                trusted_ca:
                  filename: /etc/ssl/certs/ca-cert.pem
                verify_certificate_spki:
                  - "base64-encoded-spki-hash"
                match_typed_subject_alt_names:
                  - san_type: DNS
                    matcher:
                      exact: "client.example.com"

              # ALPN protocols
              alpn_protocols:
                - h2
                - http/1.1

            # Require client certificate
            require_client_certificate: true

            # Session resumption
            session_timeout: 3600s
```

## Configuration Example - Upstream TLS

```yaml
clusters:
  - name: backend_cluster
    type: STRICT_DNS
    load_assignment:
      cluster_name: backend_cluster
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: backend.example.com
                    port_value: 443

    transport_socket:
      name: envoy.transport_sockets.tls
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext

        # SNI to send to upstream
        sni: backend.example.com

        common_tls_context:

          # TLS parameters
          tls_params:
            tls_minimum_protocol_version: TLSv1_2
            tls_maximum_protocol_version: TLSv1_3

          # Client certificate for mTLS
          tls_certificates:
            - certificate_chain:
                filename: /etc/ssl/certs/client-cert.pem
              private_key:
                filename: /etc/ssl/private/client-key.pem

          # Verify server certificate
          validation_context:
            trusted_ca:
              filename: /etc/ssl/certs/ca-bundle.pem
            match_typed_subject_alt_names:
              - san_type: DNS
                matcher:
                  exact: "backend.example.com"

          # ALPN for HTTP/2
          alpn_protocols:
            - h2
```

## Memory Management

```mermaid
flowchart TD
    A[Context Manager] --> B[Reference Counting]
    B --> C[ServerContext: shared_ptr]
    B --> D[ClientContext: shared_ptr]

    C --> E[Multiple Listeners<br/>Share Context]
    D --> F[Multiple Clusters<br/>Share Context]

    E --> G[Active Connections]
    F --> H[Active Connections]

    G --> I{Last Connection<br/>Closed?}
    H --> I

    I -->|Yes| J[Ref count = 0]
    I -->|No| K[Keep context alive]

    J --> L[SSL_CTX_free]
    L --> M[Memory released]
```

## TLS Statistics

```yaml
# Downstream TLS stats
listener.0.0.0.0_443.ssl.connection_error
listener.0.0.0.0_443.ssl.handshake
listener.0.0.0.0_443.ssl.session_reused
listener.0.0.0.0_443.ssl.no_certificate
listener.0.0.0.0_443.ssl.fail_verify_no_cert
listener.0.0.0.0_443.ssl.fail_verify_error
listener.0.0.0.0_443.ssl.fail_verify_san
listener.0.0.0.0_443.ssl.fail_verify_cert_hash

# Upstream TLS stats
cluster.backend.ssl.connection_error
cluster.backend.ssl.handshake
cluster.backend.ssl.session_reused
cluster.backend.ssl.fail_verify_no_cert
cluster.backend.ssl.fail_verify_error
cluster.backend.ssl.fail_verify_san
```

## Best Practices

### 1. TLS Version Configuration
```yaml
tls_params:
  tls_minimum_protocol_version: TLSv1_2  # Never use TLS 1.0/1.1
  tls_maximum_protocol_version: TLSv1_3  # Prefer TLS 1.3
```

### 2. Cipher Suite Selection
```yaml
# Modern, secure cipher suites only
cipher_suites:
  - ECDHE-ECDSA-AES128-GCM-SHA256
  - ECDHE-RSA-AES128-GCM-SHA256
  - ECDHE-ECDSA-AES256-GCM-SHA384
  - ECDHE-RSA-AES256-GCM-SHA384
  - ECDHE-ECDSA-CHACHA20-POLY1305
  - ECDHE-RSA-CHACHA20-POLY1305
# Avoid: CBC ciphers, RC4, 3DES, export ciphers
```

### 3. Certificate Validation
```yaml
validation_context:
  trusted_ca:
    filename: /etc/ssl/ca-bundle.pem
  match_typed_subject_alt_names:
    - san_type: DNS
      matcher:
        exact: "backend.example.com"  # Always verify hostname
  verify_certificate_spki:
    - "base64-spki-hash"  # Certificate pinning for critical upstreams
```

### 4. Client Certificate Validation
```yaml
require_client_certificate: true
validation_context:
  trusted_ca:
    filename: /etc/ssl/client-ca.pem
  only_verify_leaf_cert_crl: false  # Verify entire chain
```

### 5. Session Resumption
```yaml
# Enable for performance
session_timeout: 3600s
session_ticket_keys:
  keys:
    - filename: /etc/ssl/session-ticket-key.bin
```

## Troubleshooting

### Debug TLS Issues

```bash
# Enable TLS debug logging
curl -X POST "http://localhost:9901/logging?connection=debug"
curl -X POST "http://localhost:9901/logging?ssl=trace"

# Check TLS stats
curl http://localhost:9901/stats | grep ssl

# View active connections
curl http://localhost:9901/certs

# Dump TLS configuration
curl http://localhost:9901/config_dump | jq '.configs[] | select(.["@type"] | contains("tls"))'
```

### Common Issues

1. **Certificate chain incomplete**: Include intermediate certificates
2. **Private key mismatch**: Ensure key matches certificate
3. **Cipher suite mismatch**: Client and server must share common ciphers
4. **Protocol version mismatch**: Ensure overlapping TLS versions
5. **SNI not sent**: Configure SNI on upstream connections
6. **Certificate validation fails**: Check SAN/hostname matching

## References

- [Envoy TLS Documentation](https://www.envoyproxy.io/docs/envoy/latest/intro/arch_overview/security/ssl)
- [BoringSSL](https://boringssl.googlesource.com/boringssl/)
- [TLS 1.3 RFC 8446](https://tools.ietf.org/html/rfc8446)
