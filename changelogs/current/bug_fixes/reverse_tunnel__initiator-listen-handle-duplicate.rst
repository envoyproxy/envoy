Fixed reverse tunnel initiator listener handling in the :ref:`downstream reverse tunnel socket
interface
<envoy_v3_api_msg_extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface>`.
The listen handle now returns a fresh, unstarted handle from ``duplicate()`` rather than a raw file
descriptor dup, so every worker under ``reuse_port`` dials and an LDS update of the reverse
connection listener no longer removes the listener for the drain window. Tunnels still queued for
``accept()`` are closed on the worker that owns them when the listener stops, and a tunnel handle
disposed without an explicit close, for example after a listener filter timeout, now releases its
tunnel key so the host is redialed.
