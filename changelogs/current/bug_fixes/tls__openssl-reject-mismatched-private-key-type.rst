Fixed a crash in OpenSSL builds when a TLS certificate was configured with a private key of a
different key type, for example an ECDSA certificate with an RSA key. This could happen during a
non-atomic SDS certificate rotation. The OpenSSL compatibility layer now rejects a private key that
does not match the loaded certificate, as BoringSSL does, so the configuration fails to load
instead of aborting on the ``TLS context should have an active certificate`` assertion.
