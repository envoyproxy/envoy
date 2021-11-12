Does Envoy on Windows support FIPS?
===================================

Envoy uses `BoringSSL <https://boringssl.googlesource.com/boringssl/>`_  which is a slimmed down TLS implementation. At the time of writing,
BoringSSL does not support a FIPS mode on Windows. As a result, Envoy does not offer support for FIPS on Windows.

If FIPS is a requirement for you, please reach out to the contributors.
