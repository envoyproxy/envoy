QUIC-LB now rejects SDS updates that change the encryption key without changing the active configuration
version, instead of accepting the update while continuing to use the old key. Updates with an identical key
and configuration version are treated as no-ops.
