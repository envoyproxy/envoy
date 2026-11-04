A TCP upstream that half closes its connection, such as the origin of a terminated CONNECT tunnel,
no longer causes an HTTP/2 or HTTP/3 downstream stream to be reset. The half close is propagated
downstream as an end of stream, and the stream is closed once the downstream client half closes its
own side. Previously the stream was reset, which discarded any response data that had not yet been
written to the downstream connection. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.tcp_tunnel_allow_upstream_half_close`` to ``false``.
