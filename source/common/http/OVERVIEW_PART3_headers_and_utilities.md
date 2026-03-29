# Envoy HTTP Layer — Overview Part 3: Header System & Utilities

**Directory:** `source/common/http/`  
**Part:** 3 of 4 — Header System, Utilities, Path Normalization, Status Codes, Hash Policy

---

## Table of Contents

1. [Header System Overview](#1-header-system-overview)
2. [HeaderMapImpl — Performance Design](#2-headermapimpl--performance-design)
3. [Inline Header Access via Macros](#3-inline-header-access-via-macros)
4. [HeaderUtility — Matchers & Manipulation](#4-headerutility--matchers--manipulation)
5. [HeaderMutation — Programmatic Changes](#5-headermutation--programmatic-changes)
6. [Http::Utility — URL, Encoding, gRPC](#6-httputility--url-encoding-grpc)
7. [PathUtility — Path Canonicalization](#7-pathutility--path-canonicalization)
8. [HashPolicy — Load Balancing](#8-hashpolicy--load-balancing)
9. [Status Codes and Error Propagation](#9-status-codes-and-error-propagation)
10. [ConnectionManagerUtility — Header Mutation at Runtime](#10-connectionmanagerutility--header-mutation-at-runtime)

---

## 1. Header System Overview

```mermaid
flowchart TD
    subgraph "Header System Components"
        HMI["HeaderMapImpl\n(storage engine)"]
        HDR["headers.h\n(CustomHeaderValues singleton\n— inline header names)"]
        HU["HeaderUtility\n(matchers, predicates, manipulation)"]
        HM["HeaderMutation\n(programmatic add/set/remove)"]
        CHV["character_set_validation.h\n(character legality checks)"]
        HLV["HeaderListView\n(read-only view over list)"]
    end

    HDR -->|provides names| HMI
    HMI -->|used by| HU
    HU -->|called by| FilterChain["Filter Chain\n(request/response header processing)"]
    HM -->|called by| RouteConfig["Route Config\n(header_mutations in routes)"]
    CHV -->|used by| HMI
    HLV -->|wrapper over| HMI
```

---

## 2. HeaderMapImpl — Performance Design

### Storage Architecture

```mermaid
flowchart LR
    subgraph HeaderMapImpl
        Inline["inline_headers_[]\n(fixed array of ~50 slots)\nO(1) access by index"]
        List["headers_\n(std::list)\nCustom/non-inline headers"]
        ByteSize["cached_byte_size_\nuint64_t\nO(1) size query"]
    end

    req[":method: GET\n:path: /api\ncontent-type: json\nx-request-id: abc"] -->|well-known| Inline
    req -->|custom| List
```

### Inline Slot Lookup vs. List Scan

```mermaid
sequenceDiagram
    participant Caller
    participant HM as HeaderMapImpl
    participant Inline as Inline Slots Array
    participant List as headers_ list

    Caller->>HM: headers.path()
    HM->>Inline: inline_headers_[kPathIndex]
    Inline-->>HM: HeaderEntryImpl* (O(1))
    HM-->>Caller: absl::string_view "/api/v1"

    Caller->>HM: headers.get(LowerCaseString("x-custom"))
    HM->>List: scan list for "x-custom"
    List-->>HM: HeaderEntryImpl* or nullptr (O(n))
    HM-->>Caller: HeaderEntry*
```

### Byte Size Maintenance

```mermaid
sequenceDiagram
    participant C as Caller
    participant HM as HeaderMapImpl
    participant WM as Watermark Check

    C->>HM: addCopy("x-trace-id", "abc123")
    HM->>HM: cached_byte_size_ += 10 + 6 + 2  (key+value+CRLF)
    C->>HM: remove("x-trace-id")
    HM->>HM: cached_byte_size_ -= 10 + 6 + 2

    WM->>HM: byteSize()
    HM-->>WM: cached_byte_size_ (O(1))
```

### HeaderString — Small String Optimization

```mermaid
flowchart TD
    subgraph HeaderString
        union["union buffer\n(128 bytes inline OR heap ptr)"]
        size["size_: size_t"]
        type["type_: Reference | Inline | Dynamic"]
    end

    A[String ≤ 128 bytes] -->|stored| union
    B[String > 128 bytes] -->|heap allocated| union
    C[absl::string_view reference] -->|zero-copy pointer| union
```

---

## 3. Inline Header Access via Macros

Three macro families generate typed accessor methods for every well-known header:

```mermaid
mindmap
  root(Inline Header Macros)
    DEFINE_INLINE_HEADER_FUNCS
      Generic typed accessor
      get / set / remove methods
    DEFINE_INLINE_HEADER_STRING_FUNCS
      String-typed headers
      setViaMove / setCopy
    DEFINE_INLINE_HEADER_NUMERIC_FUNCS
      Integer-typed headers
      setInteger / getInteger
```

### Examples of Generated Methods

| Macro | Header | Generated Methods |
|-------|--------|------------------|
| `DEFINE_INLINE_HEADER_FUNCS(path)` | `:path` | `path()`, `setPath(value)`, `removePath()` |
| `DEFINE_INLINE_HEADER_STRING_FUNCS(content_type)` | `content-type` | `contentType()`, `setContentType(value)` |
| `DEFINE_INLINE_HEADER_NUMERIC_FUNCS(content_length)` | `content-length` | `contentLength()`, `setContentLength(uint64_t)` |

### Well-Known Inline Headers (Selected)

```
HTTP/2 Pseudo-headers:    :method, :path, :scheme, :authority, :status
Request headers:          host, authorization, content-type, content-length,
                          accept, accept-encoding, user-agent, cookie,
                          transfer-encoding, connection, upgrade
Response headers:         server, date, cache-control, location, etag, 
                          set-cookie, www-authenticate, content-encoding
Envoy-specific:           x-forwarded-for, x-forwarded-proto, x-envoy-internal,
                          x-request-id, x-b3-traceid, x-b3-spanid, x-b3-sampled,
                          x-envoy-upstream-service-time, x-envoy-attempt-count,
                          x-envoy-decorator-operation, grpc-status, grpc-message
```

---

## 4. HeaderUtility — Matchers & Manipulation

`HeaderUtility` provides higher-level operations on header maps, primarily used by routing and filter configurations.

### Header Match Predicates

```mermaid
classDiagram
    class HeaderMatcherBase {
        <<abstract>>
        +matchesHeaders(headers): bool
        +name(): absl::string_view
    }

    class ExactHeaderMatcher {
        +value_: std::string
        +matchesHeaders(headers): bool
    }

    class PrefixHeaderMatcher {
        +prefix_: std::string
    }

    class SuffixHeaderMatcher {
        +suffix_: std::string
    }

    class RegexHeaderMatcher {
        +regex_: RE2
    }

    class RangeHeaderMatcher {
        +range_start_: int64_t
        +range_end_: int64_t
    }

    class PresentHeaderMatcher
    class InvertHeaderMatcher

    HeaderMatcherBase <|-- ExactHeaderMatcher
    HeaderMatcherBase <|-- PrefixHeaderMatcher
    HeaderMatcherBase <|-- SuffixHeaderMatcher
    HeaderMatcherBase <|-- RegexHeaderMatcher
    HeaderMatcherBase <|-- RangeHeaderMatcher
    HeaderMatcherBase <|-- PresentHeaderMatcher
    HeaderMatcherBase <|-- InvertHeaderMatcher
```

### `matchHeaders(headers, matchers)` Flow

```mermaid
flowchart TD
    A["matchHeaders(request_headers, route.header_matchers)"] --> B["For each HeaderMatcher"]
    B --> C{matcher.matchesHeaders(headers)?}
    C -->|All must match| D[Return true]
    C -->|Any fails| E[Return false]
```

### Common Operations

| Method | Purpose |
|--------|---------|
| `getAllOfHeader(headers, key)` | Get all values for a repeated header |
| `addHeaders(to, from)` | Copy all headers from one map to another |
| `removeHeaders(headers, keys)` | Remove a list of headers |
| `stripConnectionSpecificHeaders(headers)` | Remove hop-by-hop headers |
| `requestHeadersValid(headers)` | Validate required pseudo-headers present |
| `isConnect(headers)` | Check if this is an HTTP CONNECT request |
| `isConnectResponse(req, resp)` | Check if response is a CONNECT success |
| `isGrpc(headers)` | Check content-type for gRPC |
| `isTrailer(headers)` | Distinguish trailer from headers |

---

## 5. HeaderMutation — Programmatic Changes

`HeaderMutation` applies ordered header mutations from route/virtual host configuration:

```mermaid
flowchart TD
    subgraph "Route Config"
        RC["route_config:\n  headers_to_add:\n    - header: {key: x-version, value: v2}\n  headers_to_remove:\n    - x-internal-debug\n  headers_to_append:\n    - header: {key: vary, value: accept}"]
    end

    subgraph "HeaderMutation::mutateRequestHeaders()"
        Add["Add: x-version: v2"]
        Rem["Remove: x-internal-debug"]
        App["Append: vary: accept"]
        Set["Set (overwrite): x-service: my-svc"]
    end

    RC --> Add --> Rem --> App --> Set --> Out["Mutated Request Headers"]
```

### Mutation Action Types

| Action | Proto Field | Behavior |
|--------|-------------|---------|
| `ADD` | `headers_to_add` | Add header (preserves existing if same key) |
| `OVERWRITE` | `headers_to_overwrite` | Replace existing value or add |
| `APPEND_IF_EXISTS_OR_ADD` | `headers_to_append` | Append to existing values |
| `REMOVE` | `headers_to_remove` | Remove all values for the key |

---

## 6. Http::Utility — URL, Encoding, gRPC

`source/common/http/utility.h` is a large namespace of free functions organized into sub-namespaces.

### URL Parsing

```mermaid
classDiagram
    class Url {
        +initialize(absolute_url, is_connect): bool
        +scheme(): absl::string_view
        +host(): absl::string_view
        +path(): absl::string_view
        +port(): uint16_t
        +fragment(): absl::string_view
        -scheme_: absl::string_view
        -host_and_port_: absl::string_view
        -path_and_query_params_: absl::string_view
    }
```

### URL Parse Flow

```mermaid
sequenceDiagram
    participant C as Caller
    participant U as Url

    C->>U: initialize("https://api.example.com:8443/v1/users?page=1#top")
    U->>U: parse scheme: "https"
    U->>U: parse host: "api.example.com:8443"
    U->>U: parse port: 8443
    U->>U: parse path: "/v1/users"
    U->>U: parse query: "?page=1"
    U->>U: parse fragment: "#top"
    U-->>C: true (success)
    C->>U: host() → "api.example.com:8443"
    C->>U: path() → "/v1/users"
```

### Percent Encoding (RFC 3986)

```mermaid
flowchart LR
    A["raw string:\n'hello world/foo?bar=1'"] --> Enc["PercentEncoding::encode()"]
    Enc --> B["encoded:\n'hello%20world%2Ffoo%3Fbar%3D1'"]
    B --> Dec["PercentEncoding::decode()"]
    Dec --> A
```

### Query Parameter Parsing

```mermaid
flowchart TD
    QS["?page=2&limit=50&filter=active"] --> Parse["Utility::parseQueryString()"]
    Parse --> Map["QueryParams map:\n  page → 2\n  limit → 50\n  filter → active"]
    Map --> Enc["parseAndDecodeQueryString()\n(also percent-decodes values)"]
```

### gRPC Utilities

| Function | Purpose |
|----------|---------|
| `Utility::isGrpc(headers)` | Check `content-type: application/grpc*` |
| `Utility::getGrpcStatus(trailers, ok_only)` | Extract `grpc-status` trailer |
| `Utility::grpcStatusToHttpStatus(grpc_status)` | Map gRPC status code → HTTP status |
| `Utility::httpToGrpcStatus(http_status)` | Map HTTP status → gRPC status |

### HTTP/2 & HTTP/3 Option Validation

```mermaid
flowchart TD
    H2Config["Http2ProtocolOptions proto"] --> Val["Utility::validateH2Settings()"]
    Val --> B{SETTINGS values\nin valid ranges?}
    B -->|Yes| OK["Apply settings"]
    B -->|No| Err["Return validation error"]

    H3Config["Http3ProtocolOptions proto"] --> Val3["Utility::validateH3Settings()"]
    Val3 --> B3{QUIC transport\nparameters valid?}
    B3 -->|Yes| OK3["Apply settings"]
    B3 -->|No| Err3["Return validation error"]
```

---

## 7. PathUtility — Path Canonicalization

`source/common/http/path_utility.h`:

```mermaid
flowchart TD
    subgraph PathUtility
        MSP["mergeSlashes(path)\n'/api//v1///users' → '/api/v1/users'"]
        CPS["canonicalPath(path)\n'/api/../v1/./users' → '/api/v1/users'"]
        UPS["unescapeSlashes(path)\n'/api%2Fv1' → '/api/v1'"]
    end

    Incoming[":path header"] --> MSP --> CPS --> UPS --> Normalized["Normalized path"]
```

### Dot-Segment Resolution (RFC 3986 §5.2.4)

```
Input:  /a/b/c/../d/./e
Step 1: /a/b/d/./e       (resolve ..)
Step 2: /a/b/d/e         (resolve .)
Output: /a/b/d/e
```

---

## 8. HashPolicy — Load Balancing

`source/common/http/hash_policy.h` implements `Router::HashPolicy` for upstream load balancing:

```mermaid
classDiagram
    class HashPolicyImpl {
        +generateHash(downstream_address, headers, add_cookie_cb, filter_state): absl::optional~uint64_t~
        -hash_impls_: vector~HashMethodPtr~
    }

    class HashMethod {
        <<interface>>
        +evaluate(downstream_addr, headers, add_cookie_cb, filter_state): absl::optional~uint64_t~
    }

    class HeaderHashMethod {
        -header_name_: LowerCaseString
    }

    class CookieHashMethod {
        -name_: string
        -ttl_: optional~Duration~
        -path_: string
    }

    class ConnectionIpHashMethod
    class QueryParameterHashMethod {
        -param_name_: string
    }

    class FilterStateHashMethod {
        -key_: string
    }

    HashPolicyImpl *-- HashMethod
    HashMethod <|-- HeaderHashMethod
    HashMethod <|-- CookieHashMethod
    HashMethod <|-- ConnectionIpHashMethod
    HashMethod <|-- QueryParameterHashMethod
    HashMethod <|-- FilterStateHashMethod
```

### Hash Policy Evaluation

```mermaid
flowchart TD
    Req["Incoming Request"] --> HP["HashPolicyImpl::generateHash()"]
    HP --> H1M["HeaderHashMethod.evaluate()\ne.g. hash(x-user-id header value)"]
    HP --> H2M["CookieHashMethod.evaluate()\ne.g. hash(session cookie value)"]
    HP --> H3M["ConnectionIpHashMethod.evaluate()\ne.g. hash(client IP)"]

    H1M -->|first non-null result| Hash["uint64_t hash"]
    H2M -->|if H1M is null| Hash
    H3M -->|if both null| Hash

    Hash -->|used by LB| LB["Consistent Hash Load Balancer\n→ select upstream host"]
```

---

## 9. Status Codes and Error Propagation

### `Http::Status` (`status.h`)

Envoy wraps `absl::Status` with HTTP-specific error codes:

```mermaid
classDiagram
    class Status {
        <<typedef absl::Status>>
    }

    class CodecProtocolError
    class PrematureResponseError
    class CodecClientError
    class InboundFramesWithEmptyPayload
    class CodecErrors {
        <<enum class>>
        CodecProtocol
        PrematureResponse
        CodecClientError
        InboundFramesWithEmptyPayload
    }
```

### Error Propagation Flow

```mermaid
sequenceDiagram
    participant Net as Network
    participant Codec as Http2::ConnectionImpl
    participant CMI as ConnectionManagerImpl
    participant CM_Stats

    Net->>Codec: dispatch(malformed_frame)
    Codec->>Codec: Protocol constraint violation
    Codec-->>CMI: Status(CodecProtocol, "too many PING frames")
    CMI->>CM_Stats: downstream_cx_protocol_error++
    CMI->>Net: close(NoFlush)
    Note over CMI: All active streams reset
```

### `CodeStatsImpl` (`codes.h`)

Tracks HTTP response code statistics in histograms and counters:

```mermaid
flowchart TD
    AS["ActiveStream::onStreamComplete()"] --> CS["CodeStatsImpl::chargeResponseStat()"]
    CS --> B{Response code?}
    B -->|1xx| C["downstream_rq_1xx++"]
    B -->|2xx| D["downstream_rq_2xx++"]
    B -->|3xx| E["downstream_rq_3xx++"]
    B -->|4xx| F["downstream_rq_4xx++"]
    B -->|5xx| G["downstream_rq_5xx++"]
    CS --> H["downstream_rq_time.record(latency_ms)"]
    CS --> I["upstream_rq_time.record(upstream_latency_ms)"]
```

---

## 10. ConnectionManagerUtility — Header Mutation at Runtime

(See also: individual `conn_manager_utility.md` doc)

### Header Mutation Decision Tree

```mermaid
flowchart TD
    Req["Incoming Downstream Request"] --> XFF{"xff_num_trusted_hops > 0?"}
    XFF -->|Yes| AppendIP["Append local IP to x-forwarded-for"]
    XFF -->|No| ReplaceIP["Replace x-forwarded-for with local IP"]
    AppendIP --> Proto["Set x-forwarded-proto"]
    ReplaceIP --> Proto
    Proto --> Internal{"isInternal(remote_addr)?"}
    Internal -->|Yes| SetInt["x-envoy-internal: true"]
    Internal -->|No| RemInt["Remove x-envoy-internal"]
    SetInt --> Envoy["Remove untrusted x-envoy-* headers"]
    RemInt --> Envoy
    Envoy --> RequestID["Generate/propagate x-request-id"]
    RequestID --> Trace["mutateTracingRequestHeader()"]
    Trace --> UA["Sanitize User-Agent"]
    UA --> PathNorm["maybeNormalizePath()"]
    PathNorm --> HostNorm["maybeNormalizeHost()"]
    HostNorm --> Done["Mutated Request Headers → Filter Chain"]
```

---

## Navigation

| Part | Topics |
|------|--------|
| [Part 1](OVERVIEW_PART1_request_pipeline.md) | Architecture, Request Pipeline, ConnectionManager, FilterSystem |
| [Part 2](OVERVIEW_PART2_codecs_and_pools.md) | Codecs (H1/H2/H3), Connection Pools, Protocol Details |
| **Part 3 (this file)** | Header System, Utilities, Path Normalization |
| [Part 4](OVERVIEW_PART4_async_and_advanced.md) | Async Client, HTTP/3, Server Properties, Advanced Topics |
