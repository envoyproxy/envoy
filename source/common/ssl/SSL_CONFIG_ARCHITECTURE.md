# Envoy SSL/TLS Configuration Architecture

## Overview

The Envoy SSL/TLS configuration subsystem manages the setup and validation of TLS certificates and certificate validation contexts. It provides a flexible configuration layer that handles certificate chains, private keys, certificate validation, and various TLS-related settings including OCSP stapling, SPKI pinning, and custom validators.

---

## System Architecture

```mermaid
graph TB
    subgraph "Configuration Layer"
        ProtoConfig[Protobuf Configuration<br/>TLS v3 API]
    end

    subgraph "Certificate Management"
        TLSCertConfig[TlsCertificateConfig<br/>Certificate & Private Key]
        CertChain[Certificate Chain]
        PrivateKey[Private Key / PKCS12]
        OCSPStaple[OCSP Staple]
        PrivKeyMethod[Private Key Method Provider]
    end

    subgraph "Validation Management"
        CertValConfig[CertificateValidationContextConfig<br/>Peer Validation]
        TrustedCA[Trusted CA Certificates]
        CRL[Certificate Revocation List]
        SANMatchers[Subject Alt Name Matchers]
        CertPinning[Certificate Pinning<br/>Hash / SPKI]
    end

    subgraph "Matching System"
        MatchingInputs[SSL Matching Inputs]
        UriSan[URI SAN Matcher]
        DnsSan[DNS SAN Matcher]
        Subject[Subject Matcher]
    end

    ProtoConfig --> TLSCertConfig
    ProtoConfig --> CertValConfig

    TLSCertConfig --> CertChain
    TLSCertConfig --> PrivateKey
    TLSCertConfig --> OCSPStaple
    TLSCertConfig --> PrivKeyMethod

    CertValConfig --> TrustedCA
    CertValConfig --> CRL
    CertValConfig --> SANMatchers
    CertValConfig --> CertPinning

    CertValConfig --> MatchingInputs
    MatchingInputs --> UriSan
    MatchingInputs --> DnsSan
    MatchingInputs --> Subject

    style TLSCertConfig fill:#e1f5ff
    style CertValConfig fill:#ffe1e1
    style MatchingInputs fill:#e1ffe1
```

---

## 1. Core Components

### Component Hierarchy

```mermaid
classDiagram
    class TlsCertificateConfig {
        <<interface>>
        +certificateChain() string
        +certificateChainPath() string
        +certificateName() string
        +privateKey() string
        +privateKeyPath() string
        +pkcs12() string
        +password() string
        +ocspStaple() vector~uint8_t~
        +privateKeyMethod() PrivateKeyMethodProvider
    }

    class TlsCertificateConfigImpl {
        -certificate_chain_: string
        -certificate_chain_path_: string
        -certificate_name_: string
        -private_key_: string
        -private_key_path_: string
        -pkcs12_: string
        -password_: string
        -ocsp_staple_: vector~uint8_t~
        -private_key_method_: PrivateKeyMethodProviderSharedPtr
        +create(config, factory_context, api, name) StatusOr~TlsCertificateConfigImpl~
    }

    class CertificateValidationContextConfig {
        <<interface>>
        +caCert() string
        +caCertPath() string
        +certificateRevocationList() string
        +subjectAltNameMatchers() vector~SubjectAltNameMatcher~
        +verifyCertificateHashList() vector~string~
        +verifyCertificateSpkiList() vector~string~
        +allowExpiredCertificate() bool
        +trustChainVerification() TrustChainVerification
        +customValidatorConfig() TypedExtensionConfig
    }

    class CertificateValidationContextConfigImpl {
        -ca_cert_: string
        -ca_cert_path_: string
        -certificate_revocation_list_: string
        -subject_alt_name_matchers_: vector
        -verify_certificate_hash_list_: vector~string~
        -verify_certificate_spki_list_: vector~string~
        -allow_expired_certificate_: bool
        -trust_chain_verification_: TrustChainVerification
        -custom_validator_config_: optional~TypedExtensionConfig~
        -only_verify_leaf_cert_crl_: bool
        -max_verify_depth_: optional~uint32_t~
        -auto_sni_san_match_: bool
        +create(context, auto_sni_san_match, api, name) StatusOr~unique_ptr~
        +initialize() Status
    }

    TlsCertificateConfig <|.. TlsCertificateConfigImpl
    CertificateValidationContextConfig <|.. CertificateValidationContextConfigImpl

    note for TlsCertificateConfigImpl "Manages server/client certificates\nSupports PEM and PKCS12 formats"
    note for CertificateValidationContextConfigImpl "Manages peer certificate validation\nSupports CA, CRL, and custom validators"
```

---

## 2. TLS Certificate Configuration

### Certificate Loading Strategies

```mermaid
flowchart TD
    Start[Load TLS Certificate] --> CheckPKCS{Has PKCS12?}

    CheckPKCS -->|Yes| ValidatePKCS[Validate PKCS12 config]
    CheckPKCS -->|No| ValidatePEM[Validate PEM config]

    ValidatePKCS --> CheckConflict1{Has private_key?}
    CheckConflict1 -->|Yes| Error1[Error: Can't have both]
    CheckConflict1 -->|No| CheckConflict2{Has certificate_chain?}

    CheckConflict2 -->|Yes| Error2[Error: Can't have both]
    CheckConflict2 -->|No| CheckConflict3{Has private_key_provider?}

    CheckConflict3 -->|Yes| Error3[Error: Can't have both]
    CheckConflict3 -->|No| LoadPKCS[Load PKCS12 bundle]

    ValidatePEM --> CheckPrivKey{Has private_key<br/>or provider?}
    CheckPrivKey -->|No| Error4[Error: Private key required]
    CheckPrivKey -->|Yes| CheckCertChain{Has certificate_chain?}

    CheckCertChain -->|No| Error5[Error: Cert chain required]
    CheckCertChain -->|Yes| LoadPEM[Load PEM files]

    LoadPKCS --> LoadPassword[Load password if present]
    LoadPEM --> CheckProvider{Has private_key_provider?}

    CheckProvider -->|Yes| InitProvider[Initialize Private Key Method]
    CheckProvider -->|No| LoadPrivateKey[Load private key directly]

    InitProvider --> CheckAvailable{Provider available?}
    CheckAvailable -->|No & no fallback| Error6[Error: Provider unavailable]
    CheckAvailable -->|No & fallback| LoadPrivateKey
    CheckAvailable -->|Yes| LoadCertChain

    LoadPrivateKey --> LoadCertChain[Load certificate chain]
    LoadPassword --> LoadOCSP
    LoadCertChain --> LoadOCSP[Load OCSP staple if present]

    LoadOCSP --> Success[Configuration Ready]

    style Success fill:#ccffcc
    style Error1 fill:#ffcccc
    style Error2 fill:#ffcccc
    style Error3 fill:#ffcccc
    style Error4 fill:#ffcccc
    style Error5 fill:#ffcccc
    style Error6 fill:#ffcccc
```

### Certificate Data Sources

```mermaid
graph TB
    subgraph "Data Source Types"
        InlineBytes[Inline Bytes<br/>Direct content in config]
        InlineString[Inline String<br/>Text content in config]
        Filename[Filename<br/>Path to file on disk]
        EnvVar[Environment Variable<br/>Read from env]
    end

    subgraph "Certificate Components"
        CertChain[Certificate Chain]
        PrivKey[Private Key]
        PKCS12[PKCS12 Bundle]
        Password[Password]
        OCSP[OCSP Staple]
    end

    InlineBytes --> CertChain
    InlineBytes --> PrivKey
    InlineBytes --> PKCS12
    InlineBytes -.->|Not allowed| OCSP

    InlineString --> Password
    InlineString -.->|Deprecated| CertChain
    InlineString -.->|Deprecated| PrivKey

    Filename --> CertChain
    Filename --> PrivKey
    Filename --> PKCS12
    Filename --> Password
    Filename --> OCSP

    EnvVar --> Password
    EnvVar --> CertChain
    EnvVar --> PrivKey

    style OCSP fill:#ffffcc
    style InlineBytes fill:#ccffcc
```

### Certificate Configuration Fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| **certificate_chain** | DataSource | Yes* | PEM-encoded certificate chain |
| **private_key** | DataSource | Yes* | PEM-encoded private key |
| **pkcs12** | DataSource | No | PKCS12 bundle (alternative to PEM) |
| **password** | DataSource | No | Password for encrypted key/PKCS12 |
| **ocsp_staple** | DataSource | No | OCSP stapling response (must be filename) |
| **private_key_provider** | PrivateKeyProvider | No | Hardware/software key provider |

\* Either (certificate_chain + private_key) or pkcs12 is required

---

## 3. Certificate Validation Context

### Validation Configuration

```mermaid
flowchart TD
    Start[Initialize Validation Context] --> LoadCA[Load Trusted CA]

    LoadCA --> CheckCA{CA cert empty?}

    CheckCA -->|Yes| CheckCustom{Has custom<br/>validator?}
    CheckCA -->|No| ValidateCA[Validate CA configuration]

    CheckCustom -->|Yes| AllowEmpty[Allow empty CA]
    CheckCustom -->|No| CheckCRL{Has CRL?}

    CheckCRL -->|Yes| Error1[Error: CRL needs CA]
    CheckCRL -->|No| CheckSAN{Has SAN matchers?}

    CheckSAN -->|Yes| Error2[Error: SAN needs CA<br/>insecure without CA]
    CheckSAN -->|No| CheckExpired{Allow expired?}

    CheckExpired -->|Yes| Error3[Error: No CA but allow expired?]
    CheckExpired -->|No| AllowEmpty

    ValidateCA --> LoadCRL[Load CRL if present]
    LoadCRL --> ParseSAN[Parse SAN matchers]
    ParseSAN --> LoadHashes[Load certificate hashes]
    LoadHashes --> LoadSPKI[Load SPKI pins]
    LoadSPKI --> ConfigCustom[Configure custom validator]

    AllowEmpty --> Success[Validation context ready]
    ConfigCustom --> Success

    style Success fill:#ccffcc
    style Error1 fill:#ffcccc
    style Error2 fill:#ffcccc
    style Error3 fill:#ffcccc
```

### Subject Alternative Name (SAN) Matching

```mermaid
graph TB
    subgraph "SAN Matcher Types"
        DNS[DNS SAN<br/>example.com]
        URI[URI SAN<br/>spiffe://cluster/service]
        Email[Email SAN<br/>user@example.com]
        IP[IP Address SAN<br/>192.0.2.1]
        OtherName[Other Name SAN<br/>Custom OIDs]
    end

    subgraph "Matching Strategies"
        Exact[Exact Match]
        Prefix[Prefix Match]
        Suffix[Suffix Match]
        Contains[Contains Match]
        Regex[Regex Match]
    end

    DNS --> Exact
    DNS --> Suffix
    DNS --> Regex

    URI --> Exact
    URI --> Prefix
    URI --> Regex

    Email --> Exact
    Email --> Contains

    IP --> Exact

    OtherName --> Exact

    style DNS fill:#e1f5ff
    style URI fill:#ffe1e1
    style Email fill:#e1ffe1
    style IP fill:#ffe1ff
```

### Validation Context Fields

| Field | Type | Description |
|-------|------|-------------|
| **trusted_ca** | DataSource | PEM-encoded CA certificates |
| **crl** | DataSource | Certificate Revocation List |
| **match_typed_subject_alt_names** | SubjectAltNameMatcher[] | Typed SAN matchers |
| **verify_certificate_hash** | string[] | SHA-256 cert fingerprints |
| **verify_certificate_spki** | string[] | SHA-256 SPKI pins |
| **allow_expired_certificate** | bool | Accept expired certs |
| **trust_chain_verification** | enum | VERIFY_TRUST_CHAIN / ACCEPT_UNTRUSTED |
| **custom_validator_config** | TypedExtensionConfig | Custom validation extension |
| **only_verify_leaf_cert_crl** | bool | Skip intermediate CRL checks |
| **max_verify_depth** | uint32 | Maximum chain depth |

---

## 4. Certificate Pinning

### Pinning Strategies

```mermaid
graph TB
    subgraph "Certificate Hash Pinning"
        CertHash[Full Certificate Hash<br/>SHA-256 of entire cert]
        LeafOnly1[Pins specific certificate]
        Renewal1[Must update on renewal]
    end

    subgraph "SPKI Pinning (Recommended)"
        SPKI[Subject Public Key Info<br/>SHA-256 of public key]
        AnyKey[Works across cert renewals]
        KeyRotation[Update only on key rotation]
    end

    subgraph "Validation Process"
        Extract[Extract from peer cert]
        Hash[Compute SHA-256]
        Compare[Compare with configured pins]
        Result{Match found?}
    end

    CertHash --> Extract
    SPKI --> Extract
    Extract --> Hash
    Hash --> Compare
    Compare --> Result

    Result -->|Yes| Accept[Accept connection]
    Result -->|No| Reject[Reject connection]

    style SPKI fill:#ccffcc
    style Accept fill:#ccffcc
    style Reject fill:#ffcccc
```

### Example Configuration

```yaml
# Certificate Hash Pinning
validation_context:
  verify_certificate_hash:
    - "E3:B0:C4:42:98:FC:1C:14:9A:FB:F4:C8:99:6F:B9:24:27:AE:41:E4:64:9B:93:4C:A4:95:99:1B:78:52:B8:55"

# SPKI Pinning (Recommended)
validation_context:
  verify_certificate_spki:
    - "NIdniy8pK1mhYTLdAp1vXL5wJXMQweHAWwVrHxMJ3Y8="
    - "qKzHZAr7BLAkiDcU+sIz1N9JpmFMcHUJJcnL8IuPJ0Y="
```

---

## 5. Private Key Methods

### Private Key Provider Architecture

```mermaid
sequenceDiagram
    participant Config as TLS Config
    participant Factory as Private Key Method Manager
    participant Provider as Private Key Provider
    participant HSM as Hardware Security Module

    Config->>Factory: Load private_key_provider
    Factory->>Factory: Lookup provider by name

    alt Provider found
        Factory->>Provider: Create provider instance
        Provider->>Provider: Initialize

        alt Provider available
            Provider->>HSM: Connect to HSM/TPM
            HSM-->>Provider: Connection established
            Provider-->>Factory: Provider ready
            Factory-->>Config: Use provider
        else Provider unavailable
            alt Fallback enabled
                Config->>Config: Use software key instead
            else No fallback
                Factory-->>Config: Error: Provider unavailable
            end
        end
    else Provider not found
        Factory-->>Config: Error: Unknown provider
    end
```

### Provider Use Cases

```mermaid
graph LR
    subgraph "Software Providers"
        InMemory[In-Memory Key<br/>Default software crypto]
    end

    subgraph "Hardware Providers"
        HSM[Hardware Security Module<br/>PKCS#11, CloudHSM]
        TPM[Trusted Platform Module<br/>TPM 2.0]
        Enclave[Secure Enclave<br/>SGX, SEV]
    end

    subgraph "Cloud Providers"
        KMS[Key Management Service<br/>AWS KMS, Azure Key Vault]
        CloudKey[Cloud KMS<br/>GCP Cloud KMS]
    end

    InMemory --> LocalSigning[Local Signing Operations]
    HSM --> SecureSigning[Hardware-backed Signing]
    TPM --> PlatformSigning[Platform-backed Signing]
    Enclave --> IsolatedSigning[Isolated Signing]
    KMS --> CloudSigning[Cloud-based Signing]
    CloudKey --> CloudSigning

    style HSM fill:#e1f5ff
    style KMS fill:#ffe1e1
```

---

## 6. SSL Matching System

### Matching Input Types

```mermaid
classDiagram
    class DataInput~MatchingDataType~ {
        <<interface>>
        +get(data) DataInputGetResult
    }

    class UriSanInput {
        +get(data) DataInputGetResult
    }

    class DnsSanInput {
        +get(data) DataInputGetResult
    }

    class SubjectInput {
        +get(data) DataInputGetResult
    }

    class DataInputGetResult {
        +availability: DataAvailability
        +data: variant~string, monostate~
    }

    class DataAvailability {
        <<enumeration>>
        AllDataAvailable
        NotAvailable
    }

    DataInput <|.. UriSanInput
    DataInput <|.. DnsSanInput
    DataInput <|.. SubjectInput

    UriSanInput --> DataInputGetResult
    DnsSanInput --> DataInputGetResult
    SubjectInput --> DataInputGetResult

    DataInputGetResult --> DataAvailability
```

### Matching Flow

```mermaid
sequenceDiagram
    participant Matcher as Filter Chain Matcher
    participant Input as SSL Input
    participant Connection as SSL Connection
    participant Cert as Peer Certificate

    Matcher->>Input: get(matching_data)
    Input->>Connection: Check SSL available

    alt SSL not available
        Input-->>Matcher: NotAvailable
    else SSL available
        Input->>Cert: Extract data

        alt URI SAN Input
            Cert->>Cert: Get uriSanPeerCertificate()
            Cert-->>Input: URI list
            Input->>Input: Join with comma
        else DNS SAN Input
            Cert->>Cert: Get dnsSansPeerCertificate()
            Cert-->>Input: DNS list
            Input->>Input: Join with comma
        else Subject Input
            Cert->>Cert: Get subjectPeerCertificate()
            Cert-->>Input: Subject string
        end

        alt Data present
            Input-->>Matcher: AllDataAvailable, data
        else No data
            Input-->>Matcher: AllDataAvailable, monostate
        end
    end

    Matcher->>Matcher: Apply matching logic
```

### Use Cases

| Input Type | Use Case | Example Value |
|-----------|----------|---------------|
| **URI SAN** | Service identity (SPIFFE) | `spiffe://cluster.local/ns/default/sa/web` |
| **DNS SAN** | Hostname matching | `api.example.com,*.example.com` |
| **Subject** | Legacy DN matching | `CN=server.example.com,O=Example Inc,C=US` |

---

## 7. OCSP Stapling

### OCSP Stapling Flow

```mermaid
sequenceDiagram
    participant Server as TLS Server
    participant OCSP as OCSP Responder
    participant Client as TLS Client

    Note over Server: Server startup

    Server->>Server: Load certificate

    alt OCSP staple configured
        Server->>Server: Load stapled response from file
    else No staple configured
        Server->>OCSP: Request OCSP response
        OCSP-->>Server: OCSP response
    end

    Server->>Server: Cache OCSP response

    Note over Client: Client connects

    Client->>Server: ClientHello (status_request)
    Server->>Client: ServerHello + Certificate
    Server->>Client: CertificateStatus (OCSP response)

    Client->>Client: Verify OCSP response
    Client->>Client: Check certificate revocation status

    alt Certificate valid
        Client->>Server: Continue handshake
    else Certificate revoked
        Client->>Server: Abort connection
    end
```

### OCSP Configuration

```yaml
tls_certificates:
  - certificate_chain:
      filename: /path/to/cert.pem
    private_key:
      filename: /path/to/key.pem
    ocsp_staple:
      filename: /path/to/ocsp_response.der
```

**Restrictions:**
- OCSP staple **must** be from a file (not inline)
- Response is DER-encoded
- Server caches response (no automatic refresh in this layer)

---

## 8. Deprecated Features Handling

### SAN Matcher Migration

```mermaid
flowchart TD
    Start[Parse SAN Matchers] --> CheckTyped{Has typed matchers?}

    CheckTyped -->|Yes| UseTyped[Use match_typed_subject_alt_names]
    CheckTyped -->|No| CheckDeprecated{Has deprecated<br/>matchers?}

    UseTyped --> CheckBoth{Has deprecated too?}
    CheckBoth -->|Yes| WarnIgnore[WARN: Ignoring deprecated field]
    CheckBoth -->|No| Return1[Return typed matchers]

    CheckDeprecated -->|No| Return2[Return empty list]
    CheckDeprecated -->|Yes| Expand[Expand to all SAN types]

    WarnIgnore --> Return1

    Expand --> CreateDNS[Create DNS matcher]
    CreateDNS --> CreateURI[Create URI matcher]
    CreateURI --> CreateEmail[Create Email matcher]
    CreateEmail --> CreateIP[Create IP matcher]

    CreateIP --> Return3[Return expanded matchers]

    style UseTyped fill:#ccffcc
    style Expand fill:#ffffcc
    style WarnIgnore fill:#ffcccc
```

### Deprecated Fields

| Deprecated Field | Replacement | Migration Path |
|-----------------|-------------|----------------|
| **match_subject_alt_names** | match_typed_subject_alt_names | Specify explicit SAN type per matcher |
| **inline_string** (OCSP) | filename | Move OCSP response to file |

---

## 9. Error Handling

### Validation Errors

```mermaid
flowchart TD
    Start[Configuration Error] --> Type{Error Type}

    Type -->|Missing CA| CRLError{Has CRL?}
    Type -->|Missing CA| SANError{Has SAN matchers?}
    Type -->|Missing CA| ExpiredError{Allow expired?}
    Type -->|PKCS12 Conflict| CheckPrivKey{Has private_key?}
    Type -->|PKCS12 Conflict| CheckCertChain{Has cert_chain?}
    Type -->|Missing Cert| NoCertChain[Certificate chain required]
    Type -->|Missing Key| NoPrivKey[Private key required]
    Type -->|Provider Failed| ProviderError[Provider unavailable]
    Type -->|OCSP Error| OCSPInline[OCSP must be from file]

    CRLError -->|Yes| Error1[Error: CRL requires trusted CA]
    SANError -->|Yes| Error2[Error: SAN matching insecure without CA]
    ExpiredError -->|Yes| Error3[Error: Allow expired meaningless without CA]
    CheckPrivKey -->|Yes| Error4[Error: Can't have both PKCS12 and private_key]
    CheckCertChain -->|Yes| Error5[Error: Can't have both PKCS12 and cert_chain]

    style Error1 fill:#ffcccc
    style Error2 fill:#ffcccc
    style Error3 fill:#ffcccc
    style Error4 fill:#ffcccc
    style Error5 fill:#ffcccc
```

### Common Error Messages

| Error | Cause | Resolution |
|-------|-------|------------|
| **CRL without trusted CA** | CRL configured but no CA cert | Add trusted_ca or remove CRL |
| **SAN matching insecure** | SAN matchers without CA | Add trusted_ca for secure validation |
| **PKCS12 conflicts** | PKCS12 with PEM fields | Use either PKCS12 or PEM, not both |
| **Missing certificate** | No cert_chain configured | Provide certificate_chain |
| **Missing private key** | No key or provider | Provide private_key or provider |
| **OCSP inline error** | OCSP from inline_string | Use filename for OCSP staple |
| **Provider unavailable** | HSM/TPM not accessible | Enable fallback or fix provider |

---

## 10. Configuration Examples

### Basic TLS Server

```yaml
transport_socket:
  name: envoy.transport_sockets.tls
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
    common_tls_context:
      tls_certificates:
        - certificate_chain:
            filename: /etc/certs/server-cert.pem
          private_key:
            filename: /etc/certs/server-key.pem
```

### Mutual TLS (mTLS)

```yaml
common_tls_context:
  tls_certificates:
    - certificate_chain:
        filename: /etc/certs/server-cert.pem
      private_key:
        filename: /etc/certs/server-key.pem

  validation_context:
    trusted_ca:
      filename: /etc/certs/ca-cert.pem
    match_typed_subject_alt_names:
      - san_type: URI
        matcher:
          exact: "spiffe://cluster.local/ns/default/sa/client"
```

### Certificate Pinning

```yaml
validation_context:
  trusted_ca:
    filename: /etc/certs/ca-cert.pem
  # SPKI Pinning (recommended)
  verify_certificate_spki:
    - "NIdniy8pK1mhYTLdAp1vXL5wJXMQweHAWwVrHxMJ3Y8="
    - "qKzHZAr7BLAkiDcU+sIz1N9JpmFMcHUJJcnL8IuPJ0Y="
```

### Hardware Security Module

```yaml
tls_certificates:
  - certificate_chain:
      filename: /etc/certs/server-cert.pem
    private_key_provider:
      provider_name: pkcs11
      fallback: true
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.private_key_providers.pkcs11.v3.Pkcs11Config
        library_path: /usr/lib/libpkcs11.so
        key_id: "01:02:03:04"
```

### OCSP Stapling

```yaml
tls_certificates:
  - certificate_chain:
      filename: /etc/certs/server-cert.pem
    private_key:
      filename: /etc/certs/server-key.pem
    ocsp_staple:
      filename: /etc/certs/ocsp-response.der
```

---

## 11. Security Best Practices

### Certificate Management

```mermaid
graph TB
    subgraph "Certificate Lifecycle"
        Generate[Generate Key Pair]
        CSR[Create CSR]
        Sign[Sign by CA]
        Deploy[Deploy Certificate]
        Monitor[Monitor Expiration]
        Renew[Renew Before Expiry]
        Rotate[Rotate Keys]
    end

    subgraph "Key Management"
        HSM[Use HSM for production]
        Permissions[Restrict file permissions]
        NoInline[Avoid inline secrets]
        Rotate2[Regular key rotation]
    end

    subgraph "Validation"
        TrustedCA[Use trusted CA]
        CRL2[Enable CRL checking]
        OCSP2[Enable OCSP stapling]
        Pinning[Use SPKI pinning]
    end

    Generate --> CSR
    CSR --> Sign
    Sign --> Deploy
    Deploy --> Monitor
    Monitor --> Renew
    Renew --> Rotate

    style HSM fill:#ccffcc
    style TrustedCA fill:#ccffcc
    style Pinning fill:#ccffcc
```

### Recommendations

1. **Private Keys**
   - Store in HSM/TPM for production
   - Use file permissions (0400/0600)
   - Never use inline_string for keys
   - Rotate regularly (e.g., annually)

2. **Certificates**
   - Use strong algorithms (RSA 2048+, EC P-256+)
   - Enable OCSP stapling
   - Monitor expiration (30 days warning)
   - Automate renewal

3. **Validation**
   - Always validate peer certificates
   - Use SPKI pinning for critical services
   - Enable CRL checking
   - Set appropriate max_verify_depth

4. **Configuration**
   - Use typed SAN matchers
   - Avoid allow_expired_certificate in production
   - Set trust_chain_verification appropriately
   - Use custom validators for special requirements

---

## Summary

The Envoy SSL/TLS configuration subsystem provides:

1. **Flexible Certificate Management**
   - PEM and PKCS12 formats
   - Multiple data sources (file, inline, env)
   - OCSP stapling support
   - Private key method providers (HSM/TPM)

2. **Comprehensive Validation**
   - CA certificate trust chains
   - Certificate Revocation Lists (CRL)
   - Subject Alternative Name (SAN) matching
   - Certificate pinning (hash and SPKI)
   - Custom validator extensions

3. **Advanced Features**
   - SSL-based filter chain matching
   - Auto SNI-SAN matching
   - Configurable trust chain verification
   - Leaf-only CRL checking
   - Maximum verification depth control

4. **Production Ready**
   - Extensive error validation
   - Secure defaults
   - Hardware security module support
   - Migration path for deprecated features
   - Clear error messages

5. **Integration Points**
   - Transport socket factories
   - Filter chain selection
   - Custom certificate validators
   - Private key providers
   - OCSP responders

This architecture enables Envoy to support diverse TLS requirements from simple HTTPS to complex mTLS with hardware-backed keys, certificate pinning, and custom validation logic.
