When independent upstream half close is enabled with
``envoy.reloadable_features.allow_multiplexed_upstream_half_close``, a TCP tunnel upstream that half
closes its connection no longer causes the stream to be reset. The half close is propagated
downstream as an end of stream instead, and the stream is closed once the downstream client half
closes its own side. Previously the TCP upstream force closed the stream in this case, which
discarded any response data that had not yet been written to the downstream connection. This change
can be reverted by setting the runtime guard
``envoy.reloadable_features.tcp_tunnel_allow_upstream_half_close`` to ``false``.
