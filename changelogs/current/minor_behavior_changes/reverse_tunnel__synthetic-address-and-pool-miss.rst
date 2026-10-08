The reverse connection cluster (``envoy.clusters.reverse_connection``) now backs its synthetic
upstream host address with a concrete loopback address, so
``%UPSTREAM_REMOTE_ADDRESS_WITHOUT_PORT%`` reports ``127.0.0.1`` instead of ``0.0.0.0:0`` for
reverse tunnel hosts. The upstream reverse tunnel socket interface
(``envoy.bootstrap.reverse_tunnel.upstream_socket_interface``) also renames the counter emitted
when a request finds no cached reverse tunnel from ``fallback_no_reverse_socket`` to ``pool_miss``.
