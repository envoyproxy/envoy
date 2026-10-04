reverse_tunnel: fixed listener draining with ``enable_drain_with_goaway`` so unused tunnels close and active tunnels
immediately reject new HTTP/2 streams while existing streams continue. Initiators also stop creating replacement
tunnels when notified that their listener is draining.
Responders exclude initiators whose remaining tunnels are all draining when selecting a node for new streams,
while retaining those tunnels for existing streams.
