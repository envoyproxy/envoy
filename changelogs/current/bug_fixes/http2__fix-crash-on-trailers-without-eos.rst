Fix: `CVE-2026-73513 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-jjmm-fw8p-crpw>`_

Fixed abnormal process termination when Envoy receives trailers without the END_STREAM flag
over HTTP/2 protocol.
