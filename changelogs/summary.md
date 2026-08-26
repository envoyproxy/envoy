**Summary of changes**:

* Security fixes:
  - [CVE-2026-73511](https://github.com/envoyproxy/envoy/security/advisories/GHSA-m745-gh6x-349x): url normalization: strip path parameters from individual path segments per RFC 3986 section 3.3. Revert with `envoy.reloadable_features.strip_path_parameters_per_segment`.
  - [CVE-2026-73512](https://github.com/envoyproxy/envoy/security/advisories/GHSA-r6j2-mrm5-72mg): http3: UAF on a specifically timed sequence of HTTP/3 frames.
  - [CVE-2026-73513](https://github.com/envoyproxy/envoy/security/advisories/GHSA-jjmm-fw8p-crpw): http2: abnormal process termination on trailers received without the END_STREAM flag.
  - [CVE-2026-73546](https://github.com/envoyproxy/envoy/security/advisories/GHSA-pv9h-4fxf-7vrg): admin: sanitize stat names before converting them to HTML. Guarded by `envoy.reloadable_features.sanitize_html_stats_names`.
  - [CVE-2026-73547](https://github.com/envoyproxy/envoy/security/advisories/GHSA-87ph-jqwm-pg6r): ext_authz: abnormal process termination on requests without a URI path (i.e. CONNECT).
  - [CVE-2026-73548](https://github.com/envoyproxy/envoy/security/advisories/GHSA-3vhp-c83q-jqc2): http: payload sent before a generic HTTP upgrade was accepted could be interpreted as a pipelined HTTP/1 request and poison a shared upstream connection. Revert with `envoy.reloadable_features.http_pause_generic_upgrade_request_body`.
  - [CVE-2026-73549](https://github.com/envoyproxy/envoy/security/advisories/GHSA-jp5f-qr64-c9vw): quic: crash handling scoped IPv6 addresses in QUIC client connections and Original Dst clusters.
  - [CVE-2026-73550](https://github.com/envoyproxy/envoy/security/advisories/GHSA-qgf6-qvhw-4hvh): http2: dropped `Host` headers now count towards request header map size and count limits. Revert with `envoy.reloadable_features.http2_track_size_of_dropped_host_header`.
  - [CVE-2026-73551](https://github.com/envoyproxy/envoy/security/advisories/GHSA-2w8w-rfw7-8gg4): url normalization: strip path parameters from dot and dotdot segments (`/.;`, `/..;`) so canonicalization interprets them correctly. Applies only when `normalize_path` is enabled; revert with `envoy.reloadable_features.strip_dotdot_segments_with_parameters`.
  - [CVE-2026-73552](https://github.com/envoyproxy/envoy/security/advisories/GHSA-23xh-2qxr-3xv8): safe_regex: switch charset mode from UTF-8 to Latin1, as HTTP headers are not UTF-8 encoded. Revert with `envoy.reloadable_features.re2_use_latin1_mode`.
  - [CVE-2026-73553](https://github.com/envoyproxy/envoy/security/advisories/GHSA-77x5-xqjg-hprq): rbac: RBAC path matching now respects the route's `ignore_path_parameters_in_path_matching`, preventing authz bypass via appended path parameters. Revert with `envoy.reloadable_features.rbac_respect_ignore_path_parameters`.
  - [CVE-2026-50572](https://github.com/envoyproxy/envoy/security/advisories/GHSA-q8wp-gf7q-m8cv): ext_authz: UAF when ext_authz over HTTP causes a request to be rejected.
  - [CVE-2026-48521](https://github.com/envoyproxy/envoy/security/advisories/GHSA-5vff-j9p4-38j3): http3: abnormal process termination when upstream protocol is selected via ALPN and the server uses HTTP/3.

* Bug fixes:
  - http: fixed a filter manager bug where a body frame moved into the filter-manager buffer via `addDecodedData()`/`addEncodedData()` immediately before returning `Continue` was silently dropped, corrupting large streamed bodies. Revert with `envoy.reloadable_features.filter_manager_forward_added_data_on_continue`.
  - ext_proc: fixed multiple lifetime bugs in the ext_proc filter and the underlying gRPC async client that could lead to use-after-free or double delivery of callbacks.
  - router: fixed a lifetime bug in dynamic forward proxy async host selection when the cluster is removed while lookup is still pending.
  - tls: fixed a memory leak in the OpenSSL compatibility layer where `SSL_get0_peer_certificates()` leaked an `X509` refcount per call, preventing certificates from being freed on connection close.
  - tls: fixed a bug where OpenSSL used glibc's allocator instead of tcmalloc, operating on a separate heap and making OpenSSL allocations invisible to tcmalloc heap profiling.
