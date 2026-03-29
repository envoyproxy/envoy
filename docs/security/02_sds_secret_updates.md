# SDS (Secret Discovery Service) and Dynamic Certificate Rotation

## Overview

Secret Discovery Service (SDS) enables Envoy to dynamically fetch and rotate TLS certificates, private keys, and validation contexts without restarting. SDS uses the xDS protocol to deliver secrets from a secret provider (file-based, Kubernetes secrets, or custom SDS server). This is critical for zero-downtime certificate rotation and automated certificate management (ACME, cert-manager, etc.).

## Architecture

```mermaid
classDiagram
    class SecretManager {
        +map~string,TlsCertificateConfig~ tls_certificate_secrets_
        +map~string,CertificateValidationContext~ validation_context_secrets_
        +map~string,GenericSecret~ generic_secrets_
        +addOrUpdateSecret() void
        +removeSecret() void
        +findTlsCertificate() TlsCertificateConfigPtr
    }

    class SdsApi {
        +Grpc::AsyncClient grpc_client_
        +string sds_config_
        +Subscription subscription_
        +onConfigUpdate() void
        +requestOnDemandUpdate() void
    }

    class TlsCertificateCallback {
        <<interface>>
        +onCertificateUpdate() void
    }

    class CertificateProviderInstance {
        +updateSecret() void
        +notifyCallbacks() void
        +vector~TlsCertificateCallback~ callbacks_
    }

    class ContextManager {
        +updateSslContext() void
        +swapContexts() void
    }

    SecretManager --> SdsApi
    SdsApi --> CertificateProviderInstance
    CertificateProviderInstance --> TlsCertificateCallback
    TlsCertificateCallback <|.. ContextManager
```

## SDS Protocol Flow

```mermaid
sequenceDiagram
    participant Envoy
    participant SDS as SDS Server
    participant CertSource as Certificate Source
    participant Context as TLS Context

    Note over Envoy: Startup

    Envoy->>SDS: DiscoveryRequest (resource: "server-cert")
    Note over Envoy,SDS: gRPC stream established

    SDS->>CertSource: Fetch certificate
    CertSource-->>SDS: Certificate + Private Key

    SDS->>Envoy: DiscoveryResponse
    Note over SDS,Envoy: Contains secret data:<br/>- Certificate chain<br/>- Private key<br/>- Version

    Envoy->>Context: Create initial SSL_CTX
    Context-->>Envoy: Context ready

    Note over Envoy: Listening on port 443

    Note over CertSource: Certificate renewed<br/>(e.g., Let's Encrypt)

    CertSource->>SDS: New certificate available
    SDS->>Envoy: DiscoveryResponse (new version)

    Envoy->>Context: Create new SSL_CTX
    Note over Envoy: Atomic context swap

    Envoy->>Envoy: New connections use new cert
    Note over Envoy: Old connections continue<br/>with old cert until closed

    Envoy->>SDS: ACK (version)
```

## Secret Configuration Types

```mermaid
classDiagram
    class Secret {
        <<abstract>>
        +string name
        +timestamp last_updated
    }

    class TlsCertificate {
        +CertificateChain certificate_chain
        +PrivateKey private_key
        +string password
        +PrivateKeyProvider private_key_provider
    }

    class CertificateValidationContext {
        +DataSource trusted_ca
        +vector~string~ verify_certificate_spki
        +vector~string~ verify_certificate_hash
        +vector~SubjectAltNameMatcher~ match_subject_alt_names
        +bool allow_expired_certificate
        +CRL crl
    }

    class GenericSecret {
        +DataSource secret
    }

    class SessionTicketKeys {
        +vector~DataSource~ keys
    }

    Secret <|-- TlsCertificate
    Secret <|-- CertificateValidationContext
    Secret <|-- GenericSecret
    Secret <|-- SessionTicketKeys
```

## SDS Configuration Flow

```mermaid
flowchart TD
    A[Envoy Configuration] --> B{Secret Source?}

    B -->|Inline/Filename| C[Static Secret]
    C --> C1[Load at startup]
    C1 --> C2[No rotation]

    B -->|SDS| D{SDS Config Type?}

    D -->|File-based| E[File SDS]
    E --> E1[Watch file for changes]
    E1 --> E2[Reload on inotify event]

    D -->|gRPC| F[Remote SDS Server]
    F --> F1[Establish gRPC stream]
    F1 --> F2[Subscribe to secrets]
    F2 --> F3[Receive updates]

    D -->|Kubernetes| G[K8s Secret SDS]
    G --> G1[Watch K8s Secret resource]
    G1 --> G2[Update on K8s change]

    E2 --> H[Trigger Update]
    F3 --> H
    G2 --> H

    H --> I[Validate Secret]
    I --> J{Valid?}
    J -->|No| K[Reject, keep old]
    J -->|Yes| L[Create New Context]
    L --> M[Atomic Swap]
    M --> N[Notify Callbacks]
```

## Configuration Example - File-based SDS

```yaml
listeners:
  - name: https_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 443
    filter_chains:
      - transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificate_sds_secret_configs:
                - name: server_cert
                  sds_config:
                    path_config_source:
                      path: /etc/envoy/sds/server-cert.yaml
                      watched_directory:
                        path: /etc/envoy/sds

# File: /etc/envoy/sds/server-cert.yaml
resources:
  - "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
    name: server_cert
    tls_certificate:
      certificate_chain:
        filename: /etc/ssl/certs/server-cert.pem
      private_key:
        filename: /etc/ssl/private/server-key.pem
```

## Configuration Example - gRPC SDS

```yaml
listeners:
  - name: https_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 443
    filter_chains:
      - transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:

              # TLS certificate from SDS
              tls_certificate_sds_secret_configs:
                - name: server_cert
                  sds_config:
                    api_config_source:
                      api_type: GRPC
                      grpc_services:
                        - envoy_grpc:
                            cluster_name: sds_cluster
                      set_node_on_first_message_only: true

              # CA certificate from SDS
              validation_context_sds_secret_config:
                name: validation_context
                sds_config:
                  api_config_source:
                    api_type: GRPC
                    grpc_services:
                      - envoy_grpc:
                          cluster_name: sds_cluster

clusters:
  - name: sds_cluster
    type: STATIC
    load_assignment:
      cluster_name: sds_cluster
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: sds-server.example.com
                    port_value: 8080
    http2_protocol_options: {}
```

## Secret Update Lifecycle

```mermaid
stateDiagram-v2
    [*] --> Initial: Load config
    Initial --> Subscribing: Create SDS subscription
    Subscribing --> Active: Receive initial secret
    Active --> UpdatePending: New secret received
    UpdatePending --> Validating: Parse & validate
    Validating --> UpdatePending: Validation failed
    Validating --> ContextCreating: Validation passed
    ContextCreating --> ContextSwapping: New SSL_CTX ready
    ContextSwapping --> Active: Atomic swap complete
    Active --> Unsubscribing: Listener removed
    Unsubscribing --> [*]

    note right of Validating
        - Parse certificates
        - Verify private key
        - Check expiration
        - Validate chain
    end note

    note right of ContextSwapping
        - New connections: new context
        - Old connections: old context
        - Old context freed when unused
    end note
```

## Certificate Rotation with Zero Downtime

```mermaid
sequenceDiagram
    participant Client1 as Client 1 (Old)
    participant Client2 as Client 2 (New)
    participant Envoy
    participant OldCtx as Old SSL_CTX
    participant NewCtx as New SSL_CTX
    participant SDS

    Note over Envoy,OldCtx: Active connections<br/>use old certificate

    Client1->>Envoy: Existing TLS connection
    Envoy->>OldCtx: Handle with old cert

    SDS->>Envoy: New certificate update
    Envoy->>NewCtx: Create new SSL_CTX
    Envoy->>Envoy: Atomic pointer swap

    Note over Envoy: Old connections continue<br/>New connections use new cert

    Client2->>Envoy: New TLS connection
    Envoy->>NewCtx: Handle with new cert

    Client1->>OldCtx: Continue existing connection
    Note over Client1,OldCtx: No disruption

    Client1->>Envoy: Close connection
    Envoy->>Envoy: Decrement OldCtx ref count

    Note over OldCtx: When ref count = 0
    OldCtx->>OldCtx: SSL_CTX_free()
```

## File Watch Mechanism

```mermaid
flowchart TD
    A[File SDS Watcher] --> B[Register inotify/kqueue]
    B --> C[Monitor Directory]

    C --> D{File Event?}
    D -->|Create| E[New file detected]
    D -->|Modify| F[File modified]
    D -->|Delete| G[File deleted]
    D -->|Move| H[File renamed]

    E --> I[Read file content]
    F --> I
    H --> I
    G --> J[Log warning]

    I --> K[Parse YAML/JSON]
    K --> L{Valid Secret?}
    L -->|No| M[Log error, keep old]
    L -->|Yes| N[Trigger secret update]

    N --> O[Notify subscribers]
    O --> P[Update TLS contexts]
```

## SDS API Protocol

### DiscoveryRequest

```protobuf
message DiscoveryRequest {
  // Secret resource names
  repeated string resource_names = 3;

  // Type URL: type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret
  string type_url = 1;

  // Version from last response
  string version_info = 2;

  // Node identifier
  Node node = 4;

  // ACK/NACK
  ErrorDetail error_detail = 5;
}
```

### DiscoveryResponse

```protobuf
message DiscoveryResponse {
  // Secret resources
  repeated Any resources = 2;

  // Type URL
  string type_url = 1;

  // New version
  string version_info = 3;

  // Nonce for request/response matching
  string nonce = 4;
}
```

## Secret Update State Machine

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Fetching: Request secret
    Fetching --> Received: DiscoveryResponse
    Received --> Parsing: Parse secret
    Parsing --> Validating: Validation checks
    Validating --> Failed: Invalid
    Validating --> Applying: Valid
    Failed --> Idle: Keep old secret
    Applying --> NotifyingCallbacks: Update contexts
    NotifyingCallbacks --> Idle: Complete
    Idle --> ACKing: Send ACK
    ACKing --> Idle
    Failed --> NACKing: Send NACK
    NACKing --> Idle

    note right of Validating
        - Certificate format
        - Private key format
        - Key matches certificate
        - Chain validity
        - Expiration dates
    end note
```

## Kubernetes Secrets Integration

```yaml
apiVersion: v1
kind: Secret
metadata:
  name: envoy-tls-cert
  namespace: default
type: kubernetes.io/tls
data:
  tls.crt: <base64-encoded-cert>
  tls.key: <base64-encoded-key>

---
# Envoy configuration to use K8s secret
apiVersion: v1
kind: ConfigMap
metadata:
  name: envoy-config
data:
  envoy.yaml: |
    static_resources:
      listeners:
        - name: https_listener
          address:
            socket_address:
              address: 0.0.0.0
              port_value: 443
          filter_chains:
            - transport_socket:
                name: envoy.transport_sockets.tls
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
                  common_tls_context:
                    tls_certificate_sds_secret_configs:
                      - name: kubernetes://default/envoy-tls-cert
                        sds_config:
                          resource_api_version: V3
                          ads: {}

    # Use ADS for secret discovery
    dynamic_resources:
      ads_config:
        api_type: GRPC
        grpc_services:
          - envoy_grpc:
              cluster_name: xds_cluster
```

## Secret Validation

```mermaid
flowchart TD
    A[Receive Secret Update] --> B[Extract Certificate Chain]
    B --> C{Valid PEM Format?}
    C -->|No| Z[Reject Update]
    C -->|Yes| D[Parse Certificates]

    D --> E{Certificate Chain<br/>Valid?}
    E -->|No| Z
    E -->|Yes| F[Extract Private Key]

    F --> G{Valid Key Format?}
    G -->|No| Z
    G -->|Yes| H[Parse Private Key]

    H --> I{Key Type Matches<br/>Certificate?}
    I -->|No| Z
    I -->|Yes| J[Verify Key<br/>Matches Certificate]

    J --> K{Public Key Match?}
    K -->|No| Z
    K -->|Yes| L{Check Certificate<br/>Expiration}

    L --> M{Expired?}
    M -->|Yes| N{allow_expired?}
    N -->|No| Z
    N -->|Yes| O[Accept Update]
    M -->|No| O

    O --> P[Create New SSL_CTX]
    P --> Q[Test TLS Handshake]
    Q --> R{Success?}
    R -->|No| Z
    R -->|Yes| S[Apply Update]
```

## Monitoring Certificate Expiration

```mermaid
sequenceDiagram
    participant Envoy
    participant Cert as Certificate
    participant Metrics as Metrics System
    participant Alert as Alerting

    loop Every 1 hour
        Envoy->>Cert: Check expiration
        Cert-->>Envoy: Expires in N days

        alt Expiring Soon (< 30 days)
            Envoy->>Metrics: Update metric:<br/>tls_cert_days_until_expiration = N
            Metrics->>Alert: Check threshold
            alt Critical (< 7 days)
                Alert->>Alert: Trigger alert
            end
        end
    end
```

## Statistics

```yaml
# Certificate update stats
sds.server_cert.update_attempt
sds.server_cert.update_success
sds.server_cert.update_failure
sds.server_cert.update_rejected

# Secret fetch stats
sds.server_cert.config_reload
sds.server_cert.update_duration

# Certificate expiration
listener.0.0.0.0_443.ssl.days_until_first_cert_expiring

# Context update stats
listener.0.0.0.0_443.ssl.context_config_update_by_sds
```

## Best Practices

### 1. Certificate Rotation Strategy

```yaml
# Overlapping validity periods
# Old cert: Valid until Day 90
# New cert: Issued on Day 60, valid from Day 60

# SDS updates on Day 60:
# - Some clients see old cert (valid)
# - Some clients see new cert (valid)
# - No downtime
```

### 2. File-based SDS with Atomic Writes

```bash
# BAD: Direct write (can read partial file)
echo "new-content" > /etc/envoy/sds/cert.yaml

# GOOD: Atomic write using temp file + rename
echo "new-content" > /etc/envoy/sds/cert.yaml.tmp
mv /etc/envoy/sds/cert.yaml.tmp /etc/envoy/sds/cert.yaml
```

### 3. Validation Before Update

```yaml
# Pre-validate certificates before pushing to SDS
validation_context:
  trust_chain_verification: VERIFY_TRUST_CHAIN
  # This ensures invalid certs are rejected
```

### 4. Monitor Certificate Expiration

```yaml
# Set up alerts
alert: TLSCertificateExpiringSoon
expr: envoy_ssl_days_until_first_cert_expiring < 30
annotations:
  summary: TLS certificate expiring in {{ $value }} days
```

### 5. Graceful Rollback

```yaml
# Keep backup of previous certificate
# If new cert causes issues, quickly roll back via SDS update
```

## ACME Integration (Let's Encrypt)

```mermaid
sequenceDiagram
    participant ACME as ACME Client
    participant LE as Let's Encrypt
    participant FS as Filesystem
    participant SDS as File SDS Watcher
    participant Envoy

    Note over ACME: Certificate renewal triggered

    ACME->>LE: Request certificate renewal
    LE->>ACME: Challenge (HTTP-01/DNS-01)
    ACME->>ACME: Complete challenge
    ACME->>LE: Prove domain ownership
    LE->>ACME: Issue new certificate

    ACME->>FS: Write new cert (atomic)
    FS->>SDS: inotify event
    SDS->>Envoy: Secret update
    Envoy->>Envoy: Rotate certificate

    Note over Envoy: Zero downtime rotation
```

## Troubleshooting

### Check Secret Status

```bash
# View loaded secrets
curl http://localhost:9901/config_dump | jq '.configs[] | select(.["@type"] | contains("SecretsConfigDump"))'

# Check certificate details
curl http://localhost:9901/certs

# View SDS stats
curl http://localhost:9901/stats | grep sds
```

### Common Issues

1. **Secret not updating**
   - Check file permissions and SDS watcher
   - Verify inotify limits: `cat /proc/sys/fs/inotify/max_user_watches`
   - Check SDS server connectivity

2. **Certificate validation failed**
   - Verify private key matches certificate
   - Check certificate chain order (leaf first)
   - Ensure proper PEM format

3. **Old context not released**
   - Check for long-lived connections
   - Monitor `ssl.context_config_update_by_sds` stat

## References

- [Envoy SDS Documentation](https://www.envoyproxy.io/docs/envoy/latest/configuration/security/secret)
- [xDS Protocol](https://www.envoyproxy.io/docs/envoy/latest/api-docs/xds_protocol)
- [cert-manager](https://cert-manager.io/)
- [ACME Protocol](https://tools.ietf.org/html/rfc8555)
