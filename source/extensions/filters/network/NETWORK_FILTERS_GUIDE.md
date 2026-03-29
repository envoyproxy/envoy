# Envoy Network Filters - Comprehensive Guide

## Table of Contents
1. [Introduction](#introduction)
2. [HTTP Connection Manager](#1-http-connection-manager)
3. [TCP Proxy](#2-tcp-proxy)
4. [External Authorization (ext_authz)](#3-external-authorization-ext_authz)
5. [RBAC (Role-Based Access Control)](#4-rbac-role-based-access-control)
6. [Rate Limit](#5-rate-limit)
7. [Connection Limit](#6-connection-limit)
8. [SNI Cluster](#7-sni-cluster)
9. [Direct Response](#8-direct-response)
10. [Echo](#9-echo)
11. [Comparison Matrix](#10-comparison-matrix)
12. [Best Practices](#11-best-practices)

---

## Introduction

Network filters in Envoy operate at the **L4 (transport layer)** after listener filters have processed socket metadata. They handle the actual connection data stream and can:

- **Process TCP/UDP data** flowing through connections
- **Implement protocol-specific logic** (HTTP, Redis, MongoDB, etc.)
- **Authorize or reject** connections based on policies
- **Rate limit** at the connection or request level
- **Proxy or terminate** connections
- **Inspect and modify** data streams

### Network Filter Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│  1. Listener Filter Chain Complete                              │
│     ↓                                                            │
│  2. Filter Chain Matching (based on listener filter metadata)   │
│     ↓                                                            │
│  3. Network Filter Chain Execution (ordered)                    │
│     ├─ ReadFilter.onNewConnection()                             │
│     │   Returns: Continue or StopIteration                      │
│     ├─ ReadFilter.onData(buffer, end_stream)                    │
│     │   Returns: Continue or StopIteration                      │
│     ├─ WriteFilter.onWrite(buffer, end_stream)                  │
│     │   Returns: Continue or StopIteration                      │
│     └─ ConnectionCallbacks.onEvent(event)                       │
│         (Connected, LocalClose, RemoteClose)                    │
│     ↓                                                            │
│  4. Terminal Filter (tcp_proxy, http_connection_manager, etc.)  │
│     • Establishes upstream connections                          │
│     • Proxies data bidirectionally                              │
└─────────────────────────────────────────────────────────────────┘
```

### Key Interfaces

**Network::ReadFilter**
```cpp
class ReadFilter {
  virtual FilterStatus onNewConnection() = 0;
  virtual FilterStatus onData(Buffer::Instance& data, bool end_stream) = 0;
  virtual void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) = 0;
};
```

**Network::WriteFilter**
```cpp
class WriteFilter {
  virtual FilterStatus onWrite(Buffer::Instance& data, bool end_stream) = 0;
  virtual void initializeWriteFilterCallbacks(WriteFilterCallbacks& callbacks) = 0;
};
```

**Network::Filter** (combines Read and Write)
```cpp
class Filter : public ReadFilter, public WriteFilter {};
```

**FilterStatus**
- `Continue`: Pass data to next filter
- `StopIteration`: Buffer data, don't proceed to next filter

**Filter Types**
- **Non-Terminal**: Process data but don't establish upstream connections (auth, RBAC, rate limit)
- **Terminal**: Establish upstream connections and proxy data (tcp_proxy, http_connection_manager)

---

## 1. HTTP Connection Manager

### Overview
The HTTP Connection Manager (HCM) is the **most critical network filter** in Envoy. It terminates HTTP/1.1, HTTP/2, and HTTP/3 connections, decodes HTTP requests/responses, and manages the HTTP filter chain.

### Purpose
- **Protocol handling**: HTTP/1.1, HTTP/2 (h2/h2c), HTTP/3 (QUIC)
- **Request decoding**: Parse HTTP requests into structured format
- **HTTP filter chain**: Execute HTTP-level filters (router, CORS, auth, etc.)
- **Routing**: Route requests to upstream clusters
- **Connection management**: Keep-alive, pipelining, multiplexing
- **Stats and tracing**: Detailed HTTP metrics and distributed tracing

### Key Features
- Codec selection based on ALPN or protocol detection
- HTTP/1.1: pipelining, chunked encoding, upgrade (WebSocket)
- HTTP/2: multiplexing, flow control, server push
- HTTP/3: QUIC transport, 0-RTT
- Header validation and normalization
- Request ID generation and propagation
- Access logging
- Graceful drain and connection closing

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  HTTP Connection Manager (Network Filter)                           │
│                                                                      │
│  onNewConnection()                                                   │
│    → Create codec (HTTP/1, HTTP/2, HTTP/3)                          │
│    → Initialize connection state                                    │
│    → Return Continue                                                 │
│                                                                      │
│  onData(buffer)                                                      │
│    → codec_->dispatch(buffer)                                        │
│       ├─ Decode HTTP frames/requests                                │
│       ├─ For each complete request:                                 │
│       │   ├─ Create ActiveStream                                    │
│       │   ├─ Execute HTTP filter chain                              │
│       │   │   ├─ decodeHeaders()                                    │
│       │   │   ├─ decodeData()                                       │
│       │   │   └─ decodeTrailers()                                   │
│       │   └─ Route to upstream cluster                              │
│       └─ Encode response                                             │
│           ├─ encodeHeaders()                                         │
│           ├─ encodeData()                                            │
│           └─ encodeTrailers()                                        │
│    → Return Continue                                                 │
│                                                                      │
│  onWrite(buffer)                                                     │
│    → codec_->encode(response) → buffer                               │
│    → Return Continue                                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Class Structure

```cpp
class ConnectionManagerImpl : public Network::ReadFilter,
                               public Network::ConnectionCallbacks,
                               public ServerConnectionCallbacks {
  - ConnectionManagerConfig& config_
  - ServerConnectionPtr codec_                // HTTP/1, HTTP/2, or HTTP/3
  - StreamInfo::StreamInfoImpl stream_info_
  - std::list<ActiveStreamPtr> streams_       // Active HTTP streams
  - Http::ConnectionManagerStats stats_
  - RouteConfigProvider& route_config_provider_
  - std::unique_ptr<Router> router_

  // Network::ReadFilter
  + onNewConnection() → Continue
  + onData(buffer, end_stream) → Continue | StopIteration
  + initializeReadFilterCallbacks(callbacks)

  // ServerConnectionCallbacks (from codec)
  + newStream(ResponseEncoder&, is_internally_created) → RequestDecoder&
  + onGoAway(code)

  // Per-request processing
  - createStream() → ActiveStream*
  - startRequest()
  - endStream()
};

class ActiveStream : public StreamEncoderFilterManager,
                     public StreamDecoderFilterManager,
                     public RequestDecoder {
  - ConnectionManagerImpl& connection_manager_
  - ResponseEncoder& response_encoder_
  - std::list<ActiveStreamDecoderFilterPtr> decoder_filters_
  - std::list<ActiveStreamEncoderFilterPtr> encoder_filters_
  - Router::RouteConstSharedPtr cached_route_

  // Request decoding (from codec → filters)
  + decodeHeaders(headers, end_stream)
  + decodeData(data, end_stream)
  + decodeTrailers(trailers)
  + decodeMetadata(metadata)

  // Response encoding (from filters → codec)
  + encodeHeaders(headers, end_stream)
  + encodeData(data, end_stream)
  + encodeTrailers(trailers)
  + encodeMetadata(metadata)

  // Filter management
  - createFilterChain()
  - decodeHeaders() → iterates decoder_filters_
  - encodeHeaders() → iterates encoder_filters_
};
```

### HTTP Filter Chain

```
Downstream                HCM                    HTTP Filters              Upstream
   │                       │                          │                       │
   │────HTTP Request──────►│                          │                       │
   │                       │─────decodeHeaders()─────►│                       │
   │                       │                          │─[Authentication]──    │
   │                       │                          │─[Authorization]───    │
   │                       │                          │─[Rate Limiting]───    │
   │                       │                          │─[Router]──────────────►
   │                       │                          │                       │
   │                       │◄────encodeHeaders()──────│◄──────────────────────│
   │◄──HTTP Response───────│                          │                       │
```

### Codec Selection

**Based on ALPN** (from TLS Inspector):
- `h2` → HTTP/2 codec
- `http/1.1` → HTTP/1.1 codec
- `h3` → HTTP/3 codec (QUIC)

**Based on protocol detection** (from HTTP Inspector):
- HTTP/2 connection preface → HTTP/2 codec
- HTTP/1.x request line → HTTP/1.1 codec

**Upgrade**:
- HTTP/1.1 with `Upgrade: h2c` → HTTP/2 cleartext

### Configuration Example

```yaml
filter_chains:
  - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          codec_type: AUTO  # AUTO, HTTP1, HTTP2, HTTP3

          # Routing
          route_config:
            name: local_route
            virtual_hosts:
              - name: backend
                domains: ["*"]
                routes:
                  - match: { prefix: "/" }
                    route: { cluster: backend_cluster }

          # HTTP filters
          http_filters:
            - name: envoy.filters.http.router
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.http.router.v3.Router

          # Connection settings
          common_http_protocol_options:
            idle_timeout: 3600s
            max_connection_duration: 0s

          http2_protocol_options:
            max_concurrent_streams: 100
            initial_stream_window_size: 65536
            initial_connection_window_size: 1048576

          # Access logging
          access_log:
            - name: envoy.access_loggers.file
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
                path: /var/log/envoy/access.log
```

### Stats
- `http.<stat_prefix>.downstream_cx_total`: Total connections
- `http.<stat_prefix>.downstream_rq_total`: Total requests
- `http.<stat_prefix>.downstream_rq_2xx`: 2xx responses
- `http.<stat_prefix>.downstream_rq_4xx`: 4xx responses
- `http.<stat_prefix>.downstream_rq_5xx`: 5xx responses
- `http.<stat_prefix>.downstream_rq_time`: Request duration histogram

### Use Cases
1. **API Gateway**: HTTP routing, authentication, rate limiting
2. **Reverse Proxy**: Load balancing HTTP traffic
3. **Service Mesh**: Sidecar proxy for microservices
4. **Edge Proxy**: External-facing HTTP endpoint

---

## 2. TCP Proxy

### Overview
The TCP Proxy filter is a **terminal network filter** that proxies raw TCP data between downstream and upstream connections. It's protocol-agnostic and operates at L4.

### Purpose
- **TCP tunneling**: Transparent TCP proxying
- **TLS termination/origination**: Decrypt downstream, encrypt upstream
- **Connection pooling**: Reuse upstream connections
- **Access control**: IP allowlist/denylist
- **Traffic shifting**: Weighted cluster routing

### Key Features
- Bidirectional data proxying
- Connection draining
- Upstream cluster selection
- Hash-based routing (consistent hashing)
- Tunneling through HTTP CONNECT (egress)
- Metadata-based routing
- Configurable idle timeout

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  TCP Proxy Filter (Terminal)                                        │
│                                                                      │
│  onNewConnection()                                                   │
│    → Resolve upstream cluster (direct, weighted, metadata)          │
│    → Create upstream connection via cluster manager                 │
│    → Establish connection                                            │
│    → Return StopIteration (terminal filter)                         │
│                                                                      │
│  onData(downstream_buffer, end_stream)                               │
│    → upstream_->write(downstream_buffer)                             │
│    → Return StopIteration                                            │
│                                                                      │
│  onUpstreamData(upstream_buffer, end_stream)                         │
│    → downstream_->write(upstream_buffer)                             │
│                                                                      │
│  onEvent(event)                                                      │
│    → Handle connection close, drain, etc.                            │
└─────────────────────────────────────────────────────────────────────┘
```

### Cluster Selection

**1. Direct cluster**:
```yaml
cluster: backend_cluster
```

**2. Weighted clusters**:
```yaml
weighted_clusters:
  clusters:
    - name: cluster_a
      weight: 70
    - name: cluster_b
      weight: 30
```

**3. Metadata-based**:
```yaml
metadata_match:
  filter_metadata:
    envoy.lb:
      version: v2
```

### Class Structure

```cpp
class Filter : public Network::ReadFilter,
               public Upstream::LoadBalancerContext,
               public Network::ConnectionCallbacks {
  - Config& config_
  - Upstream::ClusterInfoConstSharedPtr cluster_
  - Upstream::TcpPoolHandle upstream_handle_
  - Network::Connection* upstream_connection_
  - Network::ReadFilterCallbacks* downstream_callbacks_
  - StreamInfo::StreamInfoImpl stream_info_
  - RouteConstSharedPtr route_

  // Network::ReadFilter
  + onNewConnection() → StopIteration
  + onData(buffer, end_stream) → StopIteration
  + initializeReadFilterCallbacks(callbacks)

  // Upstream callbacks
  + onUpstreamData(buffer, end_stream)
  + onUpstreamEvent(event)
  + onPoolReady(upstream_connection, host)
  + onPoolFailure(reason, host)

  // Connection management
  - establishUpstreamConnection()
  - onConnectTimeout()
  - drainFilterState()
  - closeDownstream(close_type)
};

class Config {
  - std::string stat_prefix_
  - RouteConfigProvider& route_config_provider_
  - Upstream::ClusterManager& cluster_manager_
  - std::chrono::milliseconds idle_timeout_
  - std::chrono::milliseconds connect_timeout_
  - AccessLogInstanceSharedPtr access_log_

  + getRouteFromEntries(connection) → RouteConstSharedPtr
  + createFilterChain(callbacks) → Filter*
};
```

### Sequence Diagram

```
Downstream        TCP Proxy         Cluster Manager      Upstream
   │                  │                    │                 │
   │──Connect─────────►│                    │                 │
   │                  │─getCluster()──────►│                 │
   │                  │◄─cluster───────────│                 │
   │                  │─newConnection()────►│                 │
   │                  │                    │──TCP Connect────►
   │                  │◄─onPoolReady()─────│◄────SYN/ACK─────│
   │◄─Accept──────────│                    │                 │
   │──Data───────────►│────────────────────────────Data─────►│
   │                  │                    │                 │
   │◄─Data────────────│◄───────────────────────────Data──────│
   │──FIN────────────►│────────────────────────────FIN──────►│
   │◄─FIN─────────────│◄───────────────────────────FIN───────│
```

### Configuration Example

```yaml
filter_chains:
  - filters:
      - name: envoy.filters.network.tcp_proxy
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
          stat_prefix: tcp_stats
          cluster: backend_cluster

          # Timeouts
          idle_timeout: 3600s

          # Tunneling (for egress)
          tunneling_config:
            hostname: proxy.example.com:443
            use_post: false

          # Access logging
          access_log:
            - name: envoy.access_loggers.file
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
                path: /var/log/envoy/tcp_access.log

          # Hash policy for consistent hashing
          hash_policy:
            - source_ip: {}
```

### Stats
- `tcp.<stat_prefix>.downstream_cx_total`: Total downstream connections
- `tcp.<stat_prefix>.downstream_cx_tx_bytes_total`: Bytes sent downstream
- `tcp.<stat_prefix>.downstream_cx_rx_bytes_total`: Bytes received downstream
- `tcp.<stat_prefix>.upstream_flush_total`: Upstream flush operations
- `tcp.<stat_prefix>.idle_timeout`: Connections closed by idle timeout

### Use Cases
1. **Database proxy**: MySQL, PostgreSQL, MongoDB tunneling
2. **Cache proxy**: Redis, Memcached proxying
3. **Generic L4 proxy**: Any TCP-based protocol
4. **TLS termination**: Decrypt TLS, proxy plaintext to backend

---

## 3. External Authorization (ext_authz)

### Overview
The ext_authz filter calls an external authorization service to determine whether a connection should be allowed. It can query both gRPC and HTTP external authz services.

### Purpose
- **Centralized authorization**: Delegate auth decisions to external service
- **Policy enforcement**: Apply complex business logic outside Envoy
- **Connection gating**: Allow/deny connections before data processing
- **Attribute enrichment**: Add metadata from authz service

### Key Features
- gRPC or HTTP external authz service
- Synchronous authorization (blocks connection)
- Failure mode: allow or deny
- TLS certificate propagation
- Filter enable/disable based on metadata
- Dynamic metadata injection

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  ExtAuthz Filter (Non-Terminal)                                     │
│                                                                      │
│  onNewConnection()                                                   │
│    → If filter disabled by metadata: Return Continue                │
│    → Build CheckRequest:                                             │
│       ├─ Source/destination addresses                               │
│       ├─ TLS certificate (if include_peer_certificate)              │
│       ├─ Metadata context                                            │
│       └─ Labels from bootstrap                                       │
│    → client_->check(request, callbacks)                             │
│    → Return StopIteration (wait for authz response)                 │
│                                                                      │
│  onComplete(response)                                                │
│    → If OK:                                                          │
│       ├─ Apply dynamic metadata                                     │
│       └─ continueReading() → Continue to next filter                │
│    → If DENIED:                                                      │
│       ├─ stats_.denied_.inc()                                        │
│       ├─ Optional: send TLS alert                                   │
│       └─ connection_.close()                                         │
│    → If ERROR:                                                       │
│       └─ If failure_mode_allow: Continue, else: close               │
│                                                                      │
│  onData(buffer, end_stream)                                          │
│    → Return Continue (authz already done)                            │
└─────────────────────────────────────────────────────────────────────┘
```

### Class Structure

```cpp
class Config {
  - InstanceStats stats_
  - bool failure_mode_allow_
  - bool include_peer_certificate_
  - bool include_tls_session_
  - bool send_tls_alert_on_denial_
  - optional<MetadataMatcher> filter_enabled_metadata_
  - vector<string> metadata_context_namespaces_
  - map<string, string> destination_labels_

  + stats() → InstanceStats&
  + failureModeAllow() → bool
  + includePeerCertificate() → bool
  + filterEnabledMetadata(metadata) → bool
};

class Filter : public Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public RequestCallbacks {
  - ConfigSharedPtr config_
  - ClientPtr client_
  - Network::ReadFilterCallbacks* filter_callbacks_
  - bool calling_check_{false}

  // Network::ReadFilter
  + onNewConnection() → StopIteration
  + onData(buffer, end_stream) → Continue
  + initializeReadFilterCallbacks(callbacks)

  // RequestCallbacks (from authz client)
  + onComplete(response)

  // Helper methods
  - createCheckRequest() → CheckRequest
  - injectDynamicMetadata(response)
};
```

### CheckRequest Structure

```protobuf
message CheckRequest {
  // Connection attributes
  AttributeContext attributes = 1 {
    source: {
      address: "1.2.3.4:12345",
      principal: "spiffe://cluster.local/ns/default/sa/client"
    },
    destination: {
      address: "10.0.0.1:8080",
      principal: "spiffe://cluster.local/ns/default/sa/server"
    },
    metadata_context: { ... },
    tls_session: { ... }
  }
}

message CheckResponse {
  Status status = 1;  // OK, PERMISSION_DENIED, etc.
  OkHttpResponse ok_response = 2 {
    // Optional dynamic metadata to inject
    dynamic_metadata: { ... }
  }
  DeniedHttpResponse denied_response = 3;
}
```

### Sequence Diagram

```
Downstream     ExtAuthz Filter    AuthZ Service     Next Filter
   │                  │                  │               │
   │──Connect─────────►│                  │               │
   │                  │──CheckRequest────►│               │
   │                  │  (blocked)        │               │
   │                  │                  [Policy Check]  │
   │                  │◄─CheckResponse───│               │
   │                  │  (status: OK)     │               │
   │                  │──────────────────────Continue────►│
   │                  │                  │               │
   │──Data───────────►│─────────────────────────Data────►│
```

### Configuration Example

```yaml
filters:
  - name: envoy.filters.network.ext_authz
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.ext_authz.v3.ExtAuthz
      stat_prefix: ext_authz

      # gRPC service
      grpc_service:
        envoy_grpc:
          cluster_name: ext_authz_cluster
        timeout: 0.5s

      # Failure mode
      failure_mode_allow: false

      # TLS settings
      include_peer_certificate: true
      include_tls_session: true
      send_tls_alert_on_denial: true

      # Metadata context
      metadata_context_namespaces:
        - envoy.filters.http.jwt_authn

      # Conditional enable
      filter_enabled_metadata:
        filter: envoy.filters.network.rbac
        path:
          - key: shadow_rules_stat_prefix
        value:
          string_match:
            exact: "authz_"
```

### Stats
- `ext_authz.<stat_prefix>.total`: Total authz checks
- `ext_authz.<stat_prefix>.ok`: Allowed connections
- `ext_authz.<stat_prefix>.denied`: Denied connections
- `ext_authz.<stat_prefix>.error`: Authz service errors
- `ext_authz.<stat_prefix>.failure_mode_allowed`: Allowed due to failure mode
- `ext_authz.<stat_prefix>.active`: Active authz calls

### Use Cases
1. **Centralized policy**: OPA, custom authz service
2. **mTLS validation**: Verify client certificates
3. **IP allowlisting**: Dynamic IP-based access control
4. **Service mesh authz**: Workload identity verification

---

## 4. RBAC (Role-Based Access Control)

### Overview
RBAC filter enforces role-based access control policies directly in Envoy without external service calls. It supports complex rules based on source/destination IPs, principals, metadata, and more.

### Purpose
- **Fast authorization**: Policy enforcement without network calls
- **Policy as code**: Define access rules in Envoy config
- **Shadow mode**: Test policies without enforcement
- **Connection-level gating**: Allow/deny before data processing

### Key Features
- AND/OR/NOT policy expressions
- Source/destination IP matching (CIDR ranges)
- Principal matching (SPIFFE IDs from mTLS)
- Metadata matching (dynamic metadata, filter state)
- Header matching (requires HTTP connection manager)
- Shadow policies for testing
- Delayed denial (log but don't immediately close)

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  RBAC Filter (Non-Terminal)                                         │
│                                                                      │
│  onNewConnection()                                                   │
│    → Return Continue (wait for data to check)                       │
│                                                                      │
│  onData(buffer, end_stream)                                          │
│    → If already evaluated: Return Continue                          │
│    → engine_result_ = checkEngine(Enforced)                         │
│       ├─ Build RequestContext from connection                       │
│       └─ engine_->allowed(request_context)                          │
│    → If shadow_engine: checkEngine(Shadow) → dynamic metadata       │
│    → If ALLOWED:                                                     │
│       └─ Return Continue                                             │
│    → If DENIED:                                                      │
│       ├─ If delay_deny_ms > 0: schedule timer, return Continue      │
│       └─ Else: closeConnection(), return StopIteration              │
│                                                                      │
│  onEvent(ConnectionEvent::LocalClose | RemoteClose)                 │
│    → If delayed denial timer active: cancel timer                   │
└─────────────────────────────────────────────────────────────────────┘
```

### Policy Structure

```yaml
policies:
  "allow-from-admin-subnet":
    permissions:
      - and_rules:
          rules:
            - destination_port: 8080
            - any: true
    principals:
      - remote_ip:
          address_prefix: "10.0.0.0"
          prefix_len: 16

  "deny-from-untrusted":
    permissions:
      - not_rule:
          requested_server_name:
            exact: "trusted.example.com"
    principals:
      - any: true
```

### Class Structure

```cpp
class RoleBasedAccessControlFilterConfig {
  - RoleBasedAccessControlFilterStats stats_
  - string shadow_rules_stat_prefix_
  - unique_ptr<RoleBasedAccessControlEngine> engine_
  - unique_ptr<RoleBasedAccessControlEngine> shadow_engine_
  - EnforcementType enforcement_type_  // CONTINUOUS, ONE_TIME_ON_FIRST_BYTE
  - chrono::milliseconds delay_deny_ms_

  + engine(mode) → RoleBasedAccessControlEngine*
  + enforcementType() → EnforcementType
  + delayDenyMs() → chrono::milliseconds
};

class RoleBasedAccessControlFilter : public Network::ReadFilter,
                                      public Network::ConnectionCallbacks {
  - RoleBasedAccessControlFilterConfigSharedPtr config_
  - Network::ReadFilterCallbacks* callbacks_
  - EngineResult engine_result_{Unknown}
  - EngineResult shadow_engine_result_{Unknown}
  - Event::TimerPtr delay_timer_
  - bool is_delay_denied_{false}

  // Network::ReadFilter
  + onNewConnection() → Continue
  + onData(buffer, end_stream) → Continue | StopIteration
  + initializeReadFilterCallbacks(callbacks)

  // Network::ConnectionCallbacks
  + onEvent(event)

  // Helper methods
  - checkEngine(mode) → Result
  - closeConnection()
  - resetTimerState()
};
```

### Policy Evaluation

**Permissions** (what actions are allowed):
- `destination_port`: Port number
- `destination_ip`: CIDR range
- `requested_server_name`: SNI from TLS
- `metadata`: Dynamic metadata matcher
- `any`: Always matches
- `not_rule`, `and_rules`, `or_rules`: Logic combinators

**Principals** (who is allowed):
- `remote_ip`: Source CIDR range
- `authenticated`: mTLS authenticated principal (SPIFFE ID)
- `source_ip`: Same as remote_ip
- `direct_remote_ip`: Actual remote IP (before PROXY protocol)
- `metadata`: Dynamic metadata matcher
- `any`: Always matches
- `not_id`, `and_ids`, `or_ids`: Logic combinators

**Action**: `ALLOW` (allowlist) or `DENY` (denylist)

### Configuration Example

```yaml
filters:
  - name: envoy.filters.network.rbac
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.rbac.v3.RBAC
      stat_prefix: rbac

      # Enforcement
      enforcement_type: CONTINUOUS  # or ONE_TIME_ON_FIRST_BYTE

      # Policies
      rules:
        action: ALLOW  # Allowlist mode
        policies:
          "allow-authenticated":
            permissions:
              - any: true
            principals:
              - authenticated:
                  principal_name:
                    exact: "spiffe://cluster.local/ns/default/sa/frontend"

          "allow-internal-ips":
            permissions:
              - destination_port: 8080
            principals:
              - remote_ip:
                  address_prefix: "10.0.0.0"
                  prefix_len: 8

      # Shadow mode for testing
      shadow_rules:
        action: DENY
        policies:
          "test-deny-external":
            permissions:
              - any: true
            principals:
              - not_id:
                  remote_ip:
                    address_prefix: "10.0.0.0"
                    prefix_len: 8

      # Optional delay before denial
      delay_deny_ms: 5000
```

### Stats
- `rbac.<stat_prefix>.allowed`: Allowed connections
- `rbac.<stat_prefix>.denied`: Denied connections
- `rbac.<stat_prefix>.shadow_allowed`: Shadow mode allowed
- `rbac.<stat_prefix>.shadow_denied`: Shadow mode denied

### Use Cases
1. **IP-based access control**: Restrict by source IP ranges
2. **mTLS authz**: Allow only authenticated workloads
3. **Service-to-service authz**: SPIFFE-based identity verification
4. **Policy testing**: Shadow mode before enforcement

---

## 5. Rate Limit

### Overview
The rate limit filter calls an external rate limiting service to enforce distributed rate limits across multiple Envoy instances.

### Purpose
- **Distributed rate limiting**: Coordinate limits across fleet
- **Per-descriptor limits**: Rate limit based on arbitrary attributes
- **Failure mode**: Allow or deny on service failure
- **Traffic shaping**: Prevent overload on upstreams

### Key Features
- gRPC rate limit service (Lyft's ratelimit service)
- Descriptor-based rate limiting
- Failure mode: allow or deny
- Runtime-configurable enable/disable
- Substitution formatters for dynamic descriptors

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  Rate Limit Filter (Non-Terminal)                                   │
│                                                                      │
│  onNewConnection()                                                   │
│    → Build descriptors from config + substitution formatters        │
│    → client_->limit(request, callbacks)                             │
│    → Return StopIteration (wait for rate limit response)            │
│                                                                      │
│  complete(response)                                                  │
│    → If OVER_LIMIT:                                                  │
│       ├─ stats_.over_limit_.inc()                                    │
│       ├─ connection_.close()                                         │
│       └─ Return (filter stopped)                                     │
│    → If OK:                                                          │
│       ├─ stats_.ok_.inc()                                            │
│       └─ filter_callbacks_->continueReading()                       │
│    → If ERROR:                                                       │
│       └─ If failure_mode_deny: close, else: continue                │
│                                                                      │
│  onData(buffer, end_stream)                                          │
│    → Return Continue (rate limit already checked)                   │
└─────────────────────────────────────────────────────────────────────┘
```

### Descriptor Structure

Rate limit descriptors are key-value pairs sent to the rate limit service:

```yaml
descriptors:
  - entries:
      - key: "remote_address"
        value: "1.2.3.4"
  - entries:
      - key: "destination_cluster"
        value: "backend_cluster"
      - key: "request_path"
        value: "/api/users"
```

The rate limit service returns:
- `OK`: Under limit
- `OVER_LIMIT`: Rate limit exceeded
- `ERROR`: Service unavailable

### Class Structure

```cpp
class Config {
  - string domain_
  - vector<RateLimit::Descriptor> original_descriptors_
  - vector<unique_ptr<FormatterImpl>> substitution_formatters_
  - InstanceStats stats_
  - Runtime::Loader& runtime_
  - bool failure_mode_deny_

  + domain() → string&
  + descriptors() → vector<Descriptor>&
  + applySubstitutionFormatter(stream_info) → vector<Descriptor>
  + failureModeAllow() → bool
};

class Filter : public Network::ReadFilter,
               public Network::ConnectionCallbacks,
               public RequestCallbacks {
  - ConfigSharedPtr config_
  - ClientPtr client_
  - Network::ReadFilterCallbacks* filter_callbacks_
  - bool calling_check_{false}

  // Network::ReadFilter
  + onNewConnection() → StopIteration
  + onData(buffer, end_stream) → Continue
  + initializeReadFilterCallbacks(callbacks)

  // RequestCallbacks
  + complete(response)

  // Network::ConnectionCallbacks
  + onEvent(event)
};
```

### Configuration Example

```yaml
filters:
  - name: envoy.filters.network.ratelimit
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.ratelimit.v3.RateLimit
      stat_prefix: tcp_rate_limit

      # Rate limit service
      domain: tcp_service

      # Descriptors (what to rate limit on)
      descriptors:
        - entries:
            - key: "remote_address"
              value: "%DOWNSTREAM_REMOTE_ADDRESS%"  # Substitution formatter
        - entries:
            - key: "destination_cluster"
              value: "backend_cluster"

      # Failure mode
      failure_mode_deny: false

      # Rate limit service cluster
      rate_limit_service:
        grpc_service:
          envoy_grpc:
            cluster_name: ratelimit_cluster
        transport_api_version: V3
```

### Rate Limit Service Configuration

The external rate limit service (e.g., Lyft ratelimit) configuration:

```yaml
domain: tcp_service
descriptors:
  - key: remote_address
    rate_limit:
      unit: minute
      requests_per_unit: 100

  - key: destination_cluster
    value: backend_cluster
    rate_limit:
      unit: second
      requests_per_unit: 10
```

### Stats
- `ratelimit.<stat_prefix>.total`: Total rate limit checks
- `ratelimit.<stat_prefix>.ok`: Allowed connections
- `ratelimit.<stat_prefix>.over_limit`: Rate limited connections
- `ratelimit.<stat_prefix>.error`: Rate limit service errors
- `ratelimit.<stat_prefix>.failure_mode_allowed`: Allowed on failure
- `ratelimit.<stat_prefix>.active`: Active rate limit calls

### Use Cases
1. **API rate limiting**: Limit connections per IP
2. **DDoS protection**: Distributed rate limiting across fleet
3. **Fair usage**: Enforce per-tenant quotas
4. **Cost control**: Limit expensive operations

---

## 6. Connection Limit

### Overview
The connection limit filter enforces a **hard limit** on concurrent connections per listener. Unlike rate limiting (which limits connection rate), this limits **total active connections**.

### Purpose
- **Resource protection**: Prevent connection exhaustion
- **Capacity management**: Enforce connection quotas
- **Graceful degradation**: Reject excess connections quickly
- **Per-listener limits**: Independent limits per listener

### Key Features
- Atomic connection counting
- Runtime-configurable enable/disable
- Optional delay before rejection (log then close)
- Per-listener, not per-client
- Very low overhead (atomic increment/decrement)

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  Connection Limit Filter (Non-Terminal)                             │
│                                                                      │
│  onNewConnection()                                                   │
│    → If !config_->enabled(): Return Continue                        │
│    → If config_->incrementConnectionWithinLimit():                  │
│       ├─ registered_ = true                                          │
│       └─ Return Continue (connection allowed)                       │
│    → Else (over limit):                                              │
│       ├─ If delay configured:                                        │
│       │   ├─ is_rejected_ = true                                    │
│       │   ├─ Schedule delay timer                                   │
│       │   └─ Return Continue (delay, then close)                    │
│       └─ Else:                                                       │
│           ├─ connection_.close()                                     │
│           └─ Return StopIteration                                    │
│                                                                      │
│  onData(buffer, end_stream)                                          │
│    → Return Continue                                                 │
│                                                                      │
│  onEvent(ConnectionEvent::RemoteClose | LocalClose)                 │
│    → If registered_: config_->decrementConnection()                 │
│    → If delay_timer_: reset timer                                   │
└─────────────────────────────────────────────────────────────────────┘
```

### Atomic Counting

```cpp
bool Config::incrementConnectionWithinLimit() {
  uint64_t current = connections_.load();
  while (current < max_connections_) {
    if (connections_.compare_exchange_weak(current, current + 1)) {
      stats_.active_connections_.inc();
      return true;
    }
  }
  stats_.limited_connections_.inc();
  return false;
}

void Config::decrementConnection() {
  connections_--;
  stats_.active_connections_.dec();
}
```

### Class Structure

```cpp
class Config {
  - Runtime::FeatureFlag enabled_
  - ConnectionLimitStats stats_
  - uint64 max_connections_
  - atomic<uint64> connections_
  - optional<chrono::milliseconds> delay_

  + incrementConnectionWithinLimit() → bool
  + incrementConnection()
  + decrementConnection()
  + enabled() → bool
  + delay() → optional<chrono::milliseconds>
};

class Filter : public Network::ReadFilter,
               public Network::ConnectionCallbacks {
  - ConfigSharedPtr config_
  - Network::ReadFilterCallbacks* read_callbacks_
  - Event::TimerPtr delay_timer_
  - bool is_rejected_{false}
  - bool registered_{false}

  // Network::ReadFilter
  + onNewConnection() → Continue | StopIteration
  + onData(buffer, end_stream) → Continue
  + initializeReadFilterCallbacks(callbacks)

  // Network::ConnectionCallbacks
  + onEvent(event)

  // Helper methods
  - resetTimerState()
};
```

### Configuration Example

```yaml
filters:
  - name: envoy.filters.network.connection_limit
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.connection_limit.v3.ConnectionLimit
      stat_prefix: connection_limit

      # Maximum concurrent connections
      max_connections: 1000

      # Optional delay before closing (for logging)
      delay: 1s

      # Runtime enable/disable
      runtime_enabled:
        default_value: true
        runtime_key: connection_limit.enabled
```

### Stats
- `connection_limit.<stat_prefix>.active_connections`: Current active connections
- `connection_limit.<stat_prefix>.limited_connections`: Rejected connections

### Use Cases
1. **Resource protection**: Prevent listener overload
2. **Capacity enforcement**: Hard limits per listener
3. **Graceful degradation**: Reject excess load quickly
4. **Testing**: Simulate resource constraints

---

## 7. SNI Cluster

### Overview
The SNI cluster filter sets the upstream cluster name based on the **SNI** (Server Name Indication) field from the TLS connection. This enables dynamic routing based on the requested hostname.

### Purpose
- **SNI-based routing**: Route to cluster matching SNI
- **TLS-based routing**: Use TLS metadata for decisions
- **Virtual hosting at L4**: Route without HTTP parsing
- **Multi-tenant proxying**: Tenant-specific clusters

### Key Features
- Simple: maps SNI directly to cluster name
- Requires TLS Inspector listener filter
- Fails closed if SNI not present
- Very lightweight (no external calls)

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  SNI Cluster Filter (Non-Terminal)                                  │
│                                                                      │
│  onNewConnection()                                                   │
│    → sni = connection.requestedServerName()                         │
│    → If sni.empty():                                                 │
│       └─ connection.close() (no SNI, fail closed)                   │
│    → Else:                                                           │
│       ├─ connection.streamInfo().setRequestedServerName(sni)        │
│       └─ connection.streamInfo().filterState().setData(             │
│             "envoy.upstream.dynamic_host", sni)                     │
│    → Return Continue                                                 │
│                                                                      │
│  onData(buffer, end_stream)                                          │
│    → Return Continue                                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### SNI to Cluster Mapping

The filter expects the **cluster name to match the SNI exactly**. For example:
- SNI: `api.example.com` → Cluster: `api.example.com`
- SNI: `backend.internal` → Cluster: `backend.internal`

If the cluster doesn't exist, the connection will fail during upstream connection establishment.

### Class Structure

```cpp
class SniClusterFilter : public Network::ReadFilter {
  - Network::ReadFilterCallbacks* read_callbacks_

  // Network::ReadFilter
  + onNewConnection() → Continue
  + onData(buffer, end_stream) → Continue
  + initializeReadFilterCallbacks(callbacks)
};
```

### Sequence Diagram

```
TLS Inspector    SNI Cluster Filter    Cluster Manager
      │                  │                    │
      │─SNI="api.example.com"─►               │
      │                  │                    │
      │                  │─setRequestedServerName("api.example.com")
      │                  │─setFilterState("envoy.upstream.dynamic_host", "api.example.com")
      │                  │                    │
      │                  │─getCluster()──────►│
      │                  │  ("api.example.com")│
      │                  │◄─cluster───────────│
```

### Configuration Example

```yaml
listeners:
  - name: tls_listener
    address:
      socket_address:
        address: 0.0.0.0
        port_value: 8443

    listener_filters:
      # REQUIRED: TLS Inspector to extract SNI
      - name: envoy.filters.listener.tls_inspector
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector

    filter_chains:
      - filters:
          # SNI Cluster filter
          - name: envoy.filters.network.sni_cluster
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.sni_cluster.v3.SniCluster

          # Terminal filter (e.g., TCP proxy)
          - name: envoy.filters.network.tcp_proxy
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy
              stat_prefix: sni_tcp
              # No cluster specified - uses SNI from filter state

        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.DownstreamTlsContext
            common_tls_context:
              tls_certificates:
                - certificate_chain: { filename: "/etc/certs/cert.pem" }
                  private_key: { filename: "/etc/certs/key.pem" }

# Clusters must match SNI names
clusters:
  - name: api.example.com
    connect_timeout: 1s
    type: STRICT_DNS
    load_assignment:
      cluster_name: api.example.com
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: api-backend.internal
                    port_value: 443

  - name: www.example.com
    connect_timeout: 1s
    type: STRICT_DNS
    load_assignment:
      cluster_name: www.example.com
      endpoints:
        - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: www-backend.internal
                    port_value: 443
```

### Use Cases
1. **Multi-tenant proxying**: Route tenants by SNI
2. **Virtual hosting**: L4 routing without HTTP parsing
3. **TLS passthrough**: Route encrypted traffic without termination
4. **Dynamic routing**: Cluster selection based on TLS metadata

---

## 8. Direct Response

### Overview
The direct response filter immediately sends a static response and closes the connection. It's useful for health checks, placeholders, or blocking specific connections.

### Purpose
- **Health checks**: Respond to TCP health probes
- **Static responses**: Send fixed data without upstream
- **Connection rejection**: Send message before closing
- **Testing**: Simulate responses for development

### Key Features
- Sends response immediately on connection
- Closes connection after response (half-close enabled)
- No upstream connection required
- Minimal overhead

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  Direct Response Filter (Terminal)                                  │
│                                                                      │
│  onNewConnection()                                                   │
│    → connection.write(response_)                                     │
│    → connection.close(ConnectionCloseType::FlushWrite)               │
│    → Return StopIteration (terminal filter)                         │
│                                                                      │
│  onData(buffer, end_stream)                                          │
│    → Return Continue (no processing needed)                          │
└─────────────────────────────────────────────────────────────────────┘
```

### Class Structure

```cpp
class DirectResponseFilter : public Network::ReadFilter {
  - string response_
  - Network::ReadFilterCallbacks* read_callbacks_

  + DirectResponseFilter(response)

  // Network::ReadFilter
  + onNewConnection() → StopIteration
  + onData(buffer, end_stream) → Continue
  + initializeReadFilterCallbacks(callbacks)
};
```

### Sequence Diagram

```
Client          Direct Response Filter
   │                    │
   │──TCP Connect──────►│
   │◄─SYN/ACK───────────│
   │                    │
   │                    ├─write(response_)
   │◄─Response Data─────│
   │                    ├─close(FlushWrite)
   │◄─FIN───────────────│
   │──FIN──────────────►│
```

### Configuration Example

```yaml
filter_chains:
  - filters:
      - name: envoy.filters.network.direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            inline_string: "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK"

# Or with file source
filter_chains:
  - filters:
      - name: envoy.filters.network.direct_response
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.direct_response.v3.Config
          response:
            filename: "/etc/envoy/response.txt"
```

### Use Cases
1. **Health check endpoints**: Return "OK" for TCP health probes
2. **Maintenance mode**: Return error message without upstream
3. **Testing**: Simulate backend responses
4. **Blocking**: Send rejection message before closing

---

## 9. Echo

### Overview
The echo filter is a **terminal filter** that echoes back all received data to the client. It's primarily used for testing and debugging.

### Purpose
- **Testing**: Verify connection and data flow
- **Debugging**: Inspect data being sent
- **Development**: Simple server for development
- **Diagnostics**: Test TCP connectivity

### Key Features
- Echoes all received data back to sender
- Terminal filter (no upstream connection)
- Minimal implementation
- No configuration options

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  Echo Filter (Terminal)                                             │
│                                                                      │
│  onNewConnection()                                                   │
│    → Return Continue                                                 │
│                                                                      │
│  onData(buffer, end_stream)                                          │
│    → read_callbacks_->connection().write(buffer, end_stream)        │
│    → buffer.drain(buffer.length())                                   │
│    → If end_stream:                                                  │
│       └─ connection.close(ConnectionCloseType::FlushWrite)          │
│    → Return StopIteration (terminal filter)                         │
└─────────────────────────────────────────────────────────────────────┘
```

### Class Structure

```cpp
class EchoFilter : public Network::ReadFilter {
  - Network::ReadFilterCallbacks* read_callbacks_

  // Network::ReadFilter
  + onNewConnection() → Continue
  + onData(buffer, end_stream) → StopIteration
  + initializeReadFilterCallbacks(callbacks)
};
```

### Implementation

```cpp
Network::FilterStatus EchoFilter::onData(Buffer::Instance& data, bool end_stream) {
  ENVOY_CONN_LOG(trace, "echo: {} bytes", read_callbacks_->connection(), data.length());

  // Write data back to client
  read_callbacks_->connection().write(data, end_stream);

  // Drain buffer (data consumed)
  data.drain(data.length());

  if (end_stream) {
    read_callbacks_->connection().close(Network::ConnectionCloseType::FlushWrite);
  }

  return Network::FilterStatus::StopIteration;
}
```

### Configuration Example

```yaml
filter_chains:
  - filters:
      - name: envoy.filters.network.echo
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.echo.v3.Echo
```

### Testing with Echo Filter

```bash
# Start Envoy with echo filter on port 9000
$ envoy -c echo-config.yaml

# Test with telnet
$ telnet localhost 9000
Trying 127.0.0.1...
Connected to localhost.
hello
hello
world
world
^]
telnet> quit

# Test with netcat
$ echo "test message" | nc localhost 9000
test message
```

### Use Cases
1. **Connection testing**: Verify TCP connectivity
2. **Data inspection**: See what data is being sent
3. **Load testing**: Simple server for benchmarking
4. **Development**: Quick TCP server for prototyping

---

## 10. Comparison Matrix

| Feature | HCM | TCP Proxy | Ext Authz | RBAC | Rate Limit | Conn Limit | SNI Cluster | Direct Resp | Echo |
|---------|-----|-----------|-----------|------|------------|------------|-------------|-------------|------|
| **Type** | Terminal | Terminal | Non-Terminal | Non-Terminal | Non-Terminal | Non-Terminal | Non-Terminal | Terminal | Terminal |
| **Protocol** | HTTP/1.1/2/3 | Protocol-agnostic | Any | Any | Any | Any | TLS | Any | Any |
| **External Service** | ❌ No | ❌ No | ✅ Yes (gRPC/HTTP) | ❌ No | ✅ Yes (gRPC) | ❌ No | ❌ No | ❌ No | ❌ No |
| **Upstream Connection** | ✅ Yes (via router) | ✅ Yes | ❌ No | ❌ No | ❌ No | ❌ No | ❌ No (routing) | ❌ No | ❌ No |
| **onNewConnection** | Setup codec | Establish upstream | Call authz service | Continue | Call rate limit | Check limit | Set cluster | Send & close | Continue |
| **onData** | Decode HTTP | Proxy data | Continue | Check policy | Continue | Continue | Continue | Continue | Echo back |
| **Blocks Connection** | ❌ No | ❌ No (terminal) | ✅ Yes (until authz) | ✅ Yes (if denied) | ✅ Yes (until check) | ✅ Yes (if over) | ❌ No | ✅ Yes (terminal) | ✅ Yes (terminal) |
| **Performance** | Medium | Low | High (network call) | Very Low | High (network call) | Very Low | Very Low | Very Low | Very Low |
| **Stats** | 50+ metrics | 10+ metrics | 6 metrics | 4 metrics | 6 metrics | 2 metrics | None | None | None |
| **Config Complexity** | Very High | Medium | High | High (policies) | Medium | Low | Very Low | Very Low | Very Low |
| **Failure Mode** | N/A | N/A | Allow/Deny | N/A | Allow/Deny | N/A | Close | N/A | N/A |
| **Common Position** | Last (terminal) | Last (terminal) | 1-2 (early) | 1-2 (early) | 2-3 (after auth) | First | Before terminal | Last (if used) | Last (if used) |

### Filter Ordering Best Practices

**Recommended order**:
1. `connection_limit` - Fast rejection before processing
2. `rbac` - Fast local authorization (no external call)
3. `ext_authz` - External authorization (if needed)
4. `ratelimit` - Distributed rate limiting
5. `sni_cluster` - Set routing metadata (if TLS)
6. `tcp_proxy` or `http_connection_manager` - Terminal filter

**Rationale**:
- Connection limit first (fastest rejection, protect resources)
- RBAC before ext_authz (local check faster than network call)
- Rate limiting after authorization (don't rate limit denied connections)
- SNI cluster before terminal filter (sets routing)
- Terminal filter last (establishes upstream connection)

---

## 11. Best Practices

### Security

1. **Defense in Depth**
   - Use multiple authorization layers (RBAC + ext_authz)
   - Implement both L4 (network filters) and L7 (HTTP filters) controls
   - Enable mTLS with certificate validation

2. **Fail Secure**
   - Set `failure_mode_allow: false` for ext_authz and rate limit in production
   - Use RBAC DENY policies for sensitive resources
   - Close connections on policy evaluation errors

3. **Rate Limiting**
   - Combine connection_limit (per-listener) with ratelimit (distributed)
   - Use different rate limits for authenticated vs unauthenticated clients
   - Monitor rate limit rejection metrics

4. **Connection Limits**
   - Set conservative connection limits based on capacity
   - Leave headroom for legitimate traffic spikes
   - Use delay option for observability before closing

### Performance

1. **Filter Ordering**
   - Put cheap filters first (connection_limit, rbac)
   - Put expensive filters after rejection (ext_authz after rbac)
   - Terminal filters always last

2. **External Service Calls**
   - Set aggressive timeouts for ext_authz and rate limit (< 100ms)
   - Use connection pooling for authz/rate limit services
   - Monitor authz/rate limit service latency

3. **Resource Management**
   - Configure appropriate buffer limits
   - Set idle timeouts to free resources
   - Use connection draining for graceful shutdown

### Monitoring

**Key Metrics**:
- Connection rate: `downstream_cx_total`
- Active connections: `downstream_cx_active`
- Authorization denials: `ext_authz.denied`, `rbac.denied`
- Rate limiting: `ratelimit.over_limit`
- Connection limit rejections: `connection_limit.limited_connections`

**Alerts**:
- Spike in authorization denials (potential attack)
- High rate limit rejections (capacity issue or attack)
- Connection limit consistently hit (need more capacity)
- Authorization service errors (service degradation)

### Configuration Patterns

**Public Edge Proxy**:
```yaml
filters:
  - connection_limit      # Protect from connection floods
  - rbac                  # IP allowlist/denylist
  - ext_authz             # API key validation
  - ratelimit             # Per-client rate limiting
  - http_connection_manager  # HTTP processing
```

**Internal Service Mesh**:
```yaml
filters:
  - rbac                  # mTLS authz (SPIFFE IDs)
  - http_connection_manager  # HTTP + routing
```

**Database Proxy**:
```yaml
filters:
  - ext_authz             # Authenticate database clients
  - connection_limit      # Limit concurrent DB connections
  - tcp_proxy             # Proxy to database cluster
```

**TLS Passthrough**:
```yaml
listener_filters:
  - tls_inspector         # Extract SNI
filters:
  - sni_cluster           # Route by SNI
  - tcp_proxy             # Proxy encrypted traffic
```

### Testing

1. **Unit Tests**
   - Mock external services (authz, rate limit)
   - Test policy evaluation logic
   - Verify filter status returns (Continue/StopIteration)

2. **Integration Tests**
   - Test full filter chain
   - Verify external service integration
   - Test failure modes (service unavailable)

3. **Load Tests**
   - Verify connection limit enforcement
   - Test rate limiting under load
   - Measure authorization latency impact

4. **Security Tests**
   - Test authorization bypass attempts
   - Verify TLS certificate validation
   - Test policy edge cases

### Debugging

1. **Enable Debug Logging**
   ```yaml
   admin:
     access_log_path: /dev/null
     address:
       socket_address:
         address: 0.0.0.0
         port_value: 9901
   ```

   Runtime logging:
   ```bash
   # Enable debug logging for filters
   curl -X POST "http://localhost:9901/logging?filter=debug"

   # Check filter stats
   curl "http://localhost:9901/stats" | grep -E "(rbac|ext_authz|connection_limit)"
   ```

2. **Common Issues**
   - **Ext authz timeout**: Check authz service latency, increase timeout
   - **RBAC always denying**: Verify policy logic, check shadow mode first
   - **Connection limit not working**: Check runtime flag enabled
   - **SNI cluster failed**: Verify TLS Inspector in listener filters

3. **Tracing**
   - Enable access logs to see filter decisions
   - Use dynamic metadata to track authz results
   - Monitor filter-specific stats

### Advanced Topics

**Dynamic Configuration**:
- Use xDS for dynamic filter updates
- Runtime flags for feature toggles
- Dynamic metadata for per-request state

**Custom Filters**:
- Implement custom network filters in C++
- Use Wasm for portable custom filters
- Dynamic modules for out-of-tree extensions

**High Availability**:
- Deploy redundant Envoy instances
- Use health checks for upstream services
- Implement graceful shutdown and drain

---

## Appendix: Filter Chain Execution Flow

### Complete Example

```
┌─────────────────────────────────────────────────────────────────────┐
│ 1. Socket Accept (Listener)                                          │
│    ↓                                                                  │
│ 2. Listener Filter Chain                                             │
│    ├─ tls_inspector: Extract SNI, ALPN                               │
│    └─ http_inspector: Detect HTTP/2 (if plaintext)                   │
│    ↓                                                                  │
│ 3. Filter Chain Matching                                             │
│    Based on: SNI, ALPN, destination port, source IP                  │
│    ↓                                                                  │
│ 4. Network Filter Chain                                              │
│    ├─ connection_limit:                                              │
│    │   onNewConnection() → Check limit → Continue or Close           │
│    ├─ rbac:                                                           │
│    │   onNewConnection() → Continue                                  │
│    │   onData() → Check policy → Continue or Close                   │
│    ├─ ext_authz:                                                      │
│    │   onNewConnection() → Call authz service → StopIteration        │
│    │   onComplete() → continueReading() or close()                   │
│    ├─ ratelimit:                                                      │
│    │   onNewConnection() → Call rate limit service → StopIteration   │
│    │   complete() → continueReading() or close()                     │
│    └─ http_connection_manager: (TERMINAL)                            │
│        onNewConnection() → Create codec                               │
│        onData() → Decode HTTP → HTTP filter chain → Route            │
│    ↓                                                                  │
│ 5. Upstream Connection                                               │
│    HTTP Connection Manager or TCP Proxy establishes connection       │
│    ↓                                                                  │
│ 6. Data Flow                                                          │
│    Downstream ←→ Network Filters ←→ Terminal Filter ←→ Upstream      │
└─────────────────────────────────────────────────────────────────────┘
```

### Read Filter Callbacks

```cpp
class ReadFilterCallbacks {
  // Connection access
  virtual Connection& connection() = 0;

  // Continue filter chain
  virtual void continueReading() = 0;

  // Upstream connection pool
  virtual Upstream::ClusterManager& clusterManager() = 0;

  // Request info
  virtual StreamInfo::StreamInfo& streamInfo() = 0;
};
```

### Write Filter Callbacks

```cpp
class WriteFilterCallbacks {
  // Connection access
  virtual Connection& connection() = 0;

  // Upstream connection info
  virtual const Upstream::HostDescription* upstreamHost() = 0;
};
```

---

## Conclusion

Envoy's network filters provide powerful L4 processing capabilities that enable:
- **Protocol handling**: HTTP, TCP, and custom protocols
- **Security**: Authorization, authentication, and access control
- **Traffic management**: Rate limiting, connection limiting
- **Routing**: Dynamic cluster selection, SNI-based routing
- **Observability**: Detailed metrics, access logs, tracing

**Key Takeaways**:
- **Network filters operate at L4** after listener filters
- **Terminal filters** establish upstream connections
- **Non-terminal filters** process/authorize without upstream connections
- **Order matters** - cheap filters first, external calls after local checks
- **Combine filters** for defense in depth
- **Monitor metrics** to detect issues and attacks

For implementation details, refer to source files in:
- `source/extensions/filters/network/<filter_name>/`

For API documentation:
- `api/envoy/extensions/filters/network/<filter_name>/v3/*.proto`

For additional network filters not covered here:
- **redis_proxy**: Redis protocol proxy with command splitting
- **mongo_proxy**: MongoDB protocol proxy with query analysis
- **thrift_proxy**: Apache Thrift protocol proxy
- **dubbo_proxy**: Apache Dubbo protocol proxy
- **zookeeper_proxy**: Apache ZooKeeper protocol proxy
- **wasm**: WebAssembly extensibility
- **dynamic_modules**: Dynamic library loading
