If Envoy sends an RST over a connection to/from an internal listener, the
userspace socket's IOHandle implementation will propagate the RST to the peer
handle. This behaviour change can be disabled by setting the runtime guard
``envoy.reloadable_features.enable_send_rst_on_user_space_socket`` to ``false``.
