# ConnectionManagerUtility

**File:** `source/common/http/conn_manager_utility.h` / `.cc`  
**Size:** ~6.8 KB header, ~32 KB implementation  
**Namespace:** `Envoy::Http`

## Overview

`ConnectionManagerUtility` is a **stateless utility class** containing pure static methods used by `ConnectionManagerImpl`. It handles protocol detection, codec instantiation, request/response header mutation, path normalization, and tracing header injection. By being stateless, it is trivially testable and has no dependency on the connection manager's lifecycle.

## Static Methods Overview

```mermaid
mindmap
  root((ConnectionManagerUtility))
    Protocol Detection
      determineNextProtocol
      autoCreateCodec
    Request Headers
      mutateRequestHeaders
      mutateTracingRequestHeader
      maybeNormalizePath
      maybeNormalizeHost
    Response Headers
      mutateResponseHeaders
    Tracing
      mutateTracingRequestHeader
```

## Protocol Detection Flow

### `determineNextProtocol(connection, data)`

Sniffs the first bytes of a connection to determine H1 vs H2:

```mermaid
flowchart TD
    A[Raw bytes arrive] --> B{ALPN negotiated on TLS?}
    B -->|h2| C[Return Protocol::Http2]
    B -->|http/1.1 or none| D{First bytes = HTTP/2 preface\n'PRI * HTTP/2.0'}
    D -->|Yes| C
    D -->|No| E[Return Protocol::Http11]
```

### `autoCreateCodec(connection, data, callbacks, ...)`

Creates the appropriate `ServerConnection` based on protocol detection:

```mermaid
sequenceDiagram
    participant CM as ConnectionManagerImpl
    participant CMU as ConnectionManagerUtility
    participant H1 as Http1::ServerConnection
    participant H2 as Http2::ServerConnection

    CM->>CMU: autoCreateCodec(connection, data, ...)
    CMU->>CMU: determineNextProtocol(connection, data)
    alt HTTP/2
        CMU->>H2: new ServerConnection(connection, callbacks, settings, ...)
        H2-->>CMU: ServerConnectionPtr
    else HTTP/1.1
        CMU->>H1: new ServerConnection(connection, callbacks, settings, ...)
        H1-->>CMU: ServerConnectionPtr
    end
    CMU-->>CM: ServerConnectionPtr
```

## Request Header Mutation

### `mutateRequestHeaders(headers, connection, config, ...)`

The most complex utility method. Applied to every incoming downstream request before entering the filter chain:

```mermaid
flowchart TD
    A[Incoming Request Headers] --> B[Determine trusted downstream hops]
    B --> C{x-forwarded-for trusted?}
    C -->|Yes| D[Append local IP to XFF]
    C -->|No| E[Replace XFF with local IP]
    D --> F[Set x-forwarded-proto if missing]
    E --> F
    F --> G{internal_address_config.isInternal?}
    G -->|Yes| H[Set x-envoy-internal: true]
    G -->|No| I[Remove x-envoy-internal]
    H --> J[Remove x-envoy-* if untrusted]
    I --> J
    J --> K[Normalize path via maybeNormalizePath]
    K --> L[Normalize host via maybeNormalizeHost]
    L --> M[Generate/propagate x-request-id]
    M --> N[Sanitize user-agent per config]
    N --> O[Output: Mutated Request Headers]
```

### Headers Added / Modified

| Header | Action | Condition |
|--------|--------|-----------|
| `x-forwarded-for` | Append or replace client IP | Always |
| `x-forwarded-proto` | Set `http` or `https` | If missing |
| `x-envoy-internal` | Set `true` | If internal address |
| `x-envoy-decorator-operation` | Set from route | If route has decorator |
| `x-request-id` | Generate UUID or propagate | Always (if extension enabled) |
| `x-envoy-*` (upstream directives) | Remove | If downstream is untrusted |
| `user-agent` | Append Envoy version | If configured |

### `mutateTracingRequestHeader(headers, runtime, config, ...)`

Injects tracing context into the request headers based on sampling decisions:

```mermaid
flowchart TD
    A[mutateTracingRequestHeader] --> B{Tracing disabled?}
    B -->|Yes| Z[No-op]
    B -->|No| C{Sampling decision}
    C -->|Random sampling| D[Check runtime random_sampling flag]
    C -->|Client forced| E[x-envoy-force-trace present?]
    C -->|Service forced| F[Check force_tracing_header]
    D --> G[Set x-b3-sampled / x-envoy-force-trace]
    E --> G
    F --> G
    G --> H[Propagate trace context headers]
```

## Response Header Mutation

### `mutateResponseHeaders(headers, request_headers, config, ...)`

Applied to every upstream response before entering the encoder filter chain:

```mermaid
flowchart TD
    A[Upstream Response Headers] --> B[Remove hop-by-hop headers]
    B --> C{config.serverName?}
    C -->|Set| D[Set Server: header]
    C -->|Not set| E[Remove Server: header]
    D --> F[Set Date: header if missing]
    E --> F
    F --> G{Proxy-Status enabled?}
    G -->|Yes| H[Add Proxy-Status: envoy; ... header]
    G -->|No| I[Skip]
    H --> J[Output: Mutated Response Headers]
    I --> J
```

### Hop-by-Hop Headers Removed

Per HTTP/1.1 spec (RFC 7230), these are always stripped from proxied responses:

- `Connection`
- `Keep-Alive`
- `Proxy-Authenticate`
- `Proxy-Authorization`
- `TE`
- `Trailer`
- `Transfer-Encoding`
- `Upgrade`
- Any header listed in the `Connection` header value

## Path Normalization

### `maybeNormalizePath(headers, config)`

```mermaid
flowchart TD
    A[Request Path] --> B{path_with_escaped_slashes_action?}
    B -->|KEEP_UNCHANGED| C[No-op]
    B -->|REJECT_REQUEST| D[Return 400]
    B -->|UNESCAPE_AND_REDIRECT| E[Unescape %2F → / then 301 redirect]
    B -->|UNESCAPE_AND_FORWARD| F[Unescape %2F → /]
    C --> G{path_normalization_policy?}
    F --> G
    G -->|NONE| H[No further normalization]
    G -->|NormalizePathRFC3986| I[RFC 3986 dot-segment removal, duplicate slash merge]
    G -->|MergeSlashes| J[Merge // → /]
    I --> K[Updated :path header]
    J --> K
    H --> K
```

### `maybeNormalizeHost(headers, config, port)`

Strips the port number from the `Host`/`:authority` header if it matches the listener port (to avoid duplicate port issues):

```mermaid
flowchart TD
    A["Host: example.com:443"] --> B{strip_matching_host_port?}
    B -->|Yes| C{Port matches listener port?}
    C -->|Yes| D["Host: example.com"]
    C -->|No| E[No change]
    B -->|No| E
```

## `determineNextProtocol` — Detailed Byte Inspection

```
H2 preface bytes: "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
                   ↑ First 3 bytes checked: "PRI"

If first bytes == "PRI" → HTTP/2
Otherwise             → HTTP/1.1
```

## Key Enums and Config Options

| Config Field | Options | Effect |
|-------------|---------|--------|
| `path_normalization_policy` | NONE, NormalizePathRFC3986, MergeSlashes | Controls path canonicalization |
| `path_with_escaped_slashes_action` | KEEP, REJECT, UNESCAPE_REDIRECT, UNESCAPE_FORWARD | Controls `%2F` handling |
| `xff_num_trusted_hops` | integer | How many XFF hops to trust |
| `skip_xff_append` | bool | Suppress adding client IP to XFF |
| `via` | string | Value for `Via:` header (if set) |
| `server_name` | string | Value for `Server:` response header |
| `strip_matching_host_port` | bool | Strip port from Host if matches listener |
| `proxy_status_config` | ProxyStatusConfig | Controls Proxy-Status response header |

## Relationship to `ConnectionManagerImpl`

```mermaid
sequenceDiagram
    participant CMI as ConnectionManagerImpl
    participant CMU as ConnectionManagerUtility
    participant Codec as ServerCodec
    participant FM as FilterManager

    Note over CMI: onData() received
    CMI->>CMU: autoCreateCodec(connection, data)
    CMU-->>CMI: codec_

    Note over CMI: newStream() → ActiveStream created
    CMI->>CMU: mutateRequestHeaders(headers, ...)
    CMU-->>CMI: mutated headers
    CMI->>FM: decodeHeaders(mutated_headers)

    Note over CMI: response from upstream
    CMI->>CMU: mutateResponseHeaders(response_headers, ...)
    CMU-->>CMI: mutated response headers
    CMI->>Codec: encodeHeaders(mutated_response_headers)
```
