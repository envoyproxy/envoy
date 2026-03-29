# Part 6: Filter Chain Matching and Selection

## Table of Contents
1. [Introduction](#introduction)
2. [Filter Chain Match Criteria](#filter-chain-match-criteria)
3. [Matching Algorithm](#matching-algorithm)
4. [Listener Filters and Matching](#listener-filters-and-matching)
5. [Matching Data Structures](#matching-data-structures)
6. [Example Matching Scenarios](#example-matching-scenarios)
7. [Performance Optimizations](#performance-optimizations)

## Introduction

Filter chain matching is the process of selecting the correct filter chain for an incoming connection based on various connection attributes. This allows a single listener to handle different types of traffic with different processing pipelines.

## Filter Chain Match Criteria

### Available Match Criteria

Envoy supports matching on multiple connection attributes in a specific precedence order:

```cpp
// envoy/config/listener/v3/listener_components.proto

message FilterChainMatch {
  // 1. Destination port (exact match)
  google.protobuf.UInt32Value destination_port = 1;

  // 2. Destination IP addresses (CIDR ranges)
  repeated core.v3.CidrRange prefix_ranges = 2;

  // 3. Server names (SNI from TLS, exact or wildcard)
  repeated string server_names = 3;

  // 4. Transport protocol ("tls", "raw_buffer", etc.)
  string transport_protocol = 4;

  // 5. Application protocols (ALPN from TLS)
  repeated string application_protocols = 5;

  // 6. Direct source IP addresses (CIDR ranges)
  repeated core.v3.CidrRange direct_source_prefix_ranges = 6;

  // 7. Source type (LOCAL, EXTERNAL, ANY)
  core.v3.SocketAddress.SocketType source_type = 7;

  // 8. Source port ranges
  repeated type.v3.Int64Range source_ports = 8;
}
```

### Match Precedence

The matching algorithm follows a strict precedence order:

```
┌──────────────────────────────────────────────────────────────┐
│          FILTER CHAIN MATCH PRECEDENCE ORDER                 │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Highest Priority (Most Specific)                            │
│  ┌────────────────────────────────────────────────────┐    │
│  │  1. Destination Port                               │    │
│  │     - Exact port (e.g., 8443) > Any port (0)       │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │  2. Destination IP Address                         │    │
│  │     - Most specific CIDR > Less specific CIDR      │    │
│  │     - 192.168.1.10/32 > 192.168.1.0/24 > 0.0.0.0/0 │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │  3. Server Name (SNI)                              │    │
│  │     - Exact match > Wildcard match > No SNI        │    │
│  │     - api.example.com > *.example.com > (none)     │    │
│  │     - Longest suffix wins for wildcards            │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │  4. Transport Protocol                             │    │
│  │     - "tls" > "raw_buffer" > (empty/any)           │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │  5. Application Protocols (ALPN)                   │    │
│  │     - Match any of: ["h2", "http/1.1"] > (empty)   │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │  6. Source IP Address                              │    │
│  │     - Most specific CIDR > (empty/any)             │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │  7. Source Type                                    │    │
│  │     - LOCAL > EXTERNAL > ANY                       │    │
│  └────────────────────────────────────────────────────┘    │
│         │                                                    │
│         ▼                                                    │
│  ┌────────────────────────────────────────────────────┐    │
│  │  8. Source Port                                    │    │
│  │     - Specific range > (empty/any)                 │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
│  Lowest Priority (Least Specific)                           │
│  ┌────────────────────────────────────────────────────┐    │
│  │  Default Filter Chain                              │    │
│  │     - Matches if no other chain matches            │    │
│  └────────────────────────────────────────────────────┘    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## Matching Algorithm

### FilterChainManagerImpl::findFilterChain

```cpp
// source/common/listener_manager/filter_chain_manager_impl.cc

const FilterChain* FilterChainManagerImpl::findFilterChain( const ConnectionSocket& socket, const StreamInfo::StreamInfo& stream_info) const {

  // Extract connection attributes
  const auto& local_address = socket.connectionInfoProvider().localAddress();
  const auto& remote_address = socket.connectionInfoProvider().remoteAddress();
  const std::string& server_name = socket.requestedServerName();
  const std::string& transport_protocol = socket.detectedTransportProtocol();
  const auto& app_protocols = socket.requestedApplicationProtocols();

  // Step 1: Match destination IP
  const auto* dest_ip_match = findDestinationIP(local_address->ip());
  if (dest_ip_match == nullptr) {
    return default_filter_chain_.get();
  }

  // Step 2: Match destination port
  const auto* dest_port_match = findDestinationPort( *dest_ip_match, local_address->ip()->port());
  if (dest_port_match == nullptr) {
    return default_filter_chain_.get();
  }

  // Step 3: Match server name (SNI)
  const auto* server_name_match = findServerName( *dest_port_match, server_name);
  if (server_name_match == nullptr) {
    return default_filter_chain_.get();
  }

  // Step 4: Match transport protocol
  const auto* transport_match = findTransportProtocol( *server_name_match, transport_protocol);
  if (transport_match == nullptr) {
    return default_filter_chain_.get();
  }

  // Step 5: Match application protocols
  const auto* app_protocol_match = findApplicationProtocols( *transport_match, app_protocols);
  if (app_protocol_match == nullptr) {
    return default_filter_chain_.get();
  }

  // Step 6-8: Match source criteria (if configured)
  const FilterChain* source_match = findSourceMatch( *app_protocol_match, remote_address, stream_info);

  return source_match ? source_match : app_protocol_match;
}
```

### Destination IP Matching

```cpp
const FilterChainsByDestinationPort*
FilterChainManagerImpl::findDestinationIP(const Ip* ip) const { if (ip == nullptr) {
    // Non-IP connection (e.g., Unix domain socket)
    return destination_ips_.find(nullptr);
  }

  // Try exact IP match first
  auto iter = destination_ips_.find(ip);
  if (iter != destination_ips_.end()) {
    return iter->second.get();
  }

  // Try CIDR matches (from most specific to least specific)
  for (const auto& cidr_entry : destination_ip_tries_) { if (cidr_entry.first.isInRange(*ip)) {
    return cidr_entry.second.get();
    }
  }

  // Try wildcard (0.0.0.0/0 for IPv4, ::/0 for IPv6)
  auto wildcard_iter = destination_ips_.find(wildcard_address);
  if (wildcard_iter != destination_ips_.end()) {
    return wildcard_iter->second.get();
  }

  return nullptr;
}
```

### Server Name Matching

```cpp
const FilterChainsByTransportProtocol*
FilterChainManagerImpl::findServerName( const FilterChainsByServerNames& server_names, const std::string& server_name) const {

  if (server_name.empty()) {
    // No SNI provided
    return server_names.no_server_name_.get();
  }

  // Try exact match first
  auto exact_iter = server_names.exact_server_names_.find(server_name);
  if (exact_iter != server_names.exact_server_names_.end()) {
    return exact_iter->second.get();
  }

  // Try wildcard matches (longest suffix wins)
  const FilterChainsByTransportProtocol* best_match = nullptr;
  size_t longest_suffix_len = 0;

  for (const auto& wildcard_entry : server_names.wildcard_server_names_) { const std::string& pattern = wildcard_entry.first;

    // Pattern format: "*.example.com" or "*.*.example.com"
    if (matchesWildcard(server_name, pattern)) { size_t suffix_len = pattern.length();
      if (suffix_len > longest_suffix_len) { longest_suffix_len = suffix_len;
        best_match = wildcard_entry.second.get();
      }
    }
  }

  return best_match ? best_match : server_names.no_server_name_.get();
}

// Wildcard matching helper
bool matchesWildcard( const std::string& server_name, const std::string& pattern) {

  // Pattern: "*.example.com"
  // Matches: "api.example.com", "www.example.com"
  // Does not match: "example.com", "api.beta.example.com"

  if (!pattern.starts_with("*.")) {
    return false;
  }

  const std::string suffix = pattern.substr(1);  // Remove leading '*'

  // Check if server_name ends with suffix
  if (!server_name.ends_with(suffix)) {
    return false;
  }

  // Check that there's exactly one label before the suffix
  size_t prefix_len = server_name.length() - suffix.length();
  std::string_view prefix(server_name.data(), prefix_len);

  // Prefix should not contain '.' (single label)
  return prefix.find('.') == std::string_view::npos;
}
```

### Application Protocol Matching

```cpp
const FilterChain*
FilterChainManagerImpl::findApplicationProtocols( const FilterChainsByApplicationProtocols& app_protocols, const std::vector<std::string>& requested_protocols) const {

  if (requested_protocols.empty()) {
    // No ALPN requested
    return app_protocols.default_filter_chain_.get();
  }

  // Try to match any requested protocol
  for (const auto& requested : requested_protocols) { auto iter = app_protocols.protocols_.find(requested);
    if (iter != app_protocols.protocols_.end()) {
      return iter->second.get();
    }
  }

  // No match, use default
  return app_protocols.default_filter_chain_.get();
}
```

## Listener Filters and Matching

### TLS Inspector

The TLS Inspector listener filter extracts SNI and ALPN from TLS ClientHello before filter chain matching.

```cpp
// contrib/tls_inspector/filters/listener/source/tls_inspector.cc

Network::FilterStatus TlsInspector::onData(Buffer::Instance& data) {
  // Parse TLS ClientHello
  ParseResult result = parseTlsClientHello(data);

  if (result.state == ParseState::Complete) {
    // Extract SNI
    if (!result.server_name.empty()) { cb_->socket().setRequestedServerName(result.server_name);
    }

    // Extract ALPN
    if (!result.alpn_protocols.empty()) { cb_->socket().setRequestedApplicationProtocols( result.alpn_protocols);
    }

    // Set transport protocol
    cb_->socket().setDetectedTransportProtocol("tls");

    // Continue to filter chain matching
    return Network::FilterStatus::Continue;
  }

  // Need more data
  return Network::FilterStatus::StopIteration;
}
```

### Listener Filter Flow

```
┌──────────────────────────────────────────────────────────────┐
│        LISTENER FILTER AND MATCHING FLOW                     │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Step 1: Connection Accept                                   │
│    └─► Listener::onAccept(socket)                           │
│                                                              │
│  Step 2: Run Listener Filters                                │
│    ┌────────────────────────────────────────────────────┐  │
│    │  ListenerFilterManager::accept()                   │  │
│    │                                                     │  │
│    │  ┌──────────────────────────────────────────┐     │  │
│    │  │  TLS Inspector                           │     │  │
│    │  │  ├─ Read TLS ClientHello from socket    │     │  │
│    │  │  ├─ Extract SNI: "api.example.com"       │     │  │
│    │  │  ├─ Extract ALPN: ["h2", "http/1.1"]     │     │  │
│    │  │  ├─ Set detectedTransportProtocol: "tls" │     │  │
│    │  │  └─ Return Continue                       │     │  │
│    │  └──────────────────────────────────────────┘     │  │
│    │                                                     │  │
│    │  ┌──────────────────────────────────────────┐     │  │
│    │  │  Proxy Protocol Filter                   │     │  │
│    │  │  ├─ Parse PROXY protocol header          │     │  │
│    │  │  ├─ Extract real source IP               │     │  │
│    │  │  └─ Return Continue                       │     │  │
│    │  └──────────────────────────────────────────┘     │  │
│    │                                                     │  │
│    └─────────────────────────────────────────────────────┘  │
│                                                              │
│  Step 3: Filter Chain Matching                              │
│    └─► FilterChainManager::findFilterChain()                │
│         ├─ Use socket.requestedServerName() → "api..."     │
│         ├─ Use socket.requestedApplicationProtocols()      │
│         ├─ Use socket.detectedTransportProtocol() → "tls"  │
│         └─ Return matched FilterChain                      │
│                                                              │
│  Step 4: Create Network Filter Chain                        │
│    └─► Use selected FilterChain's filters                  │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

## Matching Data Structures

### Filter Chain Matching Tree

```cpp
// source/common/listener_manager/filter_chain_manager_impl.h

class FilterChainManagerImpl {
private:
  // Root: Destination IP → Port → Server Name → Transport → App Protocol

  struct FilterChainsByApplicationProtocols {
    // Map: "h2" → FilterChain
    //      "http/1.1" → FilterChain
    absl::flat_hash_map<std::string, FilterChainPtr> protocols_;
    FilterChainPtr default_filter_chain_;
  };

  struct FilterChainsByTransportProtocol {
    // Map: "tls" → FilterChainsByApplicationProtocols
    //      "raw_buffer" → FilterChainsByApplicationProtocols
    absl::flat_hash_map<
        std::string, FilterChainsByApplicationProtocols> protocols_;
    FilterChainsByApplicationProtocols default_protocol_;
  };

  struct FilterChainsByServerNames {
    // Exact matches: "api.example.com" → FilterChainsByTransportProtocol
    absl::flat_hash_map<
        std::string, FilterChainsByTransportProtocol> exact_server_names_;

    // Wildcard matches: "*.example.com" → FilterChainsByTransportProtocol
    std::vector<std::pair<
        std::string, FilterChainsByTransportProtocol>> wildcard_server_names_;

    // No SNI case
    std::unique_ptr<FilterChainsByTransportProtocol> no_server_name_;
  };

  struct FilterChainsByDestinationPort {
    // Exact port matches: 8443 → FilterChainsByServerNames
    absl::flat_hash_map<
        uint16_t, FilterChainsByServerNames> destination_ports_;

    // Wildcard (any port)
    std::unique_ptr<FilterChainsByServerNames> any_port_;
  };

  // Root: Destination IP → FilterChainsByDestinationPort
  // Includes CIDR trie for IP range matching
  absl::flat_hash_map<
      Network::Address::InstanceConstSharedPtr, FilterChainsByDestinationPort, Network::Address::Ipv6CidrHasher> destination_ips_;

  std::vector<std::pair<
      Network::Address::CidrRange, FilterChainsByDestinationPort>> destination_ip_tries_;

  // Default filter chain (fallback)
  FilterChainPtr default_filter_chain_;
};
```

### Matching Tree Example

```
Listener on 0.0.0.0:443 with multiple filter chains:

destination_ips_
├─ 0.0.0.0/0 (any IP)
    └─ destination_ports_
        ├─ 443 (exact port)
        │   └─ server_names_
        │       ├─ "api.example.com" (exact)
        │       │   └─ transport_protocol_
        │       │       ├─ "tls"
        │       │       │   └─ application_protocols_
        │       │       │       ├─ "h2" → FilterChain #1
        │       │       │       │   (API HTTP/2 with TLS)
        │       │       │       └─ default → FilterChain #2
        │       │       │           (API HTTP/1.1 with TLS)
        │       │       └─ default → FilterChain #3
        │       │           (API without TLS)
        │       │
        │       ├─ "*.example.com" (wildcard)
        │       │   └─ ... → FilterChain #4
        │       │       (Wildcard domains)
        │       │
        │       └─ no_server_name (no SNI)
        │           └─ ... → FilterChain #5
        │               (Connections without SNI)
        │
        └─ 0 (any port)
            └─ ... → FilterChain #6
                (Catch-all for other ports)
```

## Example Matching Scenarios

### Scenario 1: HTTPS API Request

```yaml
# Configuration
filter_chains:
- filter_chain_match:
    destination_port: 443
    server_names: ["api.example.com"]
    transport_protocol: "tls"
    application_protocols: ["h2"]
  name: "api_h2_chain"
  # ... filters ...
```

```
Connection:
├─ Destination: 0.0.0.0:443
├─ SNI: "api.example.com"
├─ ALPN: ["h2", "http/1.1"]
└─ Transport: "tls"

Matching:
├─ Destination port: 443 ✓
├─ Server name: "api.example.com" ✓ (exact match)
├─ Transport: "tls" ✓
├─ Application protocol: "h2" ✓ (first in ALPN list matches)
└─ Result: "api_h2_chain" selected
```

### Scenario 2: Wildcard SNI Match

```yaml
filter_chains:
- filter_chain_match:
    server_names: ["*.example.com"]
  name: "wildcard_chain"
```

```
Connection:
├─ SNI: "app.example.com"
└─ No ALPN

Matching:
├─ Server name: "app.example.com"
├─ Try exact match: not found
├─ Try wildcard: "*.example.com" ✓
│   └─ "app" is single label, matches "*.example.com"
└─ Result: "wildcard_chain" selected

Connection 2:
├─ SNI: "api.beta.example.com"
└─ No ALPN

Matching:
├─ Server name: "api.beta.example.com"
├─ Try wildcard: "*.example.com" ✗
│   └─ "api.beta" has '.', doesn't match "*.example.com"
└─ Result: Default filter chain (or no match)
```

### Scenario 3: Source IP Matching

```yaml
filter_chains:
- filter_chain_match:
    direct_source_prefix_ranges:
    - address_prefix: "10.0.0.0"
      prefix_len: 8
  name: "internal_chain"

- filter_chain_match: {}  # Default
  name: "external_chain"
```

```
Connection from 10.1.2.3:
├─ Source IP: 10.1.2.3
└─ Destination: 0.0.0.0:443

Matching:
├─ All previous criteria match default
├─ Source IP: 10.1.2.3
│   └─ Matches 10.0.0.0/8 ✓
└─ Result: "internal_chain" selected

Connection from 203.0.113.5:
├─ Source IP: 203.0.113.5 (external)
└─ Destination: 0.0.0.0:443

Matching:
├─ Source IP: 203.0.113.5
│   └─ Does not match 10.0.0.0/8 ✗
└─ Result: "external_chain" selected (default)
```

## Performance Optimizations

### CIDR Trie

For efficient IP range matching, Envoy uses a trie data structure:

```cpp
// Sorted by prefix length (most specific first)
std::vector<std::pair<CidrRange, FilterChainsByDestinationPort>> destination_ip_tries_;

// Insertion: sorted by specificity (longer prefix first)
void addDestinationIpCidr( const Network::Address::CidrRange& cidr, FilterChainsByDestinationPort chains) {

  // Insert in sorted order (most specific first)
  auto insert_pos = std::lower_bound( destination_ip_tries_.begin(), destination_ip_tries_.end(), cidr, [](const auto& a, const auto& b) {
    return a.first.length() > b.first.length();
      });

  destination_ip_tries_.insert(insert_pos, {cidr, std::move(chains)});
}

// Lookup: iterate most specific to least specific
const FilterChainsByDestinationPort*
findDestinationIpCidr(const Network::Address::Ip& ip) const { for (const auto& entry : destination_ip_tries_) { if (entry.first.isInRange(ip)) {
  return &entry.second;
    }
  }
  return nullptr;
}
```

### Hash Map Lookups

For exact matches (port, exact server name, protocols), hash maps provide O(1) lookups:

```cpp
// O(1) destination port lookup
absl::flat_hash_map<uint16_t, FilterChainsByServerNames> destination_ports_;

// O(1) exact server name lookup
absl::flat_hash_map<std::string, FilterChainsByTransportProtocol> exact_server_names_;

// O(1) application protocol lookup
absl::flat_hash_map<std::string, FilterChainPtr> protocols_;
```

### Wildcard Caching

For wildcard server names, results can be cached:

```cpp
// Cache wildcard matches (LRU cache)
mutable absl::flat_hash_map<std::string, const FilterChain*> wildcard_match_cache_;

const FilterChain* findServerNameWithCache( const std::string& server_name) const {

  // Check cache first
  auto cache_iter = wildcard_match_cache_.find(server_name);
  if (cache_iter != wildcard_match_cache_.end()) {
    return cache_iter->second;
  }

  // Perform wildcard matching
  const FilterChain* match = findServerNameInternal(server_name);

  // Cache result (with LRU eviction if needed)
  wildcard_match_cache_[server_name] = match;

  return match;
}
```

## Summary

Key takeaways about filter chain matching:

1. **Hierarchical Matching**: Matches follow strict precedence order
2. **Lazy Evaluation**: Stops at first match for efficiency
3. **Listener Filters**: Extract matching data (SNI, ALPN) before filter chain selection
4. **Wildcard Support**: Server names support wildcard patterns with longest suffix matching
5. **Source Matching**: Can match on source IP/port for internal vs external traffic
6. **Performance**: Uses tries for CIDR, hash maps for exact matches, caching for wildcards
7. **Default Chain**: Fallback if no filter chains match

## Next Steps

Proceed to **Part 7: Filter Configuration and Registration** to understand how filters are configured and registered in the system.

---

**Document Version**: 1.0
**Last Updated**: 2026-02-28
**Related Code Paths**:
- `source/common/listener_manager/filter_chain_manager_impl.cc` - Matching algorithm
- `api/envoy/config/listener/v3/listener_components.proto` - Match criteria
- `contrib/tls_inspector/filters/listener/source/tls_inspector.cc` - SNI extraction
