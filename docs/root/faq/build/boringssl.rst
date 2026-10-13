Why does Envoy use BoringSSL as the default TLS library?
========================================================

`BoringSSL <https://boringssl.googlesource.com/boringssl/>`_ is a slimmed down TLS implementation
maintained by Google. Getting TLS right is very, very hard. Envoy has chosen to align with
BoringSSL so as to obtain access to the world class experts that Google employs to work on this
code base. In short: if BoringSSL is good enough for Google's production systems it is good enough
for Envoy.

Envoy can also be built with `OpenSSL <https://openssl.org>`_ as an alternative TLS provider,
using the ``--config=openssl`` Bazel option. This enables Envoy usage in environments that require
or prefer OpenSSL (e.g., FIPS compliance through OpenSSL, organizational policies, or specific
platform requirements). OpenSSL builds are not currently covered by the
:repo:`Envoy security policy <SECURITY.md>`.
