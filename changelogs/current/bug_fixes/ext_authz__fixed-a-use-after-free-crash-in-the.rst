Fix: `CVE-2026-47205 <https://github.com/envoyproxy/envoy/security/advisories/GHSA-mvh9-767w-x47j>`_

Fixed a use-after-free crash in the ext_authz filter when per-route service overrides are active
and the downstream connection resets during an in-flight authorization check.
