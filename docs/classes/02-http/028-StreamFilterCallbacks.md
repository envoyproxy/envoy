# Part 28: StreamFilterCallbacks

**File:** `envoy/http/filter.h`  
**Namespace:** `Envoy::Http`

## Summary

`StreamFilterCallbacks` is the base interface for HTTP filter callbacks. It provides `streamInfo`, `connection`, `route`, `requestRoute`, `encoderBufferLimit`, `decoderBufferLimit`, and `activeSpan`. Extended by StreamDecoderFilterCallbacks and StreamEncoderFilterCallbacks.

## UML Diagram

```mermaid
classDiagram
    class StreamFilterCallbacks {
        <<interface>>
        +streamInfo() StreamInfo
        +connection() Connection
        +route() Route
        +requestRoute() Route
        +encoderBufferLimit() uint64_t
        +decoderBufferLimit() uint64_t
        +activeSpan() Tracing.Span
        +upstreamHost() HostDescription
        +upstreamInfo() UpstreamInfo
        +downstreamAddress() Address.Instance
        +connectionId() uint64_t
    }
    StreamDecoderFilterCallbacks --|> StreamFilterCallbacks
    StreamEncoderFilterCallbacks --|> StreamFilterCallbacks
```

## Important Functions

| Function | One-line description |
|----------|----------------------|
| `streamInfo()` | Returns StreamInfo for this request. |
| `connection()` | Returns Network::Connection. |
| `route()` | Returns route for request. |
| `requestRoute()` | Returns route (may differ after redirect). |
| `encoderBufferLimit()` | Encoder buffer limit. |
| `decoderBufferLimit()` | Decoder buffer limit. |
| `activeSpan()` | Current tracing span. |
| `upstreamHost()` | Selected upstream host. |
| `upstreamInfo()` | Upstream connection info. |
| `connectionId()` | Connection ID. |
