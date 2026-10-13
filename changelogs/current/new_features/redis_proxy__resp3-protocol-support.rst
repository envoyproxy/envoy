Added RESP3 protocol support to the Redis proxy via the new
:ref:`protocol_version <envoy_v3_api_field_extensions.filters.network.redis_proxy.v3.RedisProxy.protocol_version>`
listener setting (default ``RESP2`` keeps the existing behavior). When set to ``RESP3``, downstream
clients negotiate with an explicit ``HELLO 3`` handshake — data commands sent beforehand are rejected
with ``-NOPROTO`` and counted by the new ``downstream_rq_noproto`` counter — and every new upstream
connection performs a ``HELLO 3`` handshake (combined with ``AUTH`` or AWS IAM credentials when
configured, followed by ``READONLY`` where applicable) before serving traffic; requests issued during
the handshake are held and replayed in order, and negotiation failures are tracked by the new
per-cluster ``upstream_resp3_hello_failure`` counter. Independently of the setting, the proxy now
answers ``HELLO``, ``CLIENT SETNAME`` and ``CLIENT SETINFO`` locally so that modern Redis clients can
complete their connection setup, and the codec understands all RESP3 frame types, down-converting
them for RESP2 connections.
