# StreamInfo

**File:** `envoy/stream_info/stream_info.h`  
**Implementation:** `source/common/stream_info/stream_info_impl.h`

## Summary

`StreamInfo` holds request/connection metadata used throughout the pipeline: timing, route, upstream host, response flags, filter state. It is the primary data source for access logs, tracing, and filters. `StreamInfoImpl` is the concrete implementation.

## Key Data (from source)

### Timing

- `startTime()`, `requestComplete()` — Request timing.
- `upstreamTiming()` — Upstream connection, first byte, etc.

### Routing

- `route()` — Selected route.
- `upstreamHost()` — Selected upstream host.
- `upstreamClusterInfo()` — Cluster info.

### Response

- `responseCode()` — HTTP status.
- `responseFlags()` — `CoreResponseFlag` bits (NoHealthyUpstream, Timeout, etc.).
- `hasResponseFlag(flag)` — Check specific flag.

### Filter State

- `filterState()` — Per-request filter state object.

## CoreResponseFlag (from `envoy/stream_info/stream_info.h`)

| Flag | Meaning |
|------|---------|
| `NoHealthyUpstream` | No healthy upstream |
| `UpstreamRequestTimeout` | Upstream timeout |
| `NoRouteFound` | No route matched |
| `RateLimited` | Rate limited |
| `UpstreamRetryLimitExceeded` | Retries exhausted |
| etc. | See enum for full list |

## Source References

- `source/common/stream_info/stream_info_impl.cc`
- Used by: ConnectionManagerImpl::ActiveStream, Router::Filter, AccessLog, Tracing
