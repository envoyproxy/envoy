reverse_tunnel: fixed listener draining so initiators stop pending handshakes and replacement dials when
the listener begins draining, without stopping replacement tunnels during in-place filter-chain updates.
With ``enable_drain_with_goaway``, unused tunnels close while accepted tunnels follow the configured drain schedule.
When the responder's reverse connection cluster enables ``ReverseTunnelUpstreamCodecOptions.enable_drain_with_goaway``,
responders exclude initiators whose remaining tunnels have received GOAWAY when selecting a node for new streams,
while retaining those tunnels for existing streams. Node selection prefers idle sockets; when none are available,
it falls back to round-robin among nodes with a tunnel that has not received GOAWAY. A request with no eligible
initiator fails with HTTP 503 (no healthy upstream). Early drain METADATA still only notifies the reporter.
