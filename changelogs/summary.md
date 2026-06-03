**Summary of changes**:

* Security fixes:
  - [CVE-2026-47774](https://github.com/envoyproxy/envoy/security/advisories/GHSA-22m2-hvr2-xqc8): http2: HTTP/2 streams are now reset if they violate the configured maximum header list size. Uncompressed cookies now count towards ``mutable_max_request_headers_kb`` and ``max_headers_count`` limits, protecting against an HPACK cookie-bomb that could cause excessive memory usage. This can be reverted with ``envoy.reloadable_features.http2_include_cookies_in_limits``.
  - oauth2: fixed a timing side-channel in HMAC verification that could leak HMAC secret validity.
  - oauth2: fixed a crash where AES-CBC decryption of token cookies could spuriously succeed (~1/256) on a secret mismatch, tripping a ``HeaderString`` validation assert.
  - CVE-2026-27135: http2: applied nghttp2 CVE-2026-27135 patch.

* Bug fixes:
  - load_report: fixed a shutdown race with ADS stream by introducing proper gRPC stream cleanup.
