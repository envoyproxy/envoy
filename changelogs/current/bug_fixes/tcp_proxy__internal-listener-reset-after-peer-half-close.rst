Fixed :ref:`TCP proxy <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>` sending a reset
downstream when a tunneled upstream closed gracefully through an internal listener. A full close of a user-space
socket peer is now a normal remote close instead of a connection reset, also if end-of-stream was already read.
