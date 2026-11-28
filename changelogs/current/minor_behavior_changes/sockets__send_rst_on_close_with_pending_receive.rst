If ``envoy.reloadable_features.enable_send_rst_on_user_space_socket`` is
``true``, a call to ``close()`` while there is pending data in the receive
queue will generate an RST for the peer socket.
