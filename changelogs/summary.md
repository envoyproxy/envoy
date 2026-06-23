**Summary of changes**:

* Security fixes:
  - CVE-2026-47261: wasm: bumped ``com_github_wasmtime`` to resolve CVE-2026-47261.

* Behavior changes:
  - build: disabled the contrib extension ``envoy.network.connection_balance.dlb`` (Intel DLB connection balancer) at the Bazel layer for all builds and platforms due to a breakage at the source archive. See https://github.com/envoyproxy/envoy/issues/45491 for local workarounds.

* Minor behavior changes:
  - tls: runtime guard ``envoy.reloadable_features.tls_certificate_compression_brotli`` is now disabled by default. When disabled, QUIC retains zlib-only certificate compression and TCP TLS performs no certificate compression. It can be re-enabled by setting the runtime guard to ``true``.
