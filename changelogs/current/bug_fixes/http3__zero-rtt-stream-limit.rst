Fixed an HTTP/3 upstream connection resuming with 0-RTT sending early data streams beyond the stream
limit cached from the previous connection, which failed a ``QUICHE_BUG``. The connection
pool now takes the cached limit as the connection's stream capacity. This behavior can be reverted by
setting the runtime guard ``envoy.reloadable_features.quic_zero_rtt_stream_limit`` to ``false``.
