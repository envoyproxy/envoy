## Summary of changes

### Breaking changes

- **build**: Envoy has switched from `WORKSPACE` to **bzlmod**. The `WORKSPACE` file is removed; Envoy, the API and the docs are published as Bazel modules (`envoy`, `envoy_api`, `envoy-docs`). Downstreams must migrate to `MODULE.bazel` and **must** add Envoy's custom registry (`https://raw.githubusercontent.com/envoyproxy/bazel-registry`) alongside the BCR — many of Envoy's dependencies and patched modules are only published there.
- **build**: the hermetic LLVM/Clang toolchain is bumped from 18 to 22. `--config=clang` and `clang-local` are removed — Clang/libc++ is now the default and needs no flag; GCC remains via `--config=gcc`. The `--@envoy//bazel:libc++`/`libstdc++` flags and `force_libcpp` define are gone. Downstreams needing a host LLVM toolchain should follow `bazel/tests/codeql`.
- **build**: the Wasm runtime is now selected with `--@proxy-wasm-cpp-host//bazel:engine=<engine>`; the old `--define wasm=`/`--define engine=` flags fail the build.
- **tls**: AWS-LC is removed as a selectable SSL library (including `--config=aws-lc-fips`). ppc64le builds should use `--config=openssl`.
- **mcp_router**: downstream request headers are no longer forwarded to backends by default — notably `authorization`. Use the new per-backend `header_forwarding` (`forward_all: true` restores the old behavior).
- **dynamic modules (Rust SDK)**: `HttpFilter` hooks now take `&self` rather than `&mut self`; per-stream mutable state must move behind interior mutability. This closes a use-after-free on synchronous filter-chain teardown.

### Major changes

- **HTTP/3 and QUIC are now stable**, including upstream HTTP/3 and all QUIC extensions. QUIC listeners gain client-certificate (mTLS) support, upstream QUIC gains client certificates, and P-384/P-521 ECDSA curves are supported.
- **Post-quantum by default**: `X25519MLKEM768` hybrid key exchange is added to the default ECDH curves for upstream and downstream TLS, including FIPS builds (revertible via `envoy.reloadable_features.pqc_default_ecdh_curves`).
- **Routing becomes extensible after match**: new `route_specifiers` extension point post-processes (refines, drops, or synthesizes) the matched route at route-config, virtual-host and route level. A priority-group cluster specifier and a dynamic-module route specifier build on it.
- **Load balancing**: new `per_worker_subset` policy decouples per-worker pool size from cluster size; new `load_aware_locality` policy picks localities by ORCA-derived headroom; coalesced LB rebuilds on EDS batch updates are now on by default.
- **Container-aware CPU detection is actually on by default** — the 1.37 behaviour was only active with `--cpuset-threads`; it now applies whenever `--concurrency` is unset.
- **Drain is event-driven**: drain-close decisions are pushed to connections when the drain starts rather than polled per response, which also fixes inbound-only drain for the Mongo/Redis/Thrift/generic proxies.
- **Hot restart for UDP**: established UDP proxy sessions are kept by the parent while unknown-session datagrams are forwarded to the child, so connectionless listeners no longer go dark during drain.

### AI / MCP

- The AI Protocol Manager filter (alpha) gains response-side LLM token-usage extraction for OpenAI, Anthropic and Gemini dialects (SSE and JSON), published as typed dynamic metadata (`envoy.data.ai.v3.TokenUsage`), plus request-info, schema-validation and transcoding sub-filters and body re-serialization.
- MCP filter: per-route config, protocol-version enforcement, required client capabilities, new method groups, no-op mode and early termination when routable. The JSON/REST bridge adds server discovery, header/protocol-version validation and result types, and now prevents path traversal via template variables.

### Dynamic modules

- New extension points: cluster specifier, route specifier, header formatter, early header mutation, TLS handshaker, transport-socket relay, config validator, and downstream STARTTLS.
- Dynamic-module clusters can now delegate host selection to Envoy's built-in LBs (`ROUND_ROBIN`, `LEAST_REQUEST`, `RANDOM`, `RING_HASH`, `MAGLEV`).
- Broad hardening: a large batch of lifetime/view fixes across HTTP, network, listener and cluster-LB filter state, deferred HTTP filter destroy, panic/exception barriers in the Go and C++ SDKs, re-entrancy fixes for callouts, and `RTLD_NOW` loading with logged load failures.
- Typed metadata get/set, runtime getters, tracing spans across hooks, stream timing, upstream attempt/peer-address accessors, and Go SDK stats/log-level getters.

### Security and auth

- Carries forward the 1.39.x security fixes: URL normalization of path parameters (CVE-2026-73511/73551), HTTP/2 trailers-without-END_STREAM and dropped-`Host` accounting (CVE-2026-73513/73550), HTTP/3 UAF (CVE-2026-73512), ext_authz pathless-request crash and UAF (CVE-2026-73547/50572), generic-upgrade body pipelining (CVE-2026-73548), safe_regex Latin1 mode (CVE-2026-73552), RBAC `ignore_path_parameters` (CVE-2026-73553), admin stat-name sanitization (CVE-2026-73546), and the oauth2/api_key_auth CONNECT crashes (GHSA-8vc2-jrm4-835w, GHSA-47vj-9r25-wv5j).
- TLS: per-certificate TLS params, multiple upstream certificates with a certificate selector, SPIFFE upstream SAN validation, SDS file polling, and upstream session cache scoped by SNI.
- OAuth2 gains assertion audience/key-id and TLS client auth for the credential injector; JWT auth gains claim paths and a JWT-claim rate-limit descriptor; GCP auth gains scopes, audiences and IAM access tokens; ext_authz gains shadow mode on the network filter and route-cache-clearing headers.

### Observability

- Access logs: downstream handshake time points and connection RTT, listener name and route-resolution-time formatters, CEL-expression formatter config, and nanosecond histogram units.
- OpenTelemetry: resource detectors, custom exporters, instrumentation scope, and typed custom tags; ext_authz and ext_proc emit client spans.
- Stats: per-cluster/listener stats matchers, explicit tags, and a fix for Prometheus native-histogram bucket indices being off by one.

### Other notable changes

- HTTP/1 now accepts the RFC 10008 `QUERY` method (revertible); `QUERY` without `Content-Type` is rejected on all protocols.
- Redis proxy: RESP3 protocol support, `CLUSTER SHARDS`, stricter RESP decoding, and several use-after-free fixes.
- Reverse tunnels: JWT handshake auth, maintain interval, fail-closed identifier validation, and a batch of handshake lifecycle fixes.
- Watchdog backtrace action, UDP health checker, multi-health-checker, TCP proxy load-shed points, and a body-size-limit HTTP filter.
- Fixed a family of races where route-level configuration (AWS signing, composite, filter chain, local ratelimit, dynamic modules, SDS secrets) could be destroyed on a worker thread after an RDS update.

### Deprecations

- `envoy.extensions.config.validators` factories keyed by type URL; dynamic-module log and timing-info callbacks; legacy HTTP filter factory base classes; reverse-tunnel `auto_close_connections`; Wasm plugin-level capability restriction.
