The downstream reverse tunnel socket interface
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) now validates ``rc://`` initiator
listener addresses when they are resolved. The per-host connection count must be within ``[1,
1024]``, and each identifier (source node, cluster, and tenant ids and the remote cluster name) must
be at most 255 bytes and a valid HTTP header value. An ``rc://`` address with a count of ``0`` or
above ``1024``, or an identifier that is too long or not a valid header value, now fails to load
where it was previously accepted. An EnvoyInternal remote cluster is also rejected, since the
accepted tunnel cannot be duplicated from the user-space handle.
