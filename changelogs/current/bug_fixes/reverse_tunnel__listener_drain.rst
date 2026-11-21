reverse_tunnel: added listener draining behavior behind the runtime guard
``envoy.reloadable_features.reverse_tunnel_listener_drain``, which defaults to false. When enabled,
initiators stop pending handshakes and replacement dials when the whole listener begins draining,
independently of ``enable_drain_with_goaway``. With that option enabled, admin drains close unused tunnels,
while in-place filter-chain updates request replacements and preserve HCM's existing idle drain grace.
Full listener replacement and hot restart also retain unused tunnels' existing drain grace.
With the guard enabled, the drain-aware HCM's legacy drain decision follows the listener direction,
so an ``INBOUND`` listener participates in ``drain_listeners?inboundonly``.
When the responder also enables ``ReverseTunnelUpstreamCodecOptions.enable_drain_with_goaway``,
node selection excludes initiators whose remaining tunnels have received GOAWAY. Existing streams
continue on those tunnels. Requests with no eligible initiator fail with HTTP 503 and response flag
``UH`` (no healthy upstream), instead of the previous fallback connection failure with ``UF``.
These requests increment ``cluster.<name>.upstream_rq_all_tunnels_draining`` instead of
``fallback_no_reverse_socket``. Early drain METADATA still only notifies the reporter.
