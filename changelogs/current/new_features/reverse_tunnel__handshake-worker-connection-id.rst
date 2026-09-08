The downstream reverse-tunnel initiator
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) now includes two additional
identifiers in the HTTP handshake it sends to the acceptor: ``x-envoy-reverse-tunnel-worker-id``
(the initiator worker dispatcher name, e.g. ``worker_2``) and ``x-envoy-reverse-tunnel-connection-id``
(the initiator's per-connection id). Both are surfaced in the initiator access log via the new
``worker_id`` and ``connection_id`` fields of the ``envoy.reverse_tunnel.initiator`` dynamic metadata
namespace. The upstream acceptor
(``envoy.bootstrap.reverse_tunnel.upstream_socket_interface``) now parses these headers and exposes
them on every reverse-tunnel lifecycle event as the ``initiator_worker_id`` and
``initiator_connection_id`` fields of the ``envoy.reverse_tunnel.lifecycle`` dynamic metadata
namespace and as the ``envoy.reverse_tunnel.initiator_worker_id`` /
``envoy.reverse_tunnel.initiator_connection_id`` connection filter-state keys. Together these let
tunnels originating from different workers/connections of the same initiator be told apart and
correlated across both ends.
