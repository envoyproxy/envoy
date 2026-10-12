Fixed upstream HTTP/3 (QUIC) connections failing the TLS handshake with ``certificate unknown`` when
the upstream TLS context sets ``trust_chain_verification: ACCEPT_UNTRUSTED`` or has no validation
context, and the server certificate does not match the SNI or no SNI is set. The leaf hostname check
is now skipped in these cases, matching TLS over TCP.
