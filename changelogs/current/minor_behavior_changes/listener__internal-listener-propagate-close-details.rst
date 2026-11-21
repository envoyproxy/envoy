When an :ref:`internal listener <config_internal_listener>` closes a connection, the client side of
the connection now reports why as its transport failure reason, instead of a bare connection
termination. The reason is made of the local close reason and the upstream transport failure reason
that the internal listener's network filters recorded on their connection, e.g. why
:ref:`tcp_proxy <config_network_filters_tcp_proxy>` failed to establish its upstream connection.
See :ref:`close details <config_internal_listener_close_details>`. This behavior can be temporarily
reverted by setting runtime guard
``envoy.reloadable_features.internal_listener_propagate_close_details`` to ``false``.
