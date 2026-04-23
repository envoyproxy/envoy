## Summary of changes

## Breaking changes

- **tcp_proxy**: `max_early_data_bytes` must be set explicitly for `upstream_connect_mode` values other than `IMMEDIATE`; missing configurations now fail validation at startup.
- **on_demand**: the on-demand filter no longer performs internal redirects after a successful CDS fetch, so earlier filters are not invoked twice (revertible via `envoy.reloadable_features.on_demand_cluster_no_recreate_stream`).
- **BoringSSL/FIPS**: the `--define=boringssl=fips` flag has been removed; use `--config=boringssl-fips`.
- **TLS**: `enforce_rsa_key_usage` now defaults to `true`; the option will be removed in the next release.
- **ext_proc**: the `processing_effect_lib` has moved from `extensions/filters/http/ext_proc` to `extensions/filters/common/processing_effect`.

### Dynamic modules
- New extension points: tracers, TLS certificate validators, custom clusters, load balancing policies, input matchers, upstream HTTP-to-TCP bridge, and listener filters with HTTP callouts.
- Bootstrap extensions gained init-manager integration, drain/shutdown lifecycle hooks, listener-lifecycle callbacks, timer and admin-handler APIs, and metrics support.
- Network filter callbacks for flow-control and connection state (`read_disable`, watermarks, half-close, buffer limits, etc.) and persistent read/write buffers across callbacks.
- Listener-filter socket and TLS introspection (SNI, ALPN, JA3/JA4, SSL SANs/subject) plus `write_to_socket`/`close_socket` callbacks enabling Postgres SSL, MySQL, and similar protocol negotiation.
- Module loading from local file paths and remote HTTP sources (SHA256-verified, cached, with optional NACK-on-cache-miss).
- Process-wide function and shared-data registries for zero-copy cross-module interactions.
- Rust SDK: unified `declare_all_init_functions!` macro for registering any combination of HTTP/network/listener/UDP/bootstrap/access-logger filters, opt-in `CatchUnwind` panic wrapper, multi-logger support.
- Custom metrics on load balancers with configurable `metrics_namespace`, `get_host_health_by_address` fast path, host-membership update callbacks.
- ABI forward-compatibility: modules built against the v1.38 SDK can be loaded by a v1.39 Envoy binary.
- New `envoy_dynamic_module_callback_is_validation_mode` callback and typed filter-state support.

### MCP (Model Context Protocol) and A2A
- MCP router: full method coverage — `resources/list|read|subscribe|unsubscribe`, `resources/templates/list`, `prompts/list|get`, `completion/complete`, `logging/setLevel`, plus `notifications/cancelled` and `notifications/roots/list_changed`.
- SSE streaming support: pass-through for `tools/call` and fan-out aggregation for `tools/list`, `initialize`, `resources/list`, and `prompts/list`.
- MCP filter: HTTP DELETE session termination, relaxed `application/json` Content-Type matching, optional `traceparent`/`tracestate`/baggage propagation from MCP parameters, statistics added to the MCP router, and default metadata namespace changed to `envoy.filters.http.mcp`.
- New **MCP JSON REST Bridge** HTTP filter (work-in-progress) transcoding JSON-RPC to REST, with `tools/call` request transcoding and session negotiation.
- Added parsing support for the **A2A (Agent2Agent)** JSON-RPC protocol.

### HTTP, routing and protocol
- HTTP/2: new `max_header_field_size_kb` to raise the nghttp2 64 KiB per-header limit; applied the nghttp2 **CVE-2026-27135** patch.
- HTTP/1: optional strict chunked-encoding parsing behind a runtime guard.
- Optional **JSON format for the `x-forwarded-client-cert` (XFCC)** header.
- New `envoy.filters.http.sse_to_metadata` filter (extract SSE event values into dynamic metadata, useful for LLM token-usage metrics), with a pluggable `envoy.content_parsers.json` parser.
- New `envoy.filters.http.file_server` filter for serving files directly from disk.
- Refactored `route()`, `clusterInfo()`, and `virtualHost()` to return `OptRef<const T>`, with new `*SharedPtr()` companions.
- Happy Eyeballs now handles interleaving of non-IP addresses.

### TLS, security and authorization
- TLS certificate compression (RFC 8879) extended: brotli added to QUIC, and both brotli and zlib added to TCP TLS.
- `enforce_rsa_key_usage` defaults to `true` on upstream TLS contexts; the option will be removed next release.
- On-demand upstream certificate fetching via SDS using the `envoy.tls.certificate_selectors.on_demand_secret` extension.
- Exposed verified issuer SHA-256 fingerprint and serial number via `%DOWNSTREAM_PEER_ISSUER_FINGERPRINT_256%` / `%DOWNSTREAM_PEER_ISSUER_SERIAL%` and corresponding Lua accessors.
- Per-connection SPIFFE trust-domain selection for multi-tenant deployments; reduced file-watch overhead and support for `watched_directory`.
- **ext_authz** — `shadow_mode` (decision written to filter state without terminating requests), `path_override`, honoring `status_on_error` on 5xx/HTTP-call failures, fix for propagating headers from denied responses.
- **OAuth2** — per-route configuration, `TLS_CLIENT_AUTH` (RFC 8705 mTLS client auth), `OauthExpires` cookie cleared on logout, `oauth2_encrypt_tokens` runtime guard removed (encryption now default, opt-out via `disable_token_encryption`).
- **RBAC** header matcher now validates each header value individually (guarded) to prevent concatenation-based bypasses.
- Query-parameter values added via `query_parameter_mutations` are now URL-encoded to prevent injection.
- **OpenSSL** can now be used as an alternative to the default BoringSSL (build with `--config=openssl` Bazel flag); HTTP/3 (QUIC) is disabled and OpenSSL builds are not covered by the Envoy security policy.

### Observability
- New formatters: `SPAN_ID`, `QUERY_PARAMS`, `UPSTREAM_LOCAL_CLOSE_REASON`, `DOWNSTREAM_LOCAL_CLOSE_REASON`, `UPSTREAM_DETECTED_CLOSE_TYPE`, `DOWNSTREAM_DETECTED_CLOSE_TYPE`, `%UPSTREAM_HOSTS_ATTEMPTED%` and related attempt/connection-ID formatters, `%FILE_CONTENT(...)%`, `%SECRET(name)%`.
- `*_WITHOUT_PORT` address formatters accept an optional `MASK_PREFIX_LEN` to emit CIDR-masked addresses.
- Prometheus admin endpoint supports the **protobuf exposition format** and **Prometheus native histograms**.
- Cluster-level and listener-level stats matchers, plus stats-scope metric-count limits.
- OpenTelemetry stat sink can now export metrics over **HTTP** (OTLP/HTTP) without a collector sidecar.
- Access loggers: stats customization and gauge support in the stats access logger; network filters can register as access loggers; new `asn_org` geoip field; log events on OpenTelemetry spans.

### Routing, load balancing and upstream
- Coalesced load-balancer rebuilds during EDS batch host updates — significant CPU-spike reduction on large clusters.
- Passive degraded-host detection (`detect_degraded_hosts`) via the `x-envoy-degraded` response header.
- Redis Cluster zone-aware routing (`LOCAL_ZONE_AFFINITY` / `LOCAL_ZONE_AFFINITY_REPLICAS_AND_PRIMARY`, Valkey only).
- New `upstream_rq_active_overflow` counter distinguishing active-RQ saturation from pending-queue saturation.
- ODCDS over ADS fix for tcp_proxy; SRDS late-listener init fix; drop_overload now uses cached EDS.
- EDS metadata comparison uses a cached hash for O(1) per-host comparison.
- ORCA weight manager prefers named metrics over application utilization by default.

### Rate limiting
- `is_negative_hits` on `hits_addend` to refund tokens to the budget.
- New `RemoteAddressMatch` rate-limit action (CIDR-based, with inversion and formatter substitution).
- Per-descriptor `x-ratelimit-*` response headers and shadow mode in the local rate limit filter.
- `timeout: 0s` in HTTP ext_authz and HTTP rate-limit filters now means "no timeout", aligning with other Envoy timeouts.

### Memory, resource and connection management
- Replaced the custom timer-based tcmalloc release with tcmalloc's native `ProcessBackgroundActions` / `SetBackgroundReleaseRate`.
- New `MemoryAllocatorManager` fields (`soft_memory_limit_bytes`, `max_per_cpu_cache_size_bytes`, `max_unfreed_memory_bytes`).
- Typed `ShrinkHeapConfig` for the `shrink_heap` overload action.
- **cgroup v2** support in the CPU utilization resource monitor, with automatic v1/v2 detection.
- New `per_connection_buffer_high_watermark_timeout` on listeners and clusters to close connections stuck above the watermark.
- Fixed a resource leak in global connection-limit tracking under load shedding.

### xDS and configuration
- `set_node_on_first_message_only` now supported in Delta-xDS.
- Delta-xDS failover fix for `initial_resource_versions` on reconnect.
- `--mode validate` now creates bootstrap extensions, actually validating their configs.
- CEL expressions that attempt to read response-path data on the request path are automatically re-evaluated when the data becomes available.
- New `HttpResponseLocalReplyMatchInput` matcher input to distinguish local replies from upstream responses.
- New `HickoryDnsResolverConfig` — DNS resolver built on Hickory DNS.

### TCP proxy and PROXY protocol
- New `proxy_protocol_tlv_merge_policy` (`ADD_IF_ABSENT`, `OVERWRITE_BY_TYPE_IF_EXISTS_OR_ADD`, `APPEND_IF_EXISTS_OR_ADD`).
- Option to emit an access-log entry when a connection is accepted.
- `max_early_data_bytes` is now **required** when using non-`IMMEDIATE` `upstream_connect_mode`.

### Other notable changes and fixes
- Router returns `DEADLINE_EXCEEDED` (instead of `UNAVAILABLE`) on router-enforced gRPC timeouts (opt-in).
- Hot restart fixed for listeners with a network-namespace address.
- HTTP/3 client pool fix for early-data requests with async certificate validation.
- Fixes for HTTP/1 zombie-stream FD leaks, internal-redirect hang on buffer overflow, keep-alive header preservation, reset-stream filter-chain safety, idle-timer-before-connected behaviour, and a worker-thread watchdog configuration bug.
- Several ext_proc fixes: two ext_procs in the same chain, CEL message text-format serialization, empty-data-chunk handling.
- Geoip HTTP filter promoted to **stable**.
- Published contrib binaries now carry a `-contrib` version suffix.
