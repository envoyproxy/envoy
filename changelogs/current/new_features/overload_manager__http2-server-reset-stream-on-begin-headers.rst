Added load shed point ``envoy.load_shed_points.http2_server_reset_stream_on_begin_headers`` that
rejects new HTTP/2 streams at the codec level on the initial header frame by sending a
``RST_STREAM(REFUSED_STREAM)`` before allocating stream or header objects. When a ``RST_STREAM``
frame is submitted by this load shed point, the counter ``http2.stream_refused_errors`` will be
incremented.
