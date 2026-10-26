**Summary of changes**:

* Upstream security fixes:
  - [eMudhra intermediate CA compromise](https://emudhra.we-have-a-problem.com/):
    release: updated the `distroless` docker image base to pick up the patched
    `ca-certificates` package (`20250419+deb13u1`), so it no longer trusts the roots of the
    compromised eMudhra intermediate CAs. The distroless image released in 1.39.5 still
    shipped the unpatched bundle.
