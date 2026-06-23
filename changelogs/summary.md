**Summary of changes**:

* Upstream security fixes:
  - [CVE-2026-47205](https://github.com/envoyproxy/envoy/security/advisories/GHSA-mvh9-767w-x47j):Authz per route crash
  - [CVE-2026-47207](https://github.com/envoyproxy/envoy/security/advisories/GHSA-68cv-hq5f-g6xv): ext_proc response in one gRPC message
  - [CVE-2026-47221](https://github.com/envoyproxy/envoy/security/advisories/GHSA-rcff-gw58-pjpr): router internal redirects crash
  - [CVE-2026-47775](https://github.com/envoyproxy/envoy/security/advisories/GHSA-396h-jpq4-vc7p): OAuth2 code verifier padding oracle
  - [CVE-2026-48044](https://github.com/envoyproxy/envoy/security/advisories/GHSA-m3p9-47wh-88wg): zstd RLE zip bomb
  - [CVE-2026-47204](https://github.com/envoyproxy/envoy/security/advisories/GHSA-3jxh-8p6x-7pf6): grpc_stats filter segfault on Connect protocol requests to direct_response routes
  - [CVE-2026-47692](https://github.com/envoyproxy/envoy/security/advisories/GHSA-wh36-hm39-mm3r): PROXY Protocol v2 header generator emits "skipped" TLVs, causing 65 KB attacker-controlled spillover into the upstream application stream
  - [CVE-2026-47778](https://github.com/envoyproxy/envoy/security/advisories/GHSA-f8x4-rw5x-f3r7): Embedded NUL in TLS SAN Truncation, Auth Bypass
  - [CVE-2026-48042](https://github.com/envoyproxy/envoy/security/advisories/GHSA-f24p-rxw2-g6pv): Stack overflow in destructor of highly nested JSON
  - [CVE-2026-48090](https://github.com/envoyproxy/envoy/security/advisories/GHSA-3cj2-c63f-q26f): OAuth2 filter late async token completion after stream teardown results in UAF/crash risk
  - [CVE-2026-48497](https://github.com/envoyproxy/envoy/security/advisories/GHSA-j6g2-wf95-q66q): Abnormal process termination in DNS UDP filter
  - [CVE-2026-48743](https://github.com/envoyproxy/envoy/security/advisories/GHSA-8phg-2h2q-jgxf): HTTP/3 to HTTP/1 request smuggling via headers-only request with nonzero Content-Length
  - [CVE-2026-48706](https://github.com/envoyproxy/envoy/security/advisories/GHSA-7q3f-gwg7-j8g4): Envoy Heap Buffer Overflow in TcpStatsdSink
  - [GHSA-p7c7-7c47-pwch](https://github.com/envoyproxy/envoy/security/advisories/GHSA-p7c7-7c47-pwch): Denial-of-Service Attack Against the HTTP/3 Stack via QPACK Blocked Decoding

* Upstream security fixes:
  - CVE-2026-47261: wasm: bumped `com_github_wasmtime` to resolve CVE-2026-47261.

* Behavior changes:
  - build: disabled the contrib extension `envoy.network.connection_balance.dlb` (Intel DLB connection balancer) at the Bazel layer for all builds and platforms due to a breakage at the source archive. See https://github.com/envoyproxy/envoy/issues/45491 for local workarounds.
