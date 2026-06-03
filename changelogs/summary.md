**Summary of changes**:

* Security fixes:
  - [CVE-2026-47774](https://github.com/envoyproxy/envoy/security/advisories/GHSA-22m2-hvr2-xqc8): http2: HTTP/2 streams are now reset if they violate the configured maximum header list size. Uncompressed cookies now count towards ``mutable_max_request_headers_kb`` and ``max_headers_count`` limits, protecting against an HPACK cookie-bomb that could cause excessive memory usage. This can be reverted with ``envoy.reloadable_features.http2_include_cookies_in_limits``.
  - oauth2: fixed a timing side-channel in HMAC verification that could leak HMAC secret validity.
  - oauth2: fixed a crash where AES-CBC decryption of token cookies could spuriously succeed (~1/256) on a secret mismatch, tripping a ``HeaderString`` validation assert.

* Bug fixes:
  - dynamic_modules: fixed a crash when a stream was already above the downstream write-buffer high watermark at filter-chain construction time.

* Minor behavior changes:
  - router: the upstream transport failure reason is no longer included in the downstream response body (still in access logs via ``%UPSTREAM_TRANSPORT_FAILURE_REASON%``). Revert with ``envoy.reloadable_features.hide_transport_failure_re*
