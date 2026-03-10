**Summary of changes**:

* Security fixes:
  - [CVE-2026-26330](https://github.com/envoyproxy/envoy/security/advisories/GHSA-c23c-rp3m-vpg3): ratelimit: fix a bug where response phase limit may result in crash
  - [CVE-2026-26308](https://github.com/envoyproxy/envoy/security/advisories/GHSA-ghc4-35x6-crw5): fix multivalue header bypass in rbac
  - [CVE-2026-26310](https://github.com/envoyproxy/envoy/security/advisories/GHSA-3cw6-2j68-868p): network: fix crash in getAddressWithPort() when called with a scoped IPv6 address
  - [CVE-2026-26309](https://github.com/envoyproxy/envoy/security/advisories/GHSA-56cj-wgg3-x943): json: fixed an off-by-one write that could corrupted the string null terminator
  - [CVE-2026-26311](https://github.com/envoyproxy/envoy/security/advisories/GHSA-84xm-r438-86px): http: ensure decode* methods are blocked after a downstream reset

* Bug fix:
  - Fixed OAuth2 refresh requests so host rewriting no longer overrides the original Host value.

* Dependency updates:
  - Migrated googleurl source to GitHub (`google/gurl`).
  - Updated Kafka test binary to 3.9.2.
  - Updated Docker base images.
