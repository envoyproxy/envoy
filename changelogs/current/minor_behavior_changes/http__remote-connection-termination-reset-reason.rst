Added a new ``Http::StreamResetReason::RemoteConnectionTermination`` reset reason. The HTTP
``CodecClient`` and the QUIC remote-reset path now emit it (instead of ``ConnectionTermination``)
when an established upstream connection is closed by the remote peer, so that downstream
consumers (notably HTTP CONNECT/upgrade tunneling in ``tcp_proxy``) can distinguish a
remote-initiated termination from a local one. Routing, retry classification, and the ``UC``
response flag are unchanged. The emission of the new reason can be temporarily reverted by
setting runtime guard ``envoy.reloadable_features.emit_remote_connection_termination`` to
``false``. For HTTP CONNECT/upgrade tunnels (both the legacy ``HttpUpstream`` and the
upstream-HTTP-filters ``CombinedUpstream`` path), this new reason maps to ``RemoteClose`` with
``DetectedCloseType::RemoteReset`` so a TCP RST is propagated to the downstream client,
matching the existing remote-reset handling for other remote-originated reasons; that mapping
can be temporarily reverted by setting runtime guard
``envoy.reloadable_features.map_http_stream_reset_to_tcp_rst`` to ``false``.
