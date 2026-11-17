reverse_tunnel: fixed listener draining so initiators stop pending handshakes and replacement dials when
the listener begins draining, without stopping replacement tunnels during in-place filter-chain updates.
With ``enable_drain_with_goaway``, unused tunnels close while accepted tunnels follow the configured drain schedule.
Responders exclude initiators whose remaining tunnels have received GOAWAY when selecting a node for new streams,
while retaining those tunnels for existing streams. Early drain METADATA still only notifies the reporter.
