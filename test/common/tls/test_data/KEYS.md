# Test-only private keys

Every `*_key.pem` (and `*key.pem`) file in this directory is a **deliberately
public, test-only** private key. They are checked into the repository so that
the certificate fixtures built by
[`@envoy_toolshed//certs:gen`](https://github.com/envoyproxy/toolshed/blob/main/bazel/certs/README.md)
are reproducible: the
generator only creates certificates, never keys, so the same spec always
produces the same public key material.

These keys:

* have never protected anything,
* are known to anybody with a copy of this repository,
* must never be used outside Envoy's test suite.

Secret scanners will flag them. That is expected.

`password_protected_key.pem` is encrypted with the password in
`password_protected_password.txt` (`p4ssw0rd`), which is equally public.
