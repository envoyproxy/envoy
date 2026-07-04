Added the ``connection.peer_certificate_valid`` CEL attribute, a bool indicating whether the
downstream peer certificate was presented and validated. In optional mTLS setups using
``trust_chain_verification: ACCEPT_UNTRUSTED``, a certificate may be presented without being
validated.
