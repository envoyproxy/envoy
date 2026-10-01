**Summary of changes**:

* Security fixes:
  - [CVE-2026-35189](https://github.com/google/boringssl/blob/main/docs/advisories/2026-09-29.md): tls: patched BoringSSL to fix excessive memory allocation when parsing certificates with `nameRelativeToCRLIssuer` CRL Distribution Points, which could be exploited for remote denial of service during TLS handshakes. Note that the FIPS build is not patched.

* Build/packaging:
  - Removed Debian bullseye (11) packaging, as bullseye is end-of-life and its repositories are no longer available on the main Debian mirrors.
