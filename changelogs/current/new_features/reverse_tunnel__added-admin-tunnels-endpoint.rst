Added a ``/reverse_tunnel/tunnels`` admin endpoint to the downstream reverse-tunnel initiator
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``). It lists the active
reverse tunnels as a ``text/plain`` body, with one ``<node>:<cluster>:<tenant>: <count>``
line per source identifier.
