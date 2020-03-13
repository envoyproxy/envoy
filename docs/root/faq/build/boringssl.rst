Why does Envoy use BoringSSL?
=============================

`BoringSSL <https://boringssl.googlesource.com/boringssl/>`_ is a slimmed down TLS implementation
maintained by Google. Getting TLS right is very, very hard. Envoy has chosen to align with
BoringSSL so as to obtain access to the world class experts that Google employs to work on this
code base. In short: if BoringSSL is good enough for Google's production systems it is good enough
for Envoy and the project will not offer first class support for any other TLS implementation.
