# MCP SSE Stateful Session Extension

This extension implements the MCP 241105 specification for SSE-based session tracking in Envoy. It enables Envoy to handle session context in Server-Sent Events (SSE) event streams, allowing session ID and upstream host to be encoded/decoded as required by the protocol.

## Overview

The extension provides stateful session functionality specifically designed for SSE connections, following the MCP 241105 specification. It allows Envoy to:

1. **Encode session information**: When processing SSE responses from upstream servers, the extension encodes the session ID and upstream host address using Base64Url encoding with a separator.

2. **Decode session information**: When processing requests from downstream clients, the extension decodes the session information to extract the original session ID and upstream host address.

## Configuration

The extension is configured using the `envoy.http.stateful_session.mcp_sse` extension:

```yaml
stateful_session:
  name: "envoy.http.stateful_session.mcp_sse"
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.http.stateful_session.mcp_sse.v3.McpSseSessionState
    param_name: "sessionId"
```

### Configuration Parameters

- `param_name` (string, required): The query parameter name used to track the session state in SSE data streams.

## How It Works

### Response Processing (Upstream → Downstream)

When processing SSE responses from upstream servers:

1. The extension detects SSE responses by checking the `Content-Type` header for `text/event-stream`
2. It scans the SSE data stream for the specified parameter name
3. When found, it encodes the session ID and upstream host address in the format:
   ```
   sessionId={original_session_id}.{encoded_host}
   ```
   Where `{encoded_host}` is the Base64Url encoded host address.

### Request Processing (Downstream → Upstream)

When processing requests from downstream clients:

1. The extension parses the URL query parameters
2. It looks for the specified parameter name
3. When found, it splits the value at the last dot (`.`) separator
4. It decodes the host address using Base64Url decoding
5. It updates the request path to contain only the original session ID
6. It returns the decoded host address for upstream routing

## Example

### Initial SSE Response
```
Content-Type: text/event-stream

data: {"url": "/path?sessionId=abc123&other=value"}
```

### After Processing (with upstream host 127.0.0.1:80)
```
data: {"url": "/path?sessionId=abc123.MTI3LjAuMC4xOjgw&other=value"}
```

### Subsequent Request
```
GET /path?sessionId=abc123.MTI3LjAuMC4xOjgw&other=value
```

### After Processing
```
GET /path?sessionId=abc123&other=value
```
And the extension returns `127.0.0.1:80` for upstream routing.

## Building

To build the extension with Envoy:

```bash
bazel build //contrib/mcp_sse_stateful_session/filters/http/source:config
```

## Testing

To run the unit tests:

```bash
bazel test //contrib/mcp_sse_stateful_session/filters/http/test:mcp_sse_test
```

## Notes

- The extension uses Base64Url encoding for the host address to ensure URL-safe characters
- The separator character is `.` (dot)
- The extension supports all standard SSE line ending patterns (CRLFCRLF, CRCR, LFLF)
- Invalid Base64Url encoded host addresses are rejected and no session state is returned 