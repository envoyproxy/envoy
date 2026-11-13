Fixed certificate selection when a listener has both RSA and ECDSA certificates and allows TLS 1.3.
A TLS 1.2 client that sent the ``supported_versions`` extension without TLS 1.3 was treated as a
TLS 1.3 client, so it was offered the ECDSA certificate based on ``signature_algorithms`` alone even
when it offered no ECDSA cipher suite, and the handshake failed. Envoy now only uses
``signature_algorithms`` alone when ``supported_versions`` lists TLS 1.3, and otherwise also requires
an ECDSA cipher suite. This affects, for example, Java clients restricted to TLS 1.2 RSA cipher suites.
This behavioral change can be reverted by setting the runtime guard
``envoy.reloadable_features.tls_ecdsa_selection_check_supported_versions`` to ``false``.
