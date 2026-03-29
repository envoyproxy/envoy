# ALPN & SNI Handling and Filter Chain Matching

## Overview

Application-Layer Protocol Negotiation (ALPN) and Server Name Indication (SNI) are TLS extensions that Envoy uses to route traffic to the appropriate filter chain. SNI allows hosting multiple TLS domains on a single IP address, while ALPN enables protocol negotiation (HTTP/1.1, HTTP/2, HTTP/3). Envoy's filter chain matching is a powerful mechanism that selects the appropriate configuration based on connection properties detected during the TLS handshake.

## Architecture

```mermaid
classDiagram
    class Listener {
        +Address address_
        +vector~FilterChain~ filter_chains_
        +FilterChain default_filter_chain_
        +FilterChainManager filter_chain_manager_
        +findFilterChain() FilterChain*
    }

    class FilterChainManager {
        +FilterChainMap filter_chain_map_
        +findFilterChain() FilterChain*
    }

    class FilterChainMatch {
        +vector~CidrRange~ source_prefix_ranges_
        +vector~uint32~ source_ports_
        +vector~string~ server_names_
        +string transport_protocol_
        +vector~string~ application_protocols_
        +vector~CidrRange~ direct_source_prefix_ranges_
    }

    class FilterChain {
        +FilterChainMatch filter_chain_match_
        +vector~NetworkFilter~ filters_
        +TransportSocket transport_socket_
        +string name_
    }

    class TlsInspector {
        +inspectClientHello() void
        +extractSNI() string
        +extractALPN() vector~string~
    }

    class ConnectionMatcher {
        +matchConnection() FilterChain*
    }

    Listener --> FilterChainManager
    FilterChainManager --> FilterChain
    FilterChain --> FilterChainMatch
    Listener --> TlsInspector
    FilterChainManager --> ConnectionMatcher
```

## Filter Chain Matching Flow

```mermaid
flowchart TD
    A[New Connection] --> B[TLS Inspector]
    B --> C{TLS Traffic?}
    C -->|No| D[Use transport_protocol:<br/>raw_buffer]
    C -->|Yes| E[Parse ClientHello]

    E --> F[Extract SNI]
    E --> G[Extract ALPN]
    E --> H[Get Source IP]
    E --> I[Get Destination Port]

    F --> J[Build Match Context]
    G --> J
    H --> J
    I --> J

    J --> K[FilterChainManager:<br/>findFilterChain]

    K --> L{Match Algorithm}

    L --> M[Check server_names<br/>SNI]
    M --> N[Check application_protocols<br/>ALPN]
    N --> O[Check source_prefix_ranges<br/>Source IP]
    O --> P[Check destination_port<br/>Dest Port]
    P --> Q[Check transport_protocol<br/>TLS/raw_buffer]

    Q --> R{Match Found?}
    R -->|Yes| S[Use Matched Filter Chain]
    R -->|No| T{Default Filter<br/>Chain Exists?}
    T -->|Yes| U[Use Default]
    T -->|No| V[Reject Connection]

    S --> W[Apply Filters]
    U --> W
```

## SNI Extraction and Matching

```mermaid
sequenceDiagram
    participant Client
    participant Network as Network Stack
    participant Inspector as TLS Inspector
    participant Matcher as Filter Chain Matcher
    participant FilterChain

    Client->>Network: TCP SYN
    Network-->>Client: SYN-ACK
    Client->>Network: ACK + TLS ClientHello
    Note over Client,Network: SNI: api.example.com<br/>ALPN: h2, http/1.1

    Network->>Inspector: First bytes of connection
    Inspector->>Inspector: Detect TLS (0x16 0x03)
    Inspector->>Inspector: Parse ClientHello

    Inspector->>Inspector: Extract SNI extension
    Note over Inspector: SNI = "api.example.com"

    Inspector->>Inspector: Extract ALPN extension
    Note over Inspector: ALPN = ["h2", "http/1.1"]

    Inspector->>Matcher: Find matching filter chain
    Note over Matcher: Inputs:<br/>- SNI: api.example.com<br/>- ALPN: h2, http/1.1<br/>- Source IP: 10.0.1.5<br/>- Dest Port: 443

    Matcher->>Matcher: Iterate filter chains
    loop Each Filter Chain
        Matcher->>Matcher: Check SNI match
        alt SNI Matches
            Matcher->>Matcher: Check ALPN match
            alt ALPN Matches
                Matcher->>Matcher: Check other criteria
                alt All Match
                    Matcher->>FilterChain: Select this chain
                end
            end
        end
    end

    Matcher-->>Inspector: Filter chain selected
    Inspector->>FilterChain: Hand off connection
    FilterChain->>Client: Continue TLS handshake
```

## SNI Matching Patterns

```mermaid
flowchart TD
    A[SNI Value:<br/>'api.example.com'] --> B{Match Type?}

    B -->|Exact| C["'api.example.com'"]
    C --> C1{Exact Match?}
    C1 -->|Yes| M[Matched]
    C1 -->|No| N[Not Matched]

    B -->|Prefix| D["'*.example.com'"]
    D --> D1{Matches wildcard?}
    D1 -->|Yes| M
    D1 -->|No| N

    B -->|Suffix| E["'example.com'"]
    E --> E1{Domain matches?}
    E1 -->|Yes| M
    E1 -->|No| N

    B -->|Regex| F["'api-[0-9]+\\.example\\.com'"]
    F --> F1{Regex Match?}
    F1 -->|Yes| M
    F1 -->|No| N

    B -->|Empty/Any| G["Empty matcher"]
    G --> H{SNI Present?}
    H -->|Yes/No| M
```

## ALPN Protocol Negotiation

```mermaid
sequenceDiagram
    participant Client
    participant Envoy
    participant Callback as ALPN Callback
    participant FilterChain

    Client->>Envoy: ClientHello
    Note over Client,Envoy: ALPN: [h2, http/1.1]

    Envoy->>FilterChain: Get supported ALPN
    FilterChain-->>Envoy: [h2, http/1.1, http/1.0]

    Envoy->>Callback: Select ALPN protocol
    Callback->>Callback: Find first common protocol

    alt Client: h2, Server: h2, http/1.1
        Callback-->>Envoy: Select "h2"
    else Client: http/1.1, Server: h2, http/1.1
        Callback-->>Envoy: Select "http/1.1"
    else No common protocol
        Callback-->>Envoy: TLS handshake failure
    end

    Envoy->>Client: ServerHello
    Note over Envoy,Client: ALPN: h2 (selected)

    Client->>Envoy: Application data over HTTP/2
```

## Filter Chain Match Priority

```mermaid
flowchart TD
    A[Multiple Filter Chains] --> B[Sort by Specificity]

    B --> C[Level 1: Destination Port]
    C --> D[Level 2: Source IP Prefix Length]
    D --> E[Level 3: Source Type<br/>LOCAL vs ANY]
    E --> F[Level 4: Source Port]
    F --> G[Level 5: Server Names<br/>SNI Specificity]
    G --> H[Level 6: Transport Protocol]
    H --> I[Level 7: Application Protocols<br/>ALPN]

    I --> J[Most Specific Match Wins]

    J --> K{Example}
    K --> L[Chain 1:<br/>SNI: api.example.com<br/>ALPN: h2]
    K --> M[Chain 2:<br/>SNI: *.example.com<br/>ALPN: any]
    K --> N[Chain 3:<br/>SNI: any<br/>ALPN: any]

    L --> O[Highest Priority]
    M --> P[Medium Priority]
    N --> Q[Lowest Priority]
```

## Configuration Example - Multiple Domains

```yaml
listeners:
  - name: https_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 443

    listener_filters:
      # Required for SNI/ALPN inspection
      - name: envoy.filters.listener.tls_inspector
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector

    filter_chains:
      # Chain 1: api.example.com with HTTP/2
      - filter_chain_match:
          server_names:
            - "api.example.com"
          application_protocols:
            - "h2"
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificates:
                - certificate_chain:
                    filename: /etc/ssl/api-cert.pem
                  private_key:
                    filename: /etc/ssl/api-key.pem
              alpn_protocols:
                - h2
        filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: api_h2
              codec_type: AUTO
              route_config:
                name: api_route
                virtual_hosts:
                  - name: api
                    domains: ["api.example.com"]
                    routes:
                      - match: { prefix: "/" }
                        route: { cluster: api_cluster }

      # Chain 2: www.example.com with HTTP/1.1
      - filter_chain_match:
          server_names:
            - "www.example.com"
          application_protocols:
            - "http/1.1"
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificates:
                - certificate_chain:
                    filename: /etc/ssl/www-cert.pem
                  private_key:
                    filename: /etc/ssl/www-key.pem
              alpn_protocols:
                - http/1.1
        filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: www_http1
              codec_type: AUTO
              route_config:
                name: www_route
                virtual_hosts:
                  - name: www
                    domains: ["www.example.com"]
                    routes:
                      - match: { prefix: "/" }
                        route: { cluster: www_cluster }

      # Chain 3: Wildcard for other subdomains
      - filter_chain_match:
          server_names:
            - "*.example.com"
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificates:
                - certificate_chain:
                    filename: /etc/ssl/wildcard-cert.pem
                  private_key:
                    filename: /etc/ssl/wildcard-key.pem
              alpn_protocols:
                - h2
                - http/1.1
        filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: wildcard
              codec_type: AUTO
              route_config:
                name: wildcard_route
                virtual_hosts:
                  - name: wildcard
                    domains: ["*"]
                    routes:
                      - match: { prefix: "/" }
                        route: { cluster: default_cluster }

      # Default chain (no SNI)
      - transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificates:
                - certificate_chain:
                    filename: /etc/ssl/default-cert.pem
                  private_key:
                    filename: /etc/ssl/default-key.pem
        filters:
          - name: envoy.filters.network.http_connection_manager
```

## TLS Inspector Listener Filter

```mermaid
flowchart TD
    A[Connection Established] --> B[TLS Inspector Activated]
    B --> C[Peek First Bytes]
    Note over C[Non-blocking peek]

    C --> D{Bytes Available?}
    D -->|No| E[Wait for data]
    E --> D
    D -->|Yes| F[Examine First Byte]

    F --> G{TLS Content Type?}
    G -->|0x16 Handshake| H[TLS Detected]
    G -->|Other| I[Non-TLS]

    H --> J[Parse ClientHello]
    J --> K[Extract Extensions]

    K --> L[SNI Extension<br/>0x0000]
    K --> M[ALPN Extension<br/>0x0010]
    K --> N[Other Extensions]

    L --> O[Store SNI Value]
    M --> P[Store ALPN List]
    N --> Q[Store Other Properties]

    O --> R[Continue to Filter Chain Matching]
    P --> R
    Q --> R
    I --> R
```

## Configuration Example - Source IP Matching

```yaml
listeners:
  - name: multi_criteria_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 443

    listener_filters:
      - name: envoy.filters.listener.tls_inspector

    filter_chains:
      # Chain 1: Internal traffic from VPC
      - filter_chain_match:
          server_names:
            - "internal.example.com"
          source_prefix_ranges:
            - address_prefix: "10.0.0.0"
              prefix_len: 8
          application_protocols:
            - "h2"
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificates:
                - certificate_chain:
                    filename: /etc/ssl/internal-cert.pem
                  private_key:
                    filename: /etc/ssl/internal-key.pem
              alpn_protocols:
                - h2
              # Require client certificates for internal traffic
              validation_context:
                trusted_ca:
                  filename: /etc/ssl/internal-ca.pem
            require_client_certificate: true
        filters:
          - name: envoy.filters.network.http_connection_manager
            # ... internal HCM config ...

      # Chain 2: External traffic
      - filter_chain_match:
          server_names:
            - "api.example.com"
          # No source IP restriction - any external IP
          application_protocols:
            - "h2"
            - "http/1.1"
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificates:
                - certificate_chain:
                    filename: /etc/ssl/api-cert.pem
                  private_key:
                    filename: /etc/ssl/api-key.pem
              alpn_protocols:
                - h2
                - http/1.1
            # No client certificate required
        filters:
          - name: envoy.filters.network.http_connection_manager
            # ... external HCM config ...
```

## ALPN Protocol Selection Logic

```mermaid
flowchart TD
    A[Client ALPN List] --> B[Server ALPN List]
    B --> C[Intersection]

    C --> D{Common Protocols?}
    D -->|No| E[TLS Alert:<br/>no_application_protocol]
    D -->|Yes| F[Select First Common<br/>from Client List]

    F --> G[Example]
    G --> H[Client: h2, http/1.1]
    G --> I[Server: http/1.1, h2, http/1.0]

    H --> J[Common: h2, http/1.1]
    J --> K[Select: h2<br/>First in client list]

    K --> L[HTTP/2 Connection]
```

## Multi-Protocol Filter Chains

```mermaid
graph TD
    A[Listener Port 443] --> B{Protocol?}

    B -->|HTTP/2| C[h2 Filter Chain]
    C --> C1[HTTP/2 Codec]
    C1 --> C2[HTTP Filters]
    C2 --> C3[gRPC Services]

    B -->|HTTP/1.1| D[http/1.1 Filter Chain]
    D --> D1[HTTP/1.1 Codec]
    D1 --> D2[HTTP Filters]
    D2 --> D3[REST APIs]

    B -->|TCP| E[Raw TCP Filter Chain]
    E --> E1[TCP Proxy]
    E1 --> E2[Database Connections]
```

## Filter Chain Match Examples

### Example 1: Exact SNI Match
```yaml
filter_chain_match:
  server_names:
    - "api.example.com"  # Exact match only
```

### Example 2: Wildcard SNI Match
```yaml
filter_chain_match:
  server_names:
    - "*.example.com"  # Matches any.example.com, api.example.com, etc.
    - "*.prod.example.com"  # Matches app1.prod.example.com
```

### Example 3: Multiple ALPN Protocols
```yaml
filter_chain_match:
  application_protocols:
    - "h2"           # HTTP/2
    - "http/1.1"     # HTTP/1.1
    - "http/1.0"     # HTTP/1.0
```

### Example 4: Combined Matching
```yaml
filter_chain_match:
  server_names:
    - "secure.example.com"
  source_prefix_ranges:
    - address_prefix: "192.168.0.0"
      prefix_len: 16
  application_protocols:
    - "h2"
  destination_port: 443
```

## ClientHello Structure

```mermaid
classDiagram
    class ClientHello {
        +ProtocolVersion client_version
        +Random random
        +SessionID session_id
        +vector~CipherSuite~ cipher_suites
        +vector~CompressionMethod~ compression_methods
        +Extensions extensions
    }

    class Extensions {
        +SNI server_name
        +ALPN application_layer_protocol_negotiation
        +SupportedGroups supported_groups
        +SignatureAlgorithms signature_algorithms
    }

    class SNI {
        +NameType name_type
        +string host_name
    }

    class ALPN {
        +vector~string~ protocol_name_list
    }

    ClientHello --> Extensions
    Extensions --> SNI
    Extensions --> ALPN
```

## Statistics

```yaml
# Listener filter chain match stats
listener.0.0.0.0_443.server_ssl_socket_factory.ssl_context_update_by_sds
listener.0.0.0.0_443.server_ssl_socket_factory.downstream_context_secrets_not_ready
listener.0.0.0.0_443.ssl.connection_error
listener.0.0.0.0_443.ssl.no_certificate

# Per filter chain stats (if named)
listener.0.0.0.0_443.filter_chain.api_example_com.downstream_cx_total
listener.0.0.0.0_443.filter_chain.api_example_com.downstream_cx_active

# TLS inspector stats
listener.0.0.0.0_443.tls_inspector.alpn_found
listener.0.0.0.0_443.tls_inspector.alpn_not_found
listener.0.0.0.0_443.tls_inspector.sni_found
listener.0.0.0.0_443.tls_inspector.sni_not_found
listener.0.0.0.0_443.tls_inspector.client_hello_too_large
```

## Best Practices

### 1. Always Enable TLS Inspector for SNI/ALPN

```yaml
listener_filters:
  - name: envoy.filters.listener.tls_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
```

### 2. Order Filter Chains by Specificity

```yaml
filter_chains:
  # Most specific first
  - filter_chain_match:
      server_names: ["api.v2.example.com"]
      application_protocols: ["h2"]

  # Less specific
  - filter_chain_match:
      server_names: ["*.example.com"]
      application_protocols: ["h2"]

  # Least specific
  - filter_chain_match:
      server_names: ["*.example.com"]

  # Default (no match criteria)
  - {}
```

### 3. Use Named Filter Chains for Observability

```yaml
filter_chains:
  - name: "api_h2"
    filter_chain_match:
      server_names: ["api.example.com"]
      application_protocols: ["h2"]
```

### 4. Configure ALPN for HTTP/2

```yaml
alpn_protocols:
  - "h2"          # HTTP/2 over TLS
  - "http/1.1"    # HTTP/1.1 over TLS
```

### 5. Handle Missing SNI Gracefully

```yaml
# Default filter chain for connections without SNI
filter_chains:
  - {} # No filter_chain_match - catches all
    transport_socket:
      # ... default certificate ...
```

## Troubleshooting

### Debug SNI/ALPN Matching

```bash
# Enable debug logging
curl -X POST "http://localhost:9901/logging?connection=debug"

# Check TLS inspector stats
curl http://localhost:9901/stats | grep tls_inspector

# View active connections and their SNI
curl http://localhost:9901/certs

# Check filter chain matching
# Look for: "filter chain match" in logs
```

### Common Issues

1. **SNI Not Found**
   - Client not sending SNI extension
   - Ensure TLS inspector is enabled
   - Check for old TLS clients

2. **ALPN Mismatch**
   - No common protocols between client and server
   - Configure matching ALPN protocols

3. **Wrong Filter Chain Selected**
   - Check filter chain order
   - Verify match criteria
   - Use named chains for debugging

4. **TLS Handshake Failure**
   - Check cipher suite compatibility
   - Verify certificate validity
   - Ensure TLS version overlap

## Performance Considerations

### TLS Inspector Overhead

```mermaid
graph LR
    A[Connection] --> B[TLS Inspector: +0.1-0.5ms]
    B --> C[Filter Chain Match: +0.01ms]
    C --> D[TLS Handshake: +1-10ms]
    D --> E[Total Overhead: ~1-11ms]
```

- TLS Inspector adds minimal overhead (~0.1-0.5ms)
- Peeking is non-blocking and efficient
- Filter chain matching is very fast (hash lookup)

## References

- [Envoy TLS Inspector](https://www.envoyproxy.io/docs/envoy/latest/configuration/listeners/listener_filters/tls_inspector)
- [Filter Chain Matching](https://www.envoyproxy.io/docs/envoy/latest/api-v3/config/listener/v3/listener_components.proto#envoy-v3-api-msg-config-listener-v3-filterchainmatch)
- [ALPN RFC 7301](https://tools.ietf.org/html/rfc7301)
- [SNI RFC 6066](https://tools.ietf.org/html/rfc6066)
