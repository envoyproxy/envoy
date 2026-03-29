# Part 23: StreamEncoderFilter

**File:** `envoy/http/filter.h`  
**Namespace:** `Envoy::Http`

## Summary

`StreamEncoderFilter` is the interface for HTTP encoder filters. It processes response headers, data, and trailers. It receives `StreamEncoderFilterCallbacks` for `continueEncoding`, `addEncodedData`, etc. Encoder filters run in reverse order of config.

## UML Diagram

```mermaid
classDiagram
    class StreamEncoderFilter {
        <<interface>>
        +encode1xxHeaders(ResponseHeaderMap) Filter1xxHeadersStatus
        +encodeHeaders(ResponseHeaderMap, bool) FilterHeadersStatus
        +encodeData(Buffer.Instance, bool) FilterDataStatus
        +encodeTrailers(ResponseTrailerMap) FilterTrailersStatus
        +encodeMetadata(MetadataMapPtr) FilterMetadataStatus
    }
    class StreamEncoderFilterCallbacks {
        <<interface>>
        +continueEncoding()
        +encodingBuffer() Buffer.Instance
        +addEncodedData(Buffer.Instance, bool)
        +injectEncodedDataToFilterChain(Buffer.Instance, bool)
        +addEncodedTrailers() ResponseTrailerMap
    }
    StreamEncoderFilter ..> StreamEncoderFilterCallbacks : uses
```

## StreamEncoderFilter

| Function | One-line description |
|----------|----------------------|
| `encode1xxHeaders(ResponseHeaderMap&)` | Processes 1xx headers. |
| `encodeHeaders(ResponseHeaderMap&, bool)` | Processes response headers. |
| `encodeData(Buffer&, bool)` | Processes response body. |
| `encodeTrailers(ResponseTrailerMap&)` | Processes response trailers. |
| `encodeMetadata(MetadataMapPtr)` | Processes METADATA. |

## StreamEncoderFilterCallbacks (Key)

| Function | One-line description |
|----------|----------------------|
| `continueEncoding()` | Resumes encoder filter chain. |
| `encodingBuffer()` | Returns buffered response data. |
| `addEncodedData(data, streaming)` | Adds data to response buffer. |
| `injectEncodedDataToFilterChain(data, end_stream)` | Injects data bypassing buffering. |
| `addEncodedTrailers()` | Adds response trailers. |
