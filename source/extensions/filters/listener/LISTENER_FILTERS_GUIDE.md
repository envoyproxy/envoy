# Envoy Listener Filters - Comprehensive Guide

## Table of Contents
1. [Introduction](#introduction)
2. [TLS Inspector](#1-tls-inspector)
3. [Proxy Protocol](#2-proxy-protocol)
4. [HTTP Inspector](#3-http-inspector)
5. [Original Destination](#4-original-destination)
6. [Original Source](#5-original-source)
7. [Local Rate Limit](#6-local-rate-limit)
8. [Dynamic Modules](#7-dynamic-modules)
9. [Comparison Matrix](#8-comparison-matrix)
10. [Best Practices](#9-best-practices)

---

## Introduction

Listener filters in Envoy operate on **newly accepted sockets** before the connection is handed to the network filter chain. They have access to raw socket data and can:

- **Inspect** incoming data without consuming it (peek mode)
- **Modify** socket metadata (addresses, transport protocol, requested server name)
- **Accept or Reject** connections based on criteria
- **Extract** information for routing decisions (SNI, ALPN, original destination)

### Listener Filter Lifecycle

```
┌─────────────────────────────────────────────────────────────────┐
│  1. Socket Accept                                               │
│     ↓                                                            │
│  2. Listener Filter Chain (ordered)                             │
│     ├─ Filter.onAccept(callbacks)                               │
│     │   Returns: Continue or StopIteration                      │
│     ├─ If StopIteration: wait for data                          │
│     └─ Filter.onData(buffer)                                    │
│         Returns: Continue or StopIteration                      │
│     ↓                                                            │
│  3. Filter Chain Matching (based on filter-extracted metadata)  │
│     ↓                                                            │
│  4. Network Filter Chain Execution                              │
└─────────────────────────────────────────────────────────────────┘
```

### Key Interfaces

**Network::ListenerFilter**
```cpp
class ListenerFilter {
  virtual FilterStatus onAccept(ListenerFilterCallbacks& cb) = 0;
  virtual FilterStatus onData(ListenerFilterBuffer& buffer) = 0;
  virtual size_t maxReadBytes() const = 0;
  virtual void onClose() = 0;
};
```

**FilterStatus**
- `Continue`: Move to next filter in chain
- `StopIteration`: Pause and wait for more data (only valid when `maxReadBytes() > 0`)

---

## 1. TLS Inspector

### Overview
The TLS Inspector peeks at the TLS ClientHello message to extract connection metadata without terminating TLS. This enables SNI-based routing and protocol detection before the connection reaches the filter chain.

### Purpose
- **Extract SNI** (Server Name Indication) for virtual host routing
- **Detect ALPN** (Application-Layer Protocol Negotiation) protocols
- **Identify TLS vs plaintext** connections
- **Generate fingerprints** (JA3, JA4) for security analysis
- **Set transport protocol** to enable filter chain matching

### Key Features
- Uses BoringSSL's `TLS_with_buffers_method()` for zero-copy parsing
- Early callback mechanism (`select_certificate_cb`) to extract ClientHello
- Optional JA3/JA4 fingerprinting for client identification
- Configurable ClientHello size limits
- Dynamic buffer sizing based on handshake progress

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  TlsInspector::Filter                                               │
│                                                                      │
│  onAccept(cb)                                                        │
│    → Store callback reference                                       │
│    → Return StopIteration (need to peek at ClientHello)             │
│                                                                      │
│  onData(buffer)                                                      │
│    → parseClientHello()                                              │
│       ├─ Create SSL object with memory BIO                          │
│       ├─ SSL_set_accept_state() + SSL_do_handshake()                │
│       ├─ In select_certificate_cb:                                   │
│       │   ├─ Extract SNI → setRequestedServerName()                 │
│       │   ├─ Extract ALPN → setRequestedApplicationProtocols()      │
│       │   ├─ Generate JA3 hash (if enabled)                         │
│       │   └─ Generate JA4 hash (if enabled)                         │
│       └─ Set transport protocol to "tls"                             │
│    → Return Continue (done) or StopIteration (need more data)       │
└─────────────────────────────────────────────────────────────────────┘
```

### Class Structure

```cpp
class Config {
  - SSL_CTX* ssl_ctx_                      // Pre-configured SSL context
  - bool enable_ja3_fingerprinting_         // JA3 hash generation
  - bool enable_ja4_fingerprinting_         // JA4 hash generation
  - uint32_t max_client_hello_size_         // Default: TLS_MAX_CLIENT_HELLO
  - uint32_t initial_read_buffer_size_      // Default: 64KB

  + newSsl() → UniquePtr<SSL>               // Create SSL object per connection
};

class Filter : public Network::ListenerFilter {
  - ConfigSharedPtr config_
  - ListenerFilterCallbacks* cb_
  - UniquePtr<SSL> ssl_                     // BoringSSL handle
  - uint64_t read_                          // Bytes processed
  - uint32_t requested_read_bytes_          // Dynamic buffer size

  + onAccept(cb) → StopIteration
  + onData(buffer) → Continue | StopIteration | Error
  - parseClientHello(data, len)
  - onServername(name)                      // SNI callback
  - onALPN(data, len)                       // ALPN callback
  - createJA3Hash(SSL_CLIENT_HELLO)
  - createJA4Hash(SSL_CLIENT_HELLO)
};
```

### Sequence Diagram

```
Client          ActiveTcpSocket      TlsInspector         BoringSSL
  │                   │                    │                    │
  │──TLS ClientHello──►                   │                    │
  │                   │──onAccept()──────► │                    │
  │                   │◄─StopIteration──── │                    │
  │                   │                    │                    │
  │                   │──onData(buffer)──► │                    │
  │                   │                    │──SSL_do_handshake──►
  │                   │                    │                    │
  │                   │                    │◄─select_cert_cb──┤
  │                   │                    │  (ClientHello)     │
  │                   │                    ├─ onServername()   │
  │                   │                    ├─ onALPN()         │
  │                   │                    ├─ createJA3Hash()  │
  │                   │                    └─ createJA4Hash()  │
  │                   │                    │                    │
  │                   │◄────Continue────── │                    │
  │                   │  (SNI, ALPN set)   │                    │
```

### Implementation Details

**SSL Context Setup** (in Config constructor):
```cpp
ssl_ctx_ = SSL_CTX_new(TLS_with_buffers_method());
SSL_CTX_set_options(ssl_ctx_, SSL_OP_NO_TICKET);
SSL_CTX_set_select_certificate_cb(ssl_ctx_, selectCertificateCallback);
SSL_CTX_set_min_proto_version(ssl_ctx_, TLS_MIN_SUPPORTED_VERSION);
SSL_CTX_set_max_proto_version(ssl_ctx_, TLS_MAX_SUPPORTED_VERSION);
```

**ClientHello Parsing**:
```cpp
ParseState Filter::parseClientHello(const void* data, size_t len,
                                     uint64_t bytes_already_processed) {
  // Create memory BIO from peeked data
  BIO_write(bio, data, len);
  SSL_set0_rbio(ssl_.get(), bio);

  // Attempt handshake - this triggers select_certificate_cb
  int handshake_status = SSL_do_handshake(ssl_.get());

  return getParserState(handshake_status);
}
```

**JA3 Fingerprint**: Hash of TLS version, cipher suites, extensions, elliptic curves, and point formats
**JA4 Fingerprint**: Enhanced version with additional ClientHello attributes

### Configuration Example

```yaml
listener_filters:
  - name: envoy.filters.listener.tls_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.tls_inspector.v3.TlsInspector
      enable_ja3_fingerprinting: true
      enable_ja4_fingerprinting: true
      max_client_hello_size: 65536
      initial_read_buffer_size: 8192
```

### Stats
- `tls_found` / `tls_not_found`: TLS detection
- `sni_found` / `sni_not_found`: SNI extraction
- `alpn_found` / `alpn_not_found`: ALPN extraction
- `client_hello_too_large`: ClientHello exceeds limit
- `bytes_processed` (histogram): Bytes read per connection

### Use Cases
1. **SNI-based routing**: Route to different filter chains based on hostname
2. **Protocol detection**: Distinguish h2 from http/1.1 connections
3. **Security**: JA3/JA4 fingerprinting for bot detection
4. **Mixed TLS/plaintext**: Detect protocol on unified listener

---

## 2. Proxy Protocol

### Overview
Parses the HAProxy PROXY protocol header (v1 or v2) to recover the original client and destination addresses when connections pass through a TCP proxy or load balancer.

### Purpose
- **Preserve client address** through NAT/proxies
- **Extract TLV extensions** (v2 only) for additional metadata
- **Support transparent proxying** architectures
- **Enable proper logging** with real client IPs

### Key Features
- Supports PROXY protocol v1 (text) and v2 (binary)
- TLV (Type-Length-Value) extension parsing
- Optional pass-through of unrecognized TLVs
- Can allow connections without PROXY header
- Validates protocol version allowlists

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  ProxyProtocol::Filter                                              │
│                                                                      │
│  onAccept(cb)                                                        │
│    → Return StopIteration (wait for PROXY header)                   │
│                                                                      │
│  onData(buffer)                                                      │
│    → readProxyHeader()                                               │
│       ├─ Detect version:                                             │
│       │   V1: "PROXY TCP4|TCP6 ..."                                 │
│       │   V2: 0x0D 0x0A 0x0D 0x0A 0x00 0x0D 0x0A 0x51 0x55 0x49 0x54│
│       ├─ parseV1Header() or parseV2Header()                         │
│       │   → Extract remote_address_ and local_address_              │
│       └─ readExtensions() [V2 only]                                 │
│           → Parse TLVs into parsed_tlvs_                             │
│    → restoreLocalAddress(local_address_)                            │
│    → setRemoteAddress(remote_address_)                              │
│    → Store TLVs in filter state / dynamic metadata                  │
│    → Drain header bytes from buffer                                 │
│    → Return Continue                                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Protocol Format

**PROXY v1 (text)**:
```
PROXY TCP4 192.168.0.1 192.168.0.11 56324 443\r\n
      ^^   ^^^^^^^^^^^^ ^^^^^^^^^^^^ ^^^^^ ^^^
      │    │            │            │     └─ dest port
      │    │            │            └─ source port
      │    │            └─ destination IP
      │    └─ source IP
      └─ protocol family
```

**PROXY v2 (binary)**:
```
┌────────────────────────────────────────────────────────────┐
│ Signature (12 bytes): 0x0D0A0D0A 0x000D0A51 0x5549540A     │
├────────────────────────────────────────────────────────────┤
│ Version | Command (1 byte): 0x21 (v2, PROXY)               │
├────────────────────────────────────────────────────────────┤
│ Family | Transport (1 byte): 0x11 (AF_INET, STREAM)        │
├────────────────────────────────────────────────────────────┤
│ Length (2 bytes): total length of addresses + TLVs         │
├────────────────────────────────────────────────────────────┤
│ Addresses (variable, based on family):                     │
│   IPv4: src_addr(4) + dst_addr(4) + src_port(2) + dst(2)  │
│   IPv6: src_addr(16) + dst_addr(16) + src_port(2) + dst(2)│
├────────────────────────────────────────────────────────────┤
│ TLV Extensions (optional):                                 │
│   Type(1) + Length(2) + Value(variable)                    │
│   Common: PP2_TYPE_SSL (0x20), PP2_TYPE_UNIQUE_ID (0x05)  │
└────────────────────────────────────────────────────────────┘
```

### Class Structure

```cpp
class Config {
  - flat_hash_map<uint8_t, KeyValuePair> tlv_types_  // TLVs to extract
  - bool allow_requests_without_proxy_protocol_       // Pass-through mode
  - bool pass_all_tlvs_                               // Forward all TLVs
  - bool allow_v1_, allow_v2_                         // Version allowlist
  - TlvLocation tlv_location_                         // filter_state | metadata

  + isTlvTypeNeeded(type) → KeyValuePair*
  + isPassThroughTlvTypeNeeded(type) → bool
};

class Filter : public Network::ListenerFilter {
  - ConfigSharedPtr config_
  - optional<WireHeader> proxy_protocol_header_
  - ProxyProtocolTLVVector parsed_tlvs_
  - ProxyProtocolVersion header_version_          // NotFound | V1 | V2
  - size_t max_proxy_protocol_len_                // 108 (V1) or 216 (V2)

  + onAccept(cb) → StopIteration
  + onData(buffer) → Continue | StopIteration
  - readProxyHeader(buffer) → ReadOrParseState
  - parseV1Header(buf, len) → bool
  - parseV2Header(buf) → bool
  - readExtensions(buffer) → ReadOrParseState
};
```

### TLV Extensions (V2)

Common TLV types:
- `PP2_TYPE_ALPN` (0x01): Application protocol
- `PP2_TYPE_AUTHORITY` (0x02): Host header / authority
- `PP2_TYPE_SSL` (0x20): SSL/TLS information
  - Sub-TLVs: client cert, cipher, SNI
- `PP2_TYPE_UNIQUE_ID` (0x05): Unique connection ID
- `PP2_SUBTYPE_SSL_CN` (0x22): Client certificate CN

### Configuration Example

```yaml
listener_filters:
  - name: envoy.filters.listener.proxy_protocol
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.proxy_protocol.v3.ProxyProtocol
      allow_requests_without_proxy_protocol: false
      rules:
        - tlv_type: 0x01  # ALPN
          on_tlv_present:
            key: alpn
      pass_through_tlvs:
        match_type: INCLUDE_ALL
```

### Stats
- `v1.found` / `v2.found`: Version detection
- `v1.error` / `v2.error`: Parse errors
- `v1.disallowed` / `v2.disallowed`: Version not allowed
- `not_found_allowed`: No header, but allowed
- `not_found_disallowed`: No header, connection closed

### Use Cases
1. **AWS NLB / ALB**: Preserve client IP through load balancer
2. **Cloud environments**: Get real client address behind proxy
3. **Security logging**: Accurate client IP for audit trails
4. **Rate limiting**: Apply limits per real client IP

---

## 3. HTTP Inspector

### Overview
Detects HTTP protocol version (HTTP/1.0, HTTP/1.1, HTTP/2) on plaintext connections by peeking at the initial request bytes. Sets ALPN for routing without TLS.

### Purpose
- **Protocol detection** on non-TLS listeners
- **ALPN emulation** for plaintext HTTP
- **Filter chain matching** based on HTTP version
- **Mixed protocol support** on single port

### Key Features
- Detects HTTP/2 connection preface
- Uses HTTP/1 parser for request line parsing
- Sets requested application protocols (`h2c`, `http/1.1`, `http/1.0`)
- Only runs when transport is `raw_buffer` (not TLS)
- Configurable inspection size (up to 64KB)

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  HttpInspector::Filter                                              │
│                                                                      │
│  onAccept(cb)                                                        │
│    → If transport already detected (e.g., "tls"):                   │
│        Return Continue (skip inspection)                            │
│    → Else: Return StopIteration (need to peek)                      │
│                                                                      │
│  onData(buffer)                                                      │
│    → parseHttpHeader()                                               │
│       ├─ Check for HTTP/2 preface:                                  │
│       │   "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"                        │
│       │   → protocol_ = "HTTP/2"                                    │
│       ├─ Else try HTTP/1 parsing:                                   │
│       │   parser_->execute(data)                                     │
│       │   → protocol_ = "HTTP/1.1" or "HTTP/1.0"                    │
│       └─ done(success)                                               │
│           → setRequestedApplicationProtocols(["h2c"] or ["http/1.1"])│
│    → Return Continue                                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### HTTP/2 Connection Preface

```
PRI * HTTP/2.0\r\n
\r\n
SM\r\n
\r\n
```
This unique sequence (24 bytes) is sent by HTTP/2 clients before the first frame.

### Class Structure

```cpp
class Config {
  - HttpInspectorStats stats_
  + DEFAULT_INITIAL_BUFFER_SIZE = 8KB
  + MAX_INSPECT_SIZE = 64KB
};

class Filter : public Network::ListenerFilter {
  - ConfigSharedPtr config_
  - ListenerFilterCallbacks* cb_
  - unique_ptr<Http1::Parser> parser_         // Balsa or Legacy parser
  - string_view protocol_                      // "HTTP/1.0", "HTTP/1.1", "HTTP/2"
  - size_t requested_read_bytes_               // Dynamic sizing

  + onAccept(cb) → Continue | StopIteration
  + onData(buffer) → Continue | StopIteration
  - parseHttpHeader(data) → ParseState
  - done(success)
};
```

### Implementation Details

**HTTP/2 Detection**:
```cpp
constexpr absl::string_view HTTP2_CONNECTION_PREFACE = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";

if (data.starts_with(HTTP2_CONNECTION_PREFACE)) {
  protocol_ = "HTTP/2";
  done(true);
  return ParseState::Done;
}
```

**HTTP/1 Detection**:
```cpp
parser_->execute(data.data(), data.size());
if (parser_->hasError()) {
  return ParseState::Error;
}
if (parser_->headersComplete()) {
  protocol_ = parser_->isHttp11() ? "HTTP/1.1" : "HTTP/1.0";
  done(true);
  return ParseState::Done;
}
```

### Configuration Example

```yaml
listener_filters:
  - name: envoy.filters.listener.http_inspector
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.http_inspector.v3.HttpInspector
```

### Stats
- `http10_found`: HTTP/1.0 detected
- `http11_found`: HTTP/1.1 detected
- `http2_found`: HTTP/2 detected
- `http_not_found`: Not HTTP
- `read_error`: Parse error

### Use Cases
1. **Cleartext HTTP/2** (h2c): Detect and route HTTP/2 without TLS
2. **Protocol multiplexing**: Support HTTP/1 and HTTP/2 on same port
3. **Legacy compatibility**: Auto-detect HTTP/1.0 clients
4. **Development**: Test HTTP/2 without TLS certificates

---

## 4. Original Destination

### Overview
Retrieves the original destination address for connections redirected by iptables/netfilter (transparent proxy). Essential for Envoy to route to the intended backend.

### Purpose
- **Transparent proxying**: Get pre-NAT destination address
- **iptables integration**: Read `SO_ORIGINAL_DST` socket option
- **Dynamic listener selection**: Route to correct listener based on original IP
- **Envoy Internal addresses**: Support for internal routing with metadata

### Key Features
- No data peeking (maxReadBytes = 0)
- Runs entirely in `onAccept()`
- Platform-specific socket option queries
- Supports both IP and EnvoyInternal address types
- Filter state / metadata integration

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  OriginalDstFilter                                                  │
│                                                                      │
│  onAccept(cb)                                                        │
│    → switch (socket.addressType()):                                 │
│       ├─ IP:                                                         │
│       │   → getOriginalDst(socket)                                   │
│       │      [Linux: getsockopt(SO_ORIGINAL_DST)]                   │
│       │      [BSD: getsockopt(SO_ORIGINAL_DST) or PF_NAT ioctl]    │
│       │   → socket.restoreLocalAddress(original_address)            │
│       └─ EnvoyInternal:                                              │
│           → Read from dynamicMetadata or filterState                │
│           → restoreLocalAddress() / setRemoteAddress()              │
│    → Return Continue                                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### iptables Integration

**Redirect traffic to Envoy**:
```bash
# Redirect all traffic to port 15001
iptables -t nat -A PREROUTING -p tcp -j REDIRECT --to-port 15001

# Envoy listener config
listeners:
  - address:
      socket_address:
        address: 0.0.0.0
        port_value: 15001
    use_original_dst: true  # Key setting
```

### Class Structure

```cpp
class OriginalDstFilter : public Network::ListenerFilter {
  - TrafficDirection traffic_direction_

  + getOriginalDst(Socket&) → Address::InstanceConstSharedPtr
  + onAccept(cb) → Continue
  + maxReadBytes() → 0
  + onData(buffer) → Continue
};
```

### Platform Implementation

**Linux**:
```cpp
Address::InstanceConstSharedPtr getOriginalDst(Network::Socket& sock) {
  sockaddr_storage orig_addr;
  socklen_t addr_len = sizeof(orig_addr);

  int status = getsockopt(sock.ioHandle().fdDoNotUse(),
                          SOL_IP,
                          SO_ORIGINAL_DST,
                          &orig_addr,
                          &addr_len);

  if (status == 0) {
    return std::make_shared<Address::Ipv4Instance>(&orig_addr);
  }
  return nullptr;
}
```

**EnvoyInternal Address**:
```cpp
// Read from filter state
const auto* filter_state = cb.filterState().getDataReadOnly<Network::AddressObject>(
    FilterNames::get().LocalFilterStateKey);
if (filter_state) {
  cb.socket().restoreLocalAddress(filter_state->address());
}
```

### Configuration Example

```yaml
listener_filters:
  - name: envoy.filters.listener.original_dst
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.original_dst.v3.OriginalDst
      traffic_direction: INBOUND

# Enable original destination routing
listeners:
  - use_original_dst: true
```

### Use Cases
1. **Service mesh sidecar**: Intercept and route to original destination
2. **Transparent proxy**: iptables redirect without client awareness
3. **Port forwarding**: Forward to original port after REDIRECT
4. **Multi-tenant**: Route based on original destination IP

---

## 5. Original Source

### Overview
Captures the downstream (client) source address and attaches it as a socket option for upstream connections. Enables source IP preservation or partitioning.

### Purpose
- **Source IP preservation**: Pass client IP to upstream
- **Connection partitioning**: Partition upstream pools by client IP
- **Socket marking**: Set SO_MARK for routing policies
- **Logging enrichment**: Tag connections with source metadata

### Key Features
- No data peeking (maxReadBytes = 0)
- Runs in `onAccept()` only
- Adds socket options for upstream use
- Optional SO_MARK configuration
- IP-only (non-AF_UNIX sockets)

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  OriginalSrcFilter                                                  │
│                                                                      │
│  onAccept(cb)                                                        │
│    → address = socket.connectionInfoProvider().remoteAddress()     │
│    → If address->type() != IP:                                      │
│        Return Continue (skip)                                       │
│    → buildOriginalSrcOptions(address, config_.mark())               │
│       ├─ Create OriginalSrcSocketOption                             │
│       └─ Optional: SocketMarkOption(mark)                           │
│    → socket.addOptions(options)                                     │
│    → Return Continue                                                 │
└─────────────────────────────────────────────────────────────────────┘
```

### Socket Option Flow

```
┌─────────────┐        ┌──────────────────┐        ┌─────────────┐
│  Downstream │        │ OriginalSrcFilter│        │   Upstream  │
│   Socket    │───────►│  (Listener)      │───────►│   Socket    │
│             │        │                  │        │             │
│ 1.2.3.4:567 │        │ Store source IP  │        │ Apply IP as │
│             │        │ in socket option │        │ bind address│
└─────────────┘        └──────────────────┘        └─────────────┘
```

### Class Structure

```cpp
class Config {
  - uint32 mark_                    // Optional SO_MARK value
  + mark() → uint32
};

class OriginalSrcFilter : public Network::ListenerFilter {
  - Config config_

  + onAccept(cb) → Continue
  + maxReadBytes() → 0
  + onData(buffer) → Continue
};
```

### Implementation Details

**Building Socket Options**:
```cpp
Network::Socket::OptionsSharedPtr buildOriginalSrcOptions(
    const Network::Address::InstanceConstSharedPtr& address,
    uint32_t mark) {

  auto options = std::make_shared<Network::Socket::Options>();

  // Store source address
  options->push_back(std::make_shared<OriginalSrcSocketOption>(address));

  // Optional socket mark for routing
  if (mark != 0) {
    options->push_back(std::make_shared<SocketMarkOption>(mark));
  }

  return options;
}
```

**Upstream Application**:
```cpp
// When creating upstream connection
void applyOptions(Network::ConnectionSocket& socket,
                  const Network::Socket::OptionsSharedPtr& options) {
  for (const auto& option : *options) {
    if (auto* src_option = dynamic_cast<OriginalSrcSocketOption*>(option.get())) {
      // Bind to source IP
      socket.setLocalAddress(src_option->address());
    }
  }
}
```

### Configuration Example

```yaml
listener_filters:
  - name: envoy.filters.listener.original_src
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.original_src.v3.OriginalSrc
      mark: 123  # Optional SO_MARK value
```

### Use Cases
1. **Source IP transparency**: Backend sees real client IP
2. **Connection pooling**: Partition pools by client IP
3. **Policy routing**: Use SO_MARK for custom routing rules
4. **Logging**: Associate upstream connections with client IPs

---

## 6. Local Rate Limit

### Overview
Per-listener connection rate limiting using a token bucket algorithm. Rejects excess connections at the socket level before they reach the filter chain.

### Purpose
- **DDoS protection**: Limit connection flood attacks
- **Resource protection**: Prevent listener overload
- **Fair usage**: Enforce per-listener connection limits
- **Fast rejection**: Close sockets immediately, no processing overhead

### Key Features
- Token bucket rate limiting
- Runtime-configurable enable/disable
- Per-listener, not per-client
- No data peeking (maxReadBytes = 0)
- Immediate socket close on limit

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  LocalRateLimit::Filter                                             │
│                                                                      │
│  onAccept(cb)                                                        │
│    → If !config_->enabled():                                        │
│        Return Continue (disabled via runtime)                       │
│    → If !config_->canCreateConnection():                            │
│        ├─ stats_.rate_limited_.inc()                                 │
│        ├─ socket.ioHandle().close()                                 │
│        └─ Return StopIteration                                       │
│    → Return Continue (allowed)                                       │
└─────────────────────────────────────────────────────────────────────┘
```

### Token Bucket Algorithm

```
┌─────────────────────────────────────────────────────────────┐
│  Token Bucket State                                         │
│                                                              │
│  • max_tokens: Maximum bucket capacity                      │
│  • tokens_per_fill: Tokens added per interval               │
│  • fill_interval: Time between token additions              │
│  • current_tokens: Available tokens (≤ max_tokens)          │
│                                                              │
│  Operation:                                                  │
│  1. New connection request                                  │
│  2. If current_tokens > 0:                                  │
│       - Decrement current_tokens                            │
│       - Allow connection                                    │
│  3. Else:                                                   │
│       - Reject connection                                   │
│  4. Every fill_interval:                                    │
│       - current_tokens = min(current_tokens + tokens_per_fill, max_tokens)│
└─────────────────────────────────────────────────────────────┘
```

### Class Structure

```cpp
class FilterConfig {
  - Runtime::FeatureFlag enabled_                  // Runtime on/off switch
  - LocalRateLimitStats stats_
  - LocalRateLimiterImpl rate_limiter_             // Token bucket

  + canCreateConnection() → bool
  + enabled() → bool
};

class Filter : public Network::ListenerFilter {
  - FilterConfigSharedPtr config_

  + onAccept(cb) → Continue | StopIteration
  + maxReadBytes() → 0
  + onData(buffer) → Continue
};
```

### Token Bucket Parameters

**Example**: 100 connections/second with bursts of 200
- `max_tokens`: 200
- `tokens_per_fill`: 100
- `fill_interval`: 1s

**Burst handling**:
- Allows up to 200 connections instantly (full bucket)
- Refills at 100 tokens/second
- Sustained rate: 100 conn/sec
- Peak burst: 200 connections

### Configuration Example

```yaml
listener_filters:
  - name: envoy.filters.listener.local_ratelimit
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.local_ratelimit.v3.LocalRateLimit
      stat_prefix: listener_rate_limit
      token_bucket:
        max_tokens: 1000        # Burst capacity
        tokens_per_fill: 100    # Refill rate
        fill_interval: 1s       # Refill frequency
      runtime_enabled:
        default_value: true
        runtime_key: listener.rate_limit.enabled
```

### Stats
- `listener_local_ratelimit.<stat_prefix>.rate_limited`: Connections rejected

### Use Cases
1. **DDoS mitigation**: Limit connection flood attacks
2. **Resource management**: Prevent listener overload
3. **SLA enforcement**: Cap connections per listener
4. **Graceful degradation**: Shed load during high traffic

---

## 7. Dynamic Modules

### Overview
Extensible listener filter framework that loads custom filters from shared libraries (.so / .dylib / .dll). Enables custom logic without recompiling Envoy.

### Purpose
- **Runtime extensibility**: Load filters without rebuilding Envoy
- **Custom protocols**: Implement proprietary protocol inspection
- **Closed-source filters**: Deploy without exposing source
- **Hot reload**: Update filters without restarting (with limitations)

### Key Features
- ABI-stable interface for external modules
- Lifecycle management (new, destroy, scheduled events)
- Worker thread awareness
- Buffer access for peeking
- Callback integration for socket operations

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  DynamicModuleListenerFilter (Envoy-side)                           │
│                                                                      │
│  Constructor:                                                        │
│    → Store config (contains function pointers from .so)             │
│                                                                      │
│  onAccept(cb)                                                        │
│    ├─ Extract worker_index from cb.dispatcher().name()              │
│    ├─ initializeInModuleFilter()                                    │
│    │   → in_module_filter_ = config_->on_listener_filter_new_(      │
│    │                            config_->in_module_config_,          │
│    │                            this)                                │
│    └─ config_->on_listener_filter_on_accept_(this, in_module_filter)│
│       → Return Continue | StopIteration                             │
│                                                                      │
│  onData(buffer)                                                      │
│    ├─ current_buffer_ = &buffer                                     │
│    ├─ raw_slice = buffer.rawSlice()                                 │
│    ├─ config_->on_listener_filter_on_data_(this, in_module_filter,  │
│    │                                        raw_slice.len_)          │
│    └─ current_buffer_ = nullptr                                     │
│       → Return Continue | StopIteration                             │
│                                                                      │
│  onClose()                                                           │
│    → config_->on_listener_filter_on_close_(this, in_module_filter)  │
│                                                                      │
│  onScheduled(event_id)                                               │
│    → config_->on_listener_filter_scheduled_(this, in_module_filter, │
│                                              event_id)               │
│                                                                      │
│  Destructor:                                                         │
│    → destroy()                                                       │
│       → config_->on_listener_filter_destroy_(in_module_filter_)     │
└─────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────┐
│  External Module (.so / .dylib)                                     │
│                                                                      │
│  Exported ABI Functions:                                            │
│                                                                      │
│  envoy_dynamic_module_type_listener_filter_module_ptr               │
│  on_listener_filter_new(config, envoy_filter_ptr)                   │
│    → Allocate module-side filter state                              │
│    → Return opaque pointer to module filter                         │
│                                                                      │
│  envoy_dynamic_module_type_on_listener_filter_status                │
│  on_listener_filter_on_accept(envoy_filter_ptr, module_filter_ptr)  │
│    → Custom accept logic                                            │
│    → Return Continue | StopIteration                                │
│                                                                      │
│  envoy_dynamic_module_type_on_listener_filter_status                │
│  on_listener_filter_on_data(envoy_filter_ptr, module_filter_ptr,    │
│                              buffer_len)                             │
│    → Access buffer via ABI callbacks                                │
│    → Return Continue | StopIteration                                │
│                                                                      │
│  void on_listener_filter_destroy(module_filter_ptr)                 │
│    → Free module-side filter state                                  │
└─────────────────────────────────────────────────────────────────────┘
```

### ABI Interface

**Core Callbacks** (Module → Envoy):
```c
// Get raw buffer data
size_t envoy_dynamic_module_callback_listener_filter_buffer_get_bytes(
    void* envoy_filter_ptr,
    void* buffer_ptr,
    size_t buffer_length);

// Get original destination
bool envoy_dynamic_module_callback_listener_filter_get_original_dst(
    void* envoy_filter_ptr,
    void* address_ptr);

// Continue filter chain
void envoy_dynamic_module_callback_listener_filter_continue_iteration(
    void* envoy_filter_ptr);

// Schedule event on worker thread
void* envoy_dynamic_module_callback_listener_filter_scheduler_new(
    void* envoy_filter_ptr);
```

**Module Exports** (Envoy → Module):
```c
// Filter lifecycle
void* on_listener_filter_new(void* config_ptr, void* envoy_filter_ptr);
void on_listener_filter_destroy(void* module_filter_ptr);

// Filter callbacks
int on_listener_filter_on_accept(void* envoy_filter_ptr, void* module_filter_ptr);
int on_listener_filter_on_data(void* envoy_filter_ptr, void* module_filter_ptr, size_t len);
void on_listener_filter_on_close(void* envoy_filter_ptr, void* module_filter_ptr);
size_t on_listener_filter_get_max_read_bytes(void* envoy_filter_ptr, void* module_filter_ptr);
void on_listener_filter_scheduled(void* envoy_filter_ptr, void* module_filter_ptr, uint64_t event_id);
```

### Class Structure

```cpp
class DynamicModuleListenerFilterConfig {
  - DynamicModuleSharedPtr dynamic_module_         // .so handle
  - in_module_config_ptr in_module_config_         // Module config

  // Function pointers from module
  - on_listener_filter_new_fn on_listener_filter_new_
  - on_listener_filter_destroy_fn on_listener_filter_destroy_
  - on_listener_filter_on_accept_fn on_listener_filter_on_accept_
  - on_listener_filter_on_data_fn on_listener_filter_on_data_
  - on_listener_filter_on_close_fn on_listener_filter_on_close_
  - on_listener_filter_get_max_read_bytes_fn on_listener_filter_get_max_read_bytes_
  - on_listener_filter_scheduled_fn on_listener_filter_scheduled_
};

class DynamicModuleListenerFilter : public Network::ListenerFilter {
  - DynamicModuleListenerFilterConfigSharedPtr config_
  - in_module_filter_ptr in_module_filter_          // Opaque module state
  - ListenerFilterCallbacks* callbacks_
  - ListenerFilterBuffer* current_buffer_           // Valid during onData only
  - Address::InstanceConstSharedPtr cached_original_dst_
  - bool destroyed_
  - uint32_t worker_index_

  + onAccept(cb) → Continue | StopIteration
  + onData(buffer) → Continue | StopIteration
  + onClose()
  + maxReadBytes() → size_t
  + onScheduled(event_id)
  - initializeInModuleFilter()
  - destroy()
};

class DynamicModuleListenerFilterScheduler {
  - DynamicModuleListenerFilterWeakPtr filter_
  - Dispatcher& dispatcher_

  + commit(event_id)                                // Post to worker thread
};
```

### Example Module Implementation

**Custom Protocol Inspector** (C):
```c
#include "envoy_dynamic_module_abi.h"

typedef struct {
  int state;
} MyFilterState;

void* on_listener_filter_new(void* config_ptr, void* envoy_filter_ptr) {
  MyFilterState* state = malloc(sizeof(MyFilterState));
  state->state = 0;
  return state;
}

void on_listener_filter_destroy(void* module_filter_ptr) {
  free(module_filter_ptr);
}

int on_listener_filter_on_accept(void* envoy_filter_ptr, void* module_filter_ptr) {
  // Need to peek at data
  return envoy_dynamic_module_type_on_listener_filter_status_StopIteration;
}

int on_listener_filter_on_data(void* envoy_filter_ptr, void* module_filter_ptr, size_t len) {
  MyFilterState* state = (MyFilterState*)module_filter_ptr;

  char buffer[1024];
  size_t read = envoy_dynamic_module_callback_listener_filter_buffer_get_bytes(
      envoy_filter_ptr, buffer, sizeof(buffer));

  // Custom protocol detection logic
  if (buffer[0] == 0x42 && buffer[1] == 0x13) {
    // Detected custom protocol
    state->state = 1;
    return envoy_dynamic_module_type_on_listener_filter_status_Continue;
  }

  return envoy_dynamic_module_type_on_listener_filter_status_StopIteration;
}

size_t on_listener_filter_get_max_read_bytes(void* envoy_filter_ptr, void* module_filter_ptr) {
  return 1024;  // Peek 1KB
}
```

### Configuration Example

```yaml
listener_filters:
  - name: envoy.filters.listener.dynamic_modules
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.listener.dynamic_modules.v3.DynamicModules
      dynamic_modules:
        - name: my_protocol_inspector
          module_path: /usr/local/lib/envoy/my_protocol_inspector.so
          do_not_close: false
```

### Worker Thread Handling

```cpp
// Extract worker index from dispatcher name
// Format: "worker_0", "worker_1", etc.
const std::string& worker_name = cb.dispatcher().name();
auto pos = worker_name.find_first_of('_');
absl::SimpleAtoi(worker_name.substr(pos + 1), &worker_index_);

// Module can access worker index
uint32_t workerIndex() const { return worker_index_; }
```

### Cross-Thread Scheduling

```cpp
// Module creates scheduler
void* scheduler = envoy_dynamic_module_callback_listener_filter_scheduler_new(
    envoy_filter_ptr);

// From another thread, commit event
envoy_dynamic_module_callback_listener_filter_scheduler_commit(
    scheduler, event_id);

// Envoy posts to worker thread dispatcher
dispatcher_.post([filter, event_id]() {
  if (auto filter_shared = filter.lock()) {
    filter_shared->onScheduled(event_id);
  }
});
```

### Use Cases
1. **Proprietary protocols**: Inspect custom binary protocols
2. **Closed-source filters**: Deploy without sharing code
3. **Third-party integrations**: Load vendor-provided modules
4. **Rapid prototyping**: Test filter logic without rebuilding Envoy

---

## 8. Comparison Matrix

| Feature | TLS Inspector | Proxy Protocol | HTTP Inspector | Original Dst | Original Src | Local Rate Limit | Dynamic Modules |
|---------|---------------|----------------|----------------|--------------|--------------|------------------|-----------------|
| **Peek Data** | ✅ Yes (ClientHello) | ✅ Yes (PROXY header) | ✅ Yes (HTTP request) | ❌ No | ❌ No | ❌ No | ⚙️ Configurable |
| **maxReadBytes** | Dynamic (up to ClientHello size) | V1: 108, V2: 216 | Dynamic (up to 64KB) | 0 | 0 | 0 | Module-defined |
| **onAccept** | StopIteration | StopIteration | Continue if TLS, else StopIteration | Continue | Continue | Continue or StopIteration | Module-defined |
| **Platform-Specific** | ❌ No | ❌ No | ❌ No | ✅ Yes (SO_ORIGINAL_DST) | ⚠️ SO_MARK (Linux) | ❌ No | ⚙️ Module-dependent |
| **Modifies Socket** | SNI, ALPN, transport | Remote/local address | ALPN | Local address | Socket options | Closes on limit | Module-defined |
| **Filter Chain Matching** | ✅ SNI, ALPN | ✅ TLV metadata | ✅ ALPN (plaintext) | ✅ Address | ❌ No | ❌ No | ⚙️ Module-defined |
| **Typical Order** | 2 (after PROXY) | 1 (first) | 3 (after TLS) | Any | Any | First or last | Flexible |
| **TLS Support** | ✅ TLS-aware | ⚠️ Before TLS | ⚠️ Plaintext only | ✅ TLS-transparent | ✅ TLS-transparent | ✅ TLS-transparent | ⚙️ Module-defined |
| **Performance Impact** | Medium (SSL parsing) | Low (header read) | Low (parser) | Very Low (syscall) | Very Low (syscall) | Very Low (token check) | ⚙️ Module-dependent |
| **Stats** | 8 counters + 1 histogram | 9 counters | 5 counters | None | None | 1 counter | Module-defined |
| **Config Complexity** | Medium | High (TLVs) | Low | Low | Low | Low | High (ABI) |
| **Primary Use Case** | SNI routing | Preserve IPs through LB | Plaintext protocol detection | Transparent proxy | Source IP tracking | Connection rate limiting | Custom extensions |

### Execution Order Best Practices

**Recommended order**:
1. `proxy_protocol` - Get real addresses first
2. `tls_inspector` - Extract TLS metadata
3. `http_inspector` - Detect plaintext HTTP (if not TLS)
4. `original_dst` - Restore destination (if transparent proxy)
5. `original_src` - Tag source (if needed for upstream)
6. `local_ratelimit` - Optionally rate limit after inspection
7. `dynamic_modules` - Custom logic last

**Rationale**:
- Address correction (proxy_protocol, original_dst) before inspection
- Protocol detection (tls_inspector, http_inspector) for filter chain matching
- Rate limiting after inspection to avoid wasting resources on rejected connections
- Custom modules last to leverage all extracted metadata

---

## 9. Best Practices

### Security Considerations

1. **Trust Boundaries**
   - **Proxy Protocol**: Only enable `allow_requests_without_proxy_protocol` for trusted sources
   - **Original Dst**: Validate that connections are actually redirected (use with `use_original_dst`)
   - **Dynamic Modules**: Audit .so files, use sandboxing if available

2. **Resource Limits**
   - **TLS Inspector**: Set `max_client_hello_size` to prevent memory exhaustion
   - **HTTP Inspector**: Limit `MAX_INSPECT_SIZE` to avoid large buffer allocation
   - **Local Rate Limit**: Configure reasonable token bucket sizes

3. **Error Handling**
   - Handle parse errors gracefully (close connection vs allow)
   - Log suspicious patterns (malformed PROXY headers, oversized ClientHellos)
   - Monitor stats for anomalies (spike in `tls_not_found`, `proxy_protocol.error`)

### Performance Optimization

1. **Minimize Peeking**
   - Only enable inspection filters when necessary
   - Use filters with `maxReadBytes() == 0` when possible (original_dst, original_src)
   - Combine filters instead of running multiple inspections

2. **Order Optimization**
   - Put cheap filters first (original_dst, rate_limit)
   - Expensive inspection after initial rejection (tls_inspector after rate_limit)
   - Rate limit early to avoid wasting CPU on rejected connections

3. **Buffer Management**
   - TLS Inspector dynamically adjusts buffer size (starts small)
   - HTTP Inspector increases buffer only if needed
   - Avoid over-provisioning `maxReadBytes()`

### Configuration Patterns

**Edge Proxy** (public-facing):
```yaml
listener_filters:
  - name: envoy.filters.listener.local_ratelimit  # DDoS protection first
  - name: envoy.filters.listener.proxy_protocol   # Get real client IP
  - name: envoy.filters.listener.tls_inspector    # SNI routing
```

**Sidecar Proxy** (service mesh):
```yaml
listener_filters:
  - name: envoy.filters.listener.original_dst     # Transparent proxy
  - name: envoy.filters.listener.original_src     # Track client IP
  - name: envoy.filters.listener.tls_inspector    # mTLS inspection
```

**API Gateway** (mixed protocols):
```yaml
listener_filters:
  - name: envoy.filters.listener.tls_inspector    # TLS SNI/ALPN
  - name: envoy.filters.listener.http_inspector   # Fallback for plaintext
```

**Internal Load Balancer**:
```yaml
listener_filters:
  - name: envoy.filters.listener.proxy_protocol   # Preserve IPs
  - name: envoy.filters.listener.original_dst     # Dynamic routing
```

### Debugging

1. **Enable Debug Logging**
   ```yaml
   admin:
     access_log_path: /tmp/admin_access.log
     address:
       socket_address: { address: 0.0.0.0, port_value: 9901 }
   ```

2. **Check Stats**
   ```bash
   curl http://localhost:9901/stats | grep listener
   ```

3. **Common Issues**
   - **TLS Inspector**: `clienthello_too_large` → increase `max_client_hello_size`
   - **Proxy Protocol**: `v1.error` / `v2.error` → check header format
   - **HTTP Inspector**: `http_not_found` → verify it's actually HTTP
   - **Rate Limit**: `rate_limited` spike → adjust token bucket

### Testing

1. **Unit Tests**
   - Mock `ListenerFilterCallbacks`
   - Test parse logic with crafted inputs
   - Verify error handling (malformed data)

2. **Integration Tests**
   - Test filter chain ordering
   - Verify metadata propagation to network filters
   - Test with real clients (openssl, curl, proxy protocol senders)

3. **Load Tests**
   - Measure latency impact of inspection
   - Verify rate limiting under load
   - Test buffer management with large requests

### Monitoring

**Key Metrics**:
- `listener.downstream_cx_total`: Total accepted connections
- `listener.<name>.tls_inspector.tls_found`: TLS detection rate
- `listener.<name>.proxy_protocol.v2.found`: Proxy protocol usage
- `listener.<name>.local_ratelimit.<prefix>.rate_limited`: Rate limit rejections

**Alerts**:
- Spike in parse errors (potential attack)
- High rate limit rejections (capacity issue)
- Drop in expected protocol detection (configuration drift)

---

## Appendix: Protocol References

### TLS ClientHello Structure
```
┌────────────────────────────────────────────────────┐
│ TLS Record Header (5 bytes)                       │
│   Content Type (0x16 = Handshake)                 │
│   Version (0x03 0x01 = TLS 1.0 legacy)            │
│   Length (2 bytes)                                 │
├────────────────────────────────────────────────────┤
│ Handshake Protocol (variable)                     │
│   Handshake Type (0x01 = ClientHello)             │
│   Length (3 bytes)                                 │
│   Version                                          │
│   Random (32 bytes)                                │
│   Session ID Length + Session ID                   │
│   Cipher Suites Length + Cipher Suites             │
│   Compression Methods Length + Methods             │
│   Extensions Length + Extensions                   │
│     ├─ SNI (0x0000)                                │
│     ├─ ALPN (0x0010)                               │
│     ├─ Supported Groups (0x000a)                   │
│     └─ ...                                         │
└────────────────────────────────────────────────────┘
```

### PROXY Protocol v2 TLV Types
| Type | Name | Description |
|------|------|-------------|
| 0x01 | PP2_TYPE_ALPN | Upper layer protocol |
| 0x02 | PP2_TYPE_AUTHORITY | Host from HTTP/HTTPS |
| 0x03 | PP2_TYPE_CRC32C | Checksum |
| 0x04 | PP2_TYPE_NOOP | Padding |
| 0x05 | PP2_TYPE_UNIQUE_ID | Unique ID |
| 0x20 | PP2_TYPE_SSL | SSL/TLS information |
| 0x21 | PP2_SUBTYPE_SSL_VERSION | SSL version |
| 0x22 | PP2_SUBTYPE_SSL_CN | Client cert CN |
| 0x23 | PP2_SUBTYPE_SSL_CIPHER | Cipher name |
| 0x24 | PP2_SUBTYPE_SSL_SIG_ALG | Signature algorithm |
| 0x25 | PP2_SUBTYPE_SSL_KEY_ALG | Key algorithm |

### HTTP/1.1 Request Line Format
```
GET /path/to/resource HTTP/1.1\r\n
^^^  ^^^^^^^^^^^^^^^^^ ^^^^^^^^
│    │                 └─ Protocol version
│    └─ Request URI
└─ Method

Common methods: GET, POST, PUT, DELETE, HEAD, OPTIONS, PATCH
```

---

## Conclusion

Envoy's listener filters provide powerful early inspection and manipulation capabilities for incoming connections. Understanding their execution model, data flow, and configuration options is essential for building robust, performant proxies.

Key takeaways:
- **Listener filters run before filter chain matching** - use them to extract routing metadata
- **Peeking is non-destructive** - data remains in buffer for network filters
- **Order matters** - address correction before inspection, rate limiting strategically
- **Performance vs features** - balance inspection depth with latency requirements
- **Security first** - validate sources, limit buffers, monitor anomalies

For implementation details, refer to source files in:
- `source/extensions/filters/listener/<filter_name>/`

For API documentation:
- `api/envoy/extensions/filters/listener/<filter_name>/v3/*.proto`

For further documentation:
- [01-Overview-and-Architecture.md](docs/01-Overview-and-Architecture.md)
- [02-Listener-Filter-Chain-Execution.md](docs/02-Listener-Filter-Chain-Execution.md)
- [04-Configuration-and-Extension.md](docs/04-Configuration-and-Extension.md)
