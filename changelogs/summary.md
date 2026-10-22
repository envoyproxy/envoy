**Summary of changes**:

* Upstream security fixes:
  - [eMudhra intermediate CA compromise](https://emudhra.we-have-a-problem.com/):
    the private keys of several eMudhra intermediate CA certificates were compromised.
    - release: updated the Envoy docker images so they no longer trust the compromised
      eMudhra CAs.
    - mobile: removed the `AAA Certificate Services` and `emSign ECC Root CA - G3` roots
      from the root certificate bundle built into Envoy Mobile. Servers whose certificate
      chains only lead to these roots are no longer trusted by Envoy Mobile when it uses
      its built-in bundle.
    - dns_resolver: updated the `webpki-roots` and `rustls-webpki` crates used by the
      built-in Hickory DNS resolver, so DNS-over-TLS and DNS-over-HTTPS upstreams no longer
      trust the compromised eMudhra intermediate CAs.
