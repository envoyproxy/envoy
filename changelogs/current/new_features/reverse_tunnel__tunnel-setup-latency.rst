Added
:ref:`max_tunnel_setup_time <envoy_v3_api_field_extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface.max_tunnel_setup_time>`
to the downstream reverse-tunnel initiator
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``). The initiator now records how long
it takes for tunnels to a host to reach the configured per-host connection count via the
``tunnel_setup_time`` histogram. If that budget is exceeded, ``tunnel_setup_time_exceeded`` is
incremented and a late completion is not recorded as a setup-time sample. Connection attempts
continue after the deadline; the setting is stats-only and defaults to 30s.
