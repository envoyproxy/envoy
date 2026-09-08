Fixed a bug in the reverse tunnel downstream socket interface
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) where handshake
``additional_headers`` values that use a substitution formatter (such as ``%FILE_CONTENT%``, or
secret/SDS-backed formatters) were sent as the raw, unsubstituted template on every reverse
connection. The reverse connection listen socket snapshots the handshake formatters when it is
created, which can happen before ``onServerInitialized()`` builds them; that null snapshot is then
reused for every re-dial, so the handshake fell back to emitting the literal ``additional_headers``
value. The handshake headers are now resolved from the live bootstrap extension when the request is
assembled, so post-initialization dials substitute the value correctly.
