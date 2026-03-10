**Summary of changes**:

* Security fixes:
  - [CVE-2026-26330](https://github.com/envoyproxy/envoy/security/advisories/GHSA-c23c-rp3m-vpg3): ratelimit: fix a bug where response phase limit may result in crash
  - [CVE-2026-26308](https://github.com/envoyproxy/envoy/security/advisories/GHSA-ghc4-35x6-crw5): fix multivalue header bypass in rbac
  - [CVE-2026-26310](https://github.com/envoyproxy/envoy/security/advisories/GHSA-3cw6-2j68-868p): network: fix crash in getAddressWithPort() when called with a scoped IPv6 address
  - [CVE-2026-26309](https://github.com/envoyproxy/envoy/security/advisories/GHSA-56cj-wgg3-x943): json: fixed an off-by-one write that could corrupted the string null terminator
  - [CVE-2026-26311](https://github.com/envoyproxy/envoy/security/advisories/GHSA-84xm-r438-86px): http: ensure decode* methods are blocked after a downstream reset

* Bug fixes:
  - oauth2: Fixed OAuth2 refresh requests so host rewriting no longer overrides the original `Host` header value.
  - ext_proc: Fixed a bug to support two ext_proc filters configured in the chain.
  - ext_proc: Fixed message-valued CEL attribute serialization to use protobuf text format instead of debug string output, restoring compatibility with protobuf 30+.
  - ext_authz: Fixed headers from denied authorization responses (non-200) not being properly propagated to the client.
  - ext_authz: Fixed the HTTP ext_authz client to respect `status_on_error` configuration when the authorization server returns a 5xx error or when HTTP call failures occur.
  - access_log: Fixed a crash on listener removal with a process-level access log rate limiter.

* Other changes:
  - release: Published contrib binaries now include the `-contrib` suffix in their version string and fixed distroless-contrib images.
  - dynamic modules: Introduced extended ABI forward compatibility mechanism for dynamic modules.

* Dependency updates:
  - Migrated googleurl source to GitHub (`google/gurl`).
  - Updated Kafka test binary to 3.9.2.
  - Updated Docker base images.
