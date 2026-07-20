Fixed a crash when a UDP or QUIC listener was configured with ``bind_to_port`` set to ``false``.
Such a listener has no bound socket, so applying the listen socket options dereferenced a null I/O
handle while the listener was being created. This configuration is now rejected with a
configuration error, as a UDP listener must be bound to a port to receive datagrams.
