# Part 1: SSL/TLS — Architecture & Contexts

## Overview

Envoy's TLS implementation is built on BoringSSL and integrates with the transport socket abstraction layer. TLS is not embedded directly in connection logic — instead, it is a pluggable `TransportSocket` that wraps raw I/O. This allows clean separation between protocol handling (HTTP/1, HTTP/2) and encryption.

## TLS Architecture

```mermaid
graph TB
    subgraph config["Configuration"]
        Proto["DownstreamTlsContext / UpstreamTlsContext (proto config)"]
        SDS["Secret Discovery Service (SDS)"]
    end

    subgraph ctx["Context Layer"]
        SCImpl["ServerContextImpl"]
        CCImpl["ClientContextImpl"]
        CImpl["ContextImpl (base)"]
    end

    subgraph socket["Socket Layer"]
        SSF["ServerSslSocketFactory"]
        CSF["ClientSslSocketFactory"]
        SS["SslSocket"]
    end

    subgraph tsa["Transport Socket Abstraction"]
        TSI["TransportSocket interface"]
        DTSF["DownstreamTransportSocketFactory"]
        UTSF["UpstreamTransportSocketFactory"]
    end

    subgraph conn["Connection"]
        Conn["ConnectionImpl"]
        BIO["IoHandle BIO"]
        IoH["IoHandle (POSIX)"]
    end

    Proto --> SCImpl
    Proto --> CCImpl
    SDS --> SCImpl
    SDS --> CCImpl
    SCImpl --> CImpl
    CCImpl --> CImpl
    SCImpl --> SSF
    CCImpl --> CSF
    SSF --> SS
    CSF --> SS
    SS -.-> TSI
    SSF -.-> DTSF
    CSF -.-> UTSF
    SS --> BIO
    BIO --> IoH
    Conn --> SS
```

## Transport Socket Abstraction

```mermaid
classDiagram
    class TransportSocket {
        <<interface>>
        +doRead(buffer) IoResult
        +doWrite(buffer, end_stream) IoResult
        +onConnected() void
        +ssl() Ssl::ConnectionInfoConstSharedPtr
        +protocol() string
        +closeSocket(close_type) void
        +setTransportSocketCallbacks(callbacks)
    }
    class TransportSocketCallbacks {
        <<interface>>
        +ioHandle() IoHandle
        +connection() Connection
        +raiseEvent(event) void
        +flushWriteBuffer() void
    }
    class DownstreamTransportSocketFactory {
        <<interface>>
        +createDownstreamTransportSocket() TransportSocketPtr
        +implementsSecureTransport() bool
    }
    class UpstreamTransportSocketFactory {
        <<interface>>
        +createTransportSocket(options, host) TransportSocketPtr
        +implementsSecureTransport() bool
    }

    TransportSocket --> TransportSocketCallbacks
    DownstreamTransportSocketFactory --> TransportSocket : "creates"
    UpstreamTransportSocketFactory --> TransportSocket : "creates"

    note for TransportSocket "SslSocket implements this\nRawBufferSocket also implements this\n(for plaintext connections)"
```

### TLS vs Plaintext

```mermaid
graph LR
    subgraph "TLS Connection"
        C1["ConnectionImpl"] --> SS1["SslSocket\n(TransportSocket)"]
        SS1 --> BIO1["IoHandle BIO"]
        BIO1 --> IOH1["IoHandle (syscalls)"]
    end
    
    subgraph "Plaintext Connection"
        C2["ConnectionImpl"] --> RBS["RawBufferSocket\n(TransportSocket)"]
        RBS --> IOH2["IoHandle (syscalls)"]
    end
```

## TLS Context Hierarchy

```mermaid
classDiagram
    class ContextImpl {
        #tls_contexts_ : vector~TlsContext~
        #cert_validator_ : CertValidatorPtr
        #scope_ : Stats::Scope
        +newSsl(options) SslConPtr
        +daysUntilFirstCertExpires() int
        +getCaCertInformation() CertificateDetailsPtr
        +getCertChainInformation() vector~CertificateDetailsPtr~
    }
    class ServerContextImpl {
        -session_ticket_keys_ : vector~ServerSessionTicketKey~
        -ocsp_staple_policy_ : OcspStaplePolicy
        +create(config, factory_context, scope, stats) ServerContextSharedPtr
        -selectTlsContext(callbacks) enum ssl_select_cert_result_t
        -sessionTicketProcess(ssl, key_name, iv, ctx, hmac, encrypt) int
    }
    class ClientContextImpl {
        -server_name_ : string
        -session_keys_ : deque~bssl::UniquePtr~SSL_SESSION~~
        -max_session_keys_ : size_t
        +create(config, factory_context, scope, stats) ClientContextSharedPtr
        -newSessionKey() void
    }
    class TlsContext {
        +ssl_ctx_ : bssl::UniquePtr~SSL_CTX~
        +cert_chain_ : bssl::UniquePtr~X509~
        +ocsp_response_ : vector~uint8_t~
        +is_ecdsa_ : bool
        +is_must_staple_ : bool
        +loadCertificateChain(cert_chain, ctx)
        +loadPrivateKey(key, password)
        +loadPkcs12(pkcs12, password)
    }

    ServerContextImpl --|> ContextImpl
    ClientContextImpl --|> ContextImpl
    ContextImpl --> TlsContext : "one per certificate"
```

### Context Creation Flow

```mermaid
sequenceDiagram
    participant Config as TlsContextConfig
    participant Factory as ServerSslSocketFactory
    participant Ctx as ServerContextImpl
    participant BorSSL as BoringSSL

    Config->>Factory: Constructor(config, context)
    Factory->>Ctx: ServerContextImpl::create(config, ...)
    
    Ctx->>Ctx: ContextImpl constructor
    Ctx->>BorSSL: SSL_CTX_new(TLS_method())
    
    loop For each certificate
        Ctx->>Ctx: TlsContext created
        Ctx->>BorSSL: SSL_CTX_use_certificate_chain_mem()
        Ctx->>BorSSL: SSL_CTX_use_PrivateKey()
        Ctx->>BorSSL: SSL_CTX_set_cipher_list()
        Ctx->>BorSSL: SSL_CTX_set_min_proto_version()
        Ctx->>BorSSL: SSL_CTX_set_max_proto_version()
        Ctx->>BorSSL: SSL_CTX_set_curves()
    end
    
    Ctx->>BorSSL: SSL_CTX_set_select_certificate_cb()
    Note over BorSSL: SNI-based cert selection
    
    Ctx->>BorSSL: SSL_CTX_set_session_id_context()
    Ctx-->>Factory: ServerContextSharedPtr
```

## TLS Socket Factories

```mermaid
classDiagram
    class ServerSslSocketFactory {
        -ssl_ctx_ : ServerContextSharedPtr
        -config_ : ServerContextConfigImpl
        -manager_ : ContextManager
        -stats_ : Stats::Scope
        +createDownstreamTransportSocket() TransportSocketPtr
        +implementsSecureTransport() bool
        -onAddOrUpdateSecret() void
    }
    class ClientSslSocketFactory {
        -ssl_ctx_ : ClientContextSharedPtr
        -config_ : ClientContextConfigImpl
        -manager_ : ContextManager
        +createTransportSocket(options, host) TransportSocketPtr
        +implementsSecureTransport() bool
        -onAddOrUpdateSecret() void
    }

    note for ServerSslSocketFactory "Creates SslSocket for\ndownstream connections\n(listener side)"
    note for ClientSslSocketFactory "Creates SslSocket for\nupstream connections\n(cluster side)"
```

## TLS Context Configuration

### Server (Downstream)

```mermaid
graph TD
    subgraph "DownstreamTlsContext"
        Certs["tls_certificates[]\n→ cert chain + private key"]
        CertProviders["tls_certificate_sds_secret_configs[]\n→ SDS references"]
        Validation["validation_context\n→ CA certs, verify mode"]
        SessionTickets["session_ticket_keys /\nsession_ticket_keys_sds"]
        OCSP["ocsp_staple_policy\n→ MUST_STAPLE / LENIENT"]
        RequireClient["require_client_certificate\n→ mTLS"]
    end
    
    subgraph "CommonTlsContext"
        TLSParams["tls_params\n→ min/max version,\nciphers, curves"]
        ALPN["alpn_protocols\n→ h2, http/1.1"]
        CertProvider["custom_tls_certificate_selector"]
    end
    
    DownstreamTlsContext --> CommonTlsContext
```

### Client (Upstream)

```mermaid
graph TD
    subgraph "UpstreamTlsContext"
        SNI["sni\n→ server name indication"]
        AllowRenegotiation["allow_renegotiation"]
        MaxSessionKeys["max_session_keys\n→ session cache size"]
    end
    
    subgraph "CommonTlsContext"
        Certs["tls_certificates[]\n→ client cert for mTLS"]
        Validation["validation_context\n→ CA certs, SAN matching"]
        TLSParams["tls_params"]
    end
    
    UpstreamTlsContext --> CommonTlsContext
```

## Multiple Certificates and SNI Selection

```mermaid
flowchart TD
    A["Client sends ClientHello\nwith SNI: api.example.com"] --> B["BoringSSL calls\nselect_certificate_cb"]
    B --> C["ServerContextImpl::selectTlsContext()"]
    C --> D{Match SNI against certificates}
    
    D -->|"api.example.com matches\nRSA cert"| E["Select RSA TlsContext"]
    D -->|"api.example.com matches\nECDSA cert"| F["Select ECDSA TlsContext"]
    D -->|"No SNI match"| G["Use default (first) cert"]
    
    E --> H["SSL_set_SSL_CTX(selected)"]
    F --> H
    G --> H
    H --> I["Continue handshake\nwith selected cert"]
```

## Key Source Files

| File | Key Classes | Purpose |
|------|-------------|---------|
| `envoy/network/transport_socket.h` | `TransportSocket`, `TransportSocketCallbacks` | Transport socket interfaces |
| `source/common/tls/context_impl.h/cc` | `ContextImpl`, `TlsContext` | Base TLS context |
| `source/common/tls/server_context_impl.h/cc` | `ServerContextImpl` | Server TLS context |
| `source/common/tls/client_context_impl.h/cc` | `ClientContextImpl` | Client TLS context |
| `source/common/tls/server_ssl_socket.h/cc` | `ServerSslSocketFactory` | Downstream socket factory |
| `source/common/tls/client_ssl_socket.h/cc` | `ClientSslSocketFactory` | Upstream socket factory |
| `source/common/tls/ssl_socket.h/cc` | `SslSocket` | TLS transport socket |
| `source/common/tls/context_config_impl.h/cc` | `ClientContextConfigImpl` | Client config parsing |
| `source/common/tls/server_context_config_impl.h/cc` | `ServerContextConfigImpl` | Server config parsing |
| `source/extensions/transport_sockets/tls/downstream_config.cc` | `DownstreamSslSocketFactory` | Extension registration |
| `source/extensions/transport_sockets/tls/upstream_config.cc` | `UpstreamSslSocketFactory` | Extension registration |

---

**Next:** [Part 2 — Handshake, mTLS, and Certificate Validation](02-handshake-mtls-validation.md)
