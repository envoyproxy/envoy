Fixed :ref:`TCP proxy <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>` sending a reset
downstream when a tunneled upstream closed gracefully through an internal listener. A user-space socket now
reads the peer's end-of-stream before reporting ``ECONNRESET``.
