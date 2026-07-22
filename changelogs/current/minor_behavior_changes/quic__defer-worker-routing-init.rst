QUIC listeners now create the connection ID generator factory and apply its worker-routing socket
option in ``ActiveUdpListenerFactory::initializeWorkerRouting``. This runs after the listen sockets
are bound or duplicated after an LDS update or the hotrestart RPC. The listener init target is now
registered with the server init manager after ``initializeWorkerRouting`` instead of at the end of
listener construction, so that connection ID generator extensions can register init targets there.
This behavior can be reverted by setting runtime guard
``envoy.restart_features.defer_worker_routing_init`` to ``false``.
