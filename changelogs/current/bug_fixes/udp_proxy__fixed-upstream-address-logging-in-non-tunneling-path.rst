Fixed the :ref:`UDP proxy <config_udp_listener_filters_udp_proxy>` not populating the upstream
local and remote addresses on the session stream info. As a result the ``%UPSTREAM_LOCAL_ADDRESS%``
and ``%UPSTREAM_REMOTE_ADDRESS%`` (and their ``_WITHOUT_PORT`` / port variants) access log
formatters were always empty for non-tunneling UDP -> UDP sessions, unlike TCP proxy. The upstream
remote address is now recorded when the upstream host is selected, and the upstream local address
once the socket is bound after the first datagram is sent upstream.
