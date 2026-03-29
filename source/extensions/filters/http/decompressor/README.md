# Decompressor HTTP Filter

This document explains the **decompressor** HTTP filter implementation in `source/extensions/filters/http/decompressor/`. The filter decompresses request and/or response bodies (e.g. gzip, brotli, zstd) and can advertise support for response decompression to upstream via `Accept-Encoding`.

---

## Overview

| Aspect | Description |
|--------|-------------|
| **Role** | Decompress HTTP request and response bodies; optionally advertise response decompression to upstream. |
| **Base class** | `Http::PassThroughFilter` (pass-through decoder + encoder that returns `Continue` by default; this filter overrides only the methods it needs). |
| **Config proto** | `envoy.extensions.filters.http.decompressor.v3.Decompressor` |
| **Extension name** | `envoy.filters.http.decompressor` |

The filter is **bidirectional**: it can decompress **requests** (downstream → Envoy) and **responses** (upstream → Envoy). Each direction is configured separately and can be toggled at runtime.

---

## File Layout

| File | Purpose |
|------|---------|
| `decompressor_filter.h` | `DecompressorFilter`, `DecompressorFilterConfig`, `ByteTracker`, and helper templates. |
| `decompressor_filter.cc` | Filter callbacks (decode/encode headers, data, trailers) and `decompress()`. |
| `config.h` | `DecompressorFilterFactory` declaration. |
| `config.cc` | Factory: resolve decompressor library from proto, build `DecompressorFilterConfig`, register filter. |
| `v3/decompressor.proto` | API: `Decompressor`, `CommonDirectionConfig`, `RequestDirectionConfig`, `ResponseDirectionConfig`. |

---

## Configuration Model

### Proto (v3/decompressor.proto)

- **`decompressor_library`** (required)  
  Typed extension config for the decompressor implementation (e.g. gzip, brotli, zstd). Used for both request and response.

- **`request_direction_config`**  
  - **`common_config`**: `enabled` (runtime flag), `ignore_no_transform_header`.  
  - **`advertise_accept_encoding`** (default true): when response decompression is enabled, append the library’s encoding to the request’s `Accept-Encoding` before sending to upstream.

- **`response_direction_config`**  
  - **`common_config`** only: same `enabled` and `ignore_no_transform_header` for the response path.

### C++ config (DecompressorFilterConfig)

- **DirectionConfig** (base for request/response):
  - **Stats**: `decompressed`, `not_decompressed`, `total_compressed_bytes`, `total_uncompressed_bytes` (per direction).
  - **Runtime**: `decompression_enabled_` from `CommonDirectionConfig.enabled`.
  - **`ignoreNoTransformHeader()`**: whether to decompress when `Cache-Control: no-transform` is present.

- **RequestDirectionConfig** adds **`advertiseAcceptEncoding()`**.

- **DecompressorFilterConfig** holds:
  - `stats_prefix_`, `trailers_prefix_`, `decompressor_stats_prefix_`
  - `decompressor_factory_` (creates per-stream decompressors)
  - `request_direction_config_`, `response_direction_config_`
  - Helpers: `makeDecompressor()`, `contentEncoding()`, `trailersCompressedBytesString()`, `trailersUncompressedBytesString()` (used for byte-report trailers).

---

## Request Path (Decode)

### decodeHeaders

1. **Response decompression advertisement**  
   If response decompression is enabled and `advertise_accept_encoding` is true, the filter **appends** the decompressor library’s encoding (e.g. `gzip`) to the request’s `Accept-Encoding` so upstream may send compressed responses.

2. **Headers-only requests**  
   If `end_stream` is true, return `Continue`; no body means no decompression.

3. **Request decompression setup**  
   Otherwise call **`maybeInitDecompress`** for the request direction:
   - **Conditions**: `decompressionEnabled()`, and either no `Cache-Control: no-transform` or `ignoreNoTransformHeader()`, and **`contentEncodingMatches`** (first value in `Content-Encoding` matches the library’s encoding, e.g. `gzip`).
   - If all pass: increment `decompressed`, create **`request_decompressor_`** via `makeDecompressor()`, **remove** `Content-Length`, **modify** `Content-Encoding` (remove the matching encoding; if nothing left, remove the header).
   - If not: increment `not_decompressed`, leave headers and decompressor unchanged.

### decodeData

- If **`request_decompressor_`** is set:
  - If **`end_stream`** is true, call **`decoder_callbacks_->addDecodedTrailers()`** and pass that trailer map into `decompress` so the filter can **add byte-count trailers** (see below).
  - Call **`decompress(..., request_decompressor_, ..., request_byte_tracker_, trailers)`** (in-place: drain input buffer, add decompressed output to same buffer).
- Always return **`FilterDataStatus::Continue`**.

### decodeTrailers

- If the filter decompressed the request, **`request_byte_tracker_.reportTotalBytes(trailers)`** writes the compressed/uncompressed byte counts into the **decoded** trailer map (the one added in `decodeData` when `end_stream` was true).
- Return `Continue`.

---

## Response Path (Encode)

### encodeHeaders

- If **`end_stream`** is true (headers-only response), return `Continue`.
- Otherwise call **`maybeInitDecompress`** for the response direction with the response headers. Same logic as request: if enabled, no no-transform (or ignored), and `Content-Encoding` matches, then **`response_decompressor_`** is set, `Content-Length` is removed, and `Content-Encoding` is updated.

### encodeData

- If **`response_decompressor_`** is set:
  - If **`end_stream`** is true, call **`encoder_callbacks_->addEncodedTrailers()`** and pass that trailer map into `decompress` so the filter can add byte-count trailers on the **encoded** (downstream) side.
  - Call **`decompress(..., response_decompressor_, ..., response_byte_tracker_, trailers)`** (again in-place on the data buffer).
- Always return **`FilterDataStatus::Continue`**.

**Important:** Adding encoded trailers when `end_stream` is true causes the HTTP/2 codec to send the last **DATA** frame **without** END_STREAM, then send the trailers in a **HEADERS** frame with END_STREAM. For protocols that expect the final DATA frame to have END_STREAM (e.g. gRPC), this can cause RSTs. There is no config option to disable adding these trailers. See the top-level decompressor docs for gRPC and workarounds.

### encodeTrailers

- If the filter decompressed the response, **`response_byte_tracker_.reportTotalBytes(trailers)`** writes byte counts into the **encoded** trailer map (the one added in `encodeData` when `end_stream` was true).
- Return `Continue`.

---

## Core Logic: `decompress()`

```text
decompress(direction_config, decompressor, callbacks, input_buffer, byte_tracker, trailers)
```

1. **Decompress**: `decompressor->decompress(input_buffer, output_buffer)` (library-specific; may be streaming).
2. **Stats**: `byte_tracker.chargeBytes(compressed_len, uncompressed_len)`; increment `direction_config.stats().total_compressed_bytes_` and `total_uncompressed_bytes_`.
3. **Replace buffer**: `input_buffer.drain(length)` then `input_buffer.add(output_buffer)` so the rest of the chain sees decompressed data in the same buffer.
4. **Trailers**: If `trailers` is set (last data chunk had `end_stream`), call **`byte_tracker.reportTotalBytes(trailers)`** so the trailer map gets the `x-envoy-decompressor-*-compressed-bytes` and `x-envoy-decompressor-*-uncompressed-bytes` entries.

So the filter **always** reports compressed/uncompressed sizes in stats; when the last chunk has `end_stream`, it also reports them in **request or response trailers** (decoded or encoded depending on direction).

---

## ByteTracker and Trailers

- **ByteTracker** holds:
  - Trailer names: `{prefix}-compressed-bytes`, `{prefix}-uncompressed-bytes` (e.g. `x-envoy-decompressor-gzip-compressed-bytes`).
  - Running totals: `total_compressed_bytes_`, `total_uncompressed_bytes_`.
- **`chargeBytes(compressed, uncompressed)`**: add to the running totals (called every time `decompress()` runs).
- **`reportTotalBytes(trailers)`**: set the two trailer keys on the given `HeaderMap` to the current totals. Used when the stream ends with a data chunk (`end_stream` true) so the filter first adds a trailer map via `addDecodedTrailers()` / `addEncodedTrailers()`, then passes it into `decompress()` so `reportTotalBytes` can write into it.

---

## When Decompression Is Skipped

Decompression is **not** used when:

- **Headers-only** request/response (`end_stream` true in headers).
- **Runtime disabled**: `CommonDirectionConfig.enabled` is false for that direction.
- **Cache-Control: no-transform** is present and **`ignore_no_transform_header`** is false.
- **Content-Encoding** does not match: the first coding in the header (before any comma) does not match the decompressor library’s encoding (e.g. not `gzip` for the gzip library).

In those cases the filter only updates stats (`not_decompressed`) and does not create a decompressor or modify body/trailers.

---

## Factory (config.cc)

1. Resolve **decompressor library** from `decompressor_library.typed_config()` via `NamedDecompressorLibraryConfigFactory` (e.g. gzip, brotli, zstd).
2. Build **DecompressorFilterConfig** (direction configs + decompressor factory).
3. Register a **stream filter** that constructs **DecompressorFilter** with that config.

No decompression happens in the factory; it only wires config and the filter instance.

---

## Flow Summary

```text
Request (downstream → Envoy):
  decodeHeaders  → maybe advertise Accept-Encoding; maybe init request_decompressor_, strip Content-Length / update Content-Encoding
  decodeData     → if request_decompressor_, decompress in place; if end_stream, add decoded trailers and report bytes there
  decodeTrailers → if request_decompressor_, report bytes into existing decoded trailers

Response (upstream → Envoy):
  encodeHeaders  → maybe init response_decompressor_, strip Content-Length / update Content-Encoding
  encodeData     → if response_decompressor_, decompress in place; if end_stream, add encoded trailers and report bytes there
  encodeTrailers → if response_decompressor_, report bytes into existing encoded trailers
```

All data is modified **in place** in the same buffer; the filter always returns `Continue` so the rest of the chain sees the decompressed body and (when applicable) the extra byte-count trailers.
