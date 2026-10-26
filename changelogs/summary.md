**Summary of changes**:

* Upstream security fixes:
  - [eMudhra intermediate CA compromise](https://emudhra.we-have-a-problem.com/):
    release: the `distroless` docker image now uses the patched `ca-certificates` package
    (`20250419~deb12u2`) from Debian bookworm, so it no longer trusts the roots of the
    compromised eMudhra intermediate CAs. The distroless image released in 1.38.8 still
    shipped the unpatched bundle.
