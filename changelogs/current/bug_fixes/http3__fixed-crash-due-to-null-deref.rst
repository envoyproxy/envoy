Fix: `CVE-2026-48521 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-5vff-j9p4-38j3>`_

Fixed abnormal process termination when Envoy is configured to automatically select a protocol with upstream
server based on ALPN and the server uses HTTP/3.
