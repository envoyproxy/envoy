Fix: `CVE-2026-47221 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-rcff-gw58-pjpr>`_

Fixed an issue when handling HTTP 303 internal redirects for body-less requests. The redirect handling
code attempted to drain a request body buffer that was never allocated, causing a segmentation fault.
