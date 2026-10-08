The downstream reverse tunnel socket interface
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) now validates ``rc://`` initiator
listener addresses when they are resolved. The per-host connection count must be within ``[1,
1024]``, and each identifier (source node, cluster, and tenant ids and the remote cluster name) must
be at most 255 bytes and a valid HTTP header value. An ``rc://`` address with a count of ``0`` or
above ``1024``, or an identifier that is too long or not a valid header value, now fails to load
where it was previously accepted. Separately, a reverse connection to a remote cluster host that
resolves to an ``EnvoyInternal`` address is now rejected before dialing, because the user-space I/O
handle cannot be duplicated for the accepted tunnel.
