Fix: `CVE-2026-48743 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-8phg-2h2q-jgxf>`_.

Validate HTTP/3 headers-only request and response content-length, and reset stream if inconsistent.

The change is guarded by runtime guard ``envoy.reloadable_features.quic_validate_headers_only_content_length``.
