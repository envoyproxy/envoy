Fixed a bug where Envoy silently dropped zero-length UDP datagrams before issuing a socket
operation. Empty datagrams are now sent through both connected and unconnected UDP sockets while
preserving their packet boundaries.
This behavior can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.udp_send_zero_length_datagrams`` to ``false``.
