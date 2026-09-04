Fixed a stream leak in :ref:`TCP proxy <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>`
configured with
:ref:`tunneling_config <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.tunneling_config>`
routed through an :ref:`internal listener <envoy_v3_api_field_config.listener.v3.Listener.internal_listener>`.
A full peer close on a user-space socket surfaced as a plain end-of-stream, which a half-close-enabled
connection treats as a read half-close, so the upstream tunnel (e.g. HTTP CONNECT) stayed pinned until the
upstream idle timeout. A full peer close is now reported the way the kernel reports a reset: reads drain
pending data first and then return ``ECONNRESET``. A peer ``shutdown(WR)`` half-close is unchanged and real
OS sockets are unaffected. Guarded by runtime feature
``envoy.reloadable_features.internal_listener_peer_destroyed_propagation``.
