## Summary of changes

## Breaking changes

- **build**: Envoy now uses Bazel 8. Because Envoy still uses WORKSPACE mode, `--enable_workspace` and `--noenable_bzlmod` are required and have been added to `.bazelrc`; external-repository runfiles now appear directly under the runfiles root.
- **build**: the Intel DLB connection balancer (`envoy.network.connection_balance.dlb`) is disabled for all builds due to a broken source archive.
- **TLS**: `enforce_rsa_key_usage` is deprecated and ignored; Envoy now always enforces the certificate `keyUsage` extension.
- **TLS inspector**: client TLS versions are validated and must be between TLS 1.0 and TLS 1.3 (revertible via `envoy.reloadable_features.tls_inspector_enforce_client_tls_version`).
- **OpenTelemetry tracing**: the tracer now honors Envoy's request-entry sampling decision, including `overall_sampling`, even when propagated trace context or the configured sampler requests sampling. This may reduce exported spans.

### Security
- HTTP/2 now counts uncompressed cookies toward header-size and header-count limits (**CVE-2026-47774**), strengthens PRIORITY/WINDOW_UPDATE flood protection, and adds configurable nghttp2 RST_STREAM rate limits.
- HTTP/3 fixes cover QPACK blocked-decoding denial of service (**GHSA-p7c7-7c47-pwch**) and inconsistent headers-only `content-length` handling (**CVE-2026-48743**).
- Security fixes were added for ext_authz (**CVE-2026-47205**), ext_proc (**CVE-2026-47207**), gRPC stats (**CVE-2026-47204**), internal redirects (**CVE-2026-47221**), and OAuth2 lifecycle handling (**CVE-2026-48090**).
- OAuth2 adds AES-256-GCM cookie encryption to address **CVE-2026-47775**. Migration is opt-in: enable `oauth2_use_gcm_encryption`, monitor `oauth_legacy_cbc_decrypt`, then disable `oauth2_legacy_cbc_decrypt_compat`.
- Additional fixes cover DNS query validation (**CVE-2026-48497**), JSON nesting limits (**CVE-2026-48042**), PROXY protocol TLV smuggling (**CVE-2026-47692**), formatter crashes (**CVE-2026-47220**), TCP StatsD overflow (**CVE-2026-48706**), TLS SAN NUL handling (**CVE-2026-47778**), and Zstd decompression memory exhaustion (**CVE-2026-48044**).
- New upstream RBAC and dynamic-forward-proxy resolved-address filtering provide CIDR-based protection against SSRF after DNS resolution and host selection.

### Dynamic modules
- New extension points: access-log/header formatters, downstream and upstream transport sockets, active health checkers, and stats sinks.
- Cluster load balancers can read host stats, read and write dynamic metadata and filter state, and publish main-thread state to worker-local slots.
- Modules can emit metrics from configuration and background contexts; loading and initialization failures now expose server-wide counters tagged by extension instance.
- Added validation-mode detection, network/listener attribute access, batched header and metadata APIs, effective log-level access, and zero-copy borrowed buffers in the Rust SDK.
- Fixed listener HTTP-callout crashes, watermark initialization, streaming-response re-entry, independent decode/encode continuation, CatchUnwind re-entry, `Struct` configuration handling, and HTTP/TCP bridge buffer overflow with more than 64 slices.

### MCP (Model Context Protocol) and AI protocols
- Added a Wuffs-backed streaming JSON parser for MCP, A2A, OpenAI, Anthropic, and related protocols, with bounded field capture, incremental parsing, no DOM allocation, and duplicate-key detection.
- MCP filtering now exposes processing status, supports configurable duplicate-key rejection, and improves oversized-body behavior for pass-through and rejection modes.
- MCP router adds elicitation and server-to-client request routing, plus lazy per-backend initialization.
- MCP JSON REST Bridge adds per-route tool configuration and locally generated `tools/list` responses.

### HTTP, routing and protocol
- New HTTP filters provide weighted bandwidth sharing and selectable sub-filter chains with per-route configuration.
- `HeaderMatcher` now evaluates separately supplied header values individually instead of matching only their comma-joined representation (revertible via `envoy.reloadable_features.match_headers_individually`).
- HTTP inspector uses Balsa by default and now fast-fails invalid non-HTTP method bytes instead of buffering up to 64 KiB.
- Added drain-timeout and maximum-connection-duration jitter to reduce synchronized reconnect spikes.
- Routing gains cluster refresh on retries, weighted-cluster reselection, formatter/CEL-based redirect paths, and mixed literal/variable URI-template segments.
- Fixed connection-pool re-entrancy, connectivity-grid teardown and duplicate-attempt bugs, stale on-demand cluster information, and handling of HTTP/2 or HTTP/3 `RST_STREAM(NO_ERROR)` after complete responses.

### TLS, authentication and authorization
- Added CNSA 1.0/2.0 compliance policies, TLS group formatters for identifying post-quantum key exchange, and suppression of oversized client CA lists.
- QUIC supports opt-in TLS key logging and session-ticket resumption.
- TLS certificate compression via the brotli runtime guard is disabled by default; TLS sockets also gain improved connection-reset reporting.
- OAuth2 adds `PRIVATE_KEY_JWT`, ID-token forwarding, configurable post-logout redirects, access-token-based cookie lifetime, and safe original-request URI formatting with redirect-domain allowlists.
- BasicAuth supports missing-credential pass-through and authenticated-username metadata; JWT extraction can mark forwarded claims whose signatures were not verified.

### DNS, load balancing and upstream
- The unified DNS cluster implementation is enabled by default; equivalent c-ares resolvers can be shared across clusters to support shared qcache.
- DNS clusters gain a minimum TTL refresh floor, while dynamic forward proxy sub-clusters can use `DnsCluster` configuration and resolved-address CIDR filtering.
- Client-side weighted round robin adds out-of-band ORCA reporting with configurable port, authority, and transport socket matching.
- Least-request load balancing can optionally account for pending requests, with a new per-endpoint pending-request gauge.
- Fixed EDS hostname updates, load reports containing only custom metrics or completed requests, and dynamic-forward-proxy lookup teardown.

### Networking and system performance
- Linux deployments gain worker CPU affinity and an `SO_REUSEPORT` BPF connection balancer for CPU-local connection steering.
- Added a Linux sockmap socket interface for accelerating same-host TCP traffic through eBPF.
- io_uring adds multishot receives, adaptive read buffers, and configurable write backpressure.
- Reverse tunnels gain opt-in GOAWAY-aware draining and replacement; MySQL proxy adds downstream TLS termination with `caching_sha2_password` mediation.
- Added UDP proxy external authorization, network ext_proc metadata reception, and early external-processor stream closure.

### Observability
- New access-log data includes upstream TLS SNI and HTTP/TCP downstream/upstream connection duration points; gRPC access logging adds delivery success/failure counters.
- Added dynamic-module and Wasm programmable stats sinks, OpenTelemetry metric request chunking, endpoint observability names, and default-tag overrides.
- Prometheus scraping and large-scale stat-reference release are substantially faster; `/peak_heap_dump` exposes tcmalloc's peak heap profile.
- Added process-wide Lua and Wasm VM gauges, single-entry stack traces, and an Envoy-version log-format token.
- Tap now honors configured runtime sampling and reports sampled-out requests/connections.

### Rate limiting and configuration
- Rate limiting adds static and dynamic per-descriptor limit overrides, zero-token always-reject behavior, and configurable response-metadata namespaces.
- GCP authentication supports bound access tokens, bound JWTs, and unbound access tokens from the metadata server.
- Added file-backed IP tagging, in-place watched-file modification events, runtime-adjustable fixed-heap limits, health-check HTTP status events, and xDS unsubscribe callbacks.
- Redis proxy adds Redis 7.4 hash-field expiry commands and permits custom commands inside transactions.

### Other notable changes and fixes
- Fixed Hickory DNS resolver leaks and use-after-free, file-server cancellation use-after-free, Golang filter re-entry, outlier-detection teardown, missing network namespaces, and asynchronous TLS close handling.
- Fixed RTDS guard removal, VHDS subscriptions, Wasm VM cache invalidation when environment variables change, and upstream Wasm metric scoping.
- Improved UDP proxy attempted-host and address logging, Zipkin timestamp trace IDs, gRPC access-log failure accounting, and health-check event detail.
- Added TCP proxy delayed route selection, drain-close handling, and connection-duration access logging.
