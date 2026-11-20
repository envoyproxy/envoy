The downstream reverse-tunnel initiator
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) now records a
``tunnel_setup_time`` histogram (in milliseconds) measuring how long it takes to establish
reverse tunnels to a remote host, from the first dial of an establishment episode until the
host reaches its target connection count.
