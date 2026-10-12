Fixed several reverse tunnel handshake defects in the :ref:`reverse_tunnel
<config_network_filters_reverse_tunnel>` network filter. The HTTP/1 codec stats are now owned by the
filter configuration rather than a per-dispatch local, removing a use after free that a peer could
reach by pipelining handshakes. The handshake connection is now detached only after the acceptance
response reaches the wire, so the duplicated tunnel socket is its single reader. A handshake that
cannot be registered is answered with ``503`` instead of ``200`` over a dropped socket. A handshake
request that carries a body is rejected with ``400``, and a handshake that does not complete within
``handshake_timeout`` (default ``10s``) is closed. The filter now requires the upstream reverse
tunnel socket interface bootstrap extension at configuration load.
