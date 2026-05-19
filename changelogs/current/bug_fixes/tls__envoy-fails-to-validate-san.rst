Fix: [CVE-2026-47778](https://github.com/envoyproxy/envoy/security/advisories/GHSA-f8x4-rw5x-f3r7)

Fixes an issue where Envoy could fail to validate the Subject Alternative Name (SAN) of a peer
certificate if the SAN contained an embedded NUL byte. Previously, the SAN parsing was vulnerable
to NUL byte truncation in some configurations, potentially leading to incorrect trust decisions.
