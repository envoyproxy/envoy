QUIC listeners now create the connection ID generator factory and apply its worker-routing socket
option when the socket factory config is loaded, instead of when the listener config is loaded.
This behavior can be reverted by setting runtime guard
``envoy.restart_features.defer_worker_routing_init`` to ``false``.
