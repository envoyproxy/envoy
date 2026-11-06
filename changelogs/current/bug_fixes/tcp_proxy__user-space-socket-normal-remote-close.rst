Fixed user-space sockets (internal listeners) resetting connections when the peer closes
gracefully. A full peer ``close()`` now reads as a clean EOF, so all pending data is delivered with
``end_stream`` and the half-close enabled connection then closes with a normal ``RemoteClose``
instead of a synthesized ``ECONNRESET``. This fixes spurious downstream resets when an internal
listener closes before the client half-closes, including :ref:`TCP proxy
<envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>` tunneling, TLS over internal
listeners, and HCM-terminated ``CONNECT``, while still closing orphaned half-closed peers promptly.
This behavior is guarded by ``envoy.reloadable_features.enable_send_rst_on_user_space_socket``.
In addition, a terminated ``CONNECT`` stream whose TCP upstream delivers ``end_stream`` and then
closes normally is now reset with ``NO_ERROR`` instead of ``INTERNAL_ERROR`` and no longer records
the ``UC`` response flag. This behavior can be reverted by setting
``envoy.reloadable_features.tcp_upstream_no_error_reset_after_end_stream`` to ``false``.
