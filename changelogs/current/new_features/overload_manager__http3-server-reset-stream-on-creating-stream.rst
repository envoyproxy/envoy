Added load shed point ``envoy.load_shed_points.http3_server_reset_stream_on_creating_stream``
that rejects new HTTP/3 request streams on QUIC stream creation by sending a ``RESET_STREAM``
frame with ``H3_REQUEST_REJECTED`` (``QUIC_STREAM_REQUEST_REJECTED``) before allocating the
request stream object. When a ``RESET_STREAM`` frame is sent by this load shed point, the
counter ``http3.tx_reset`` will be incremented.
