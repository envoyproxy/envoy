The ALTS transport socket's server-side (downstream) handshakes now talk to the ALTS handshaker
service through Envoy's async gRPC client instead of blocking gRPC calls. Previously, each call
blocked the worker thread until the handshaker service responded, for up to 30 seconds, so a slow
or unreachable handshaker service stalled every connection on that worker. A worker now keeps
serving its other connections while a handshake waits, a handshake whose connection closes is
cancelled, and the handshaker service streams are counted in the ``grpc.alts_handshaker.*`` gRPC
client stats. Client-side (upstream) handshakes still make blocking calls. This behavior can be
temporarily reverted by setting runtime guard ``envoy.reloadable_features.alts_async_handshaker``
to ``false``.
