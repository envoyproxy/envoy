Fix `CVE-2026-48497 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-j6g2-wf95-q66q>`_

Fix sanity checking of the query name length to avoid abnormal process termination. Use ``ENVOY_BUG``
in case sanity check fails.
