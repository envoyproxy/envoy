The downstream reverse-tunnel initiator
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) now accepts
:ref:`maintain_interval <envoy_v3_api_field_extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface.maintain_interval>`
to control how often each host is re-checked and missing tunnels are dialed.
Unset keeps the historical 10s default. The existing 15% upward jitter still
applies. The minimum allowed value is 100ms.
