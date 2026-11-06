Added load shed point ``envoy.load_shed_points.http1_server_decode_headers`` that rejects
incoming HTTP/1.x requests on stream decode headers before filter chain creation by sending a
``503`` local reply, closing the downstream connection if a request body is pending and keeping
it open for subsequent requests when the request has no body. When a request is rejected by this
load shed point, the counter ``http.<stat_prefix>.downstream_rq_overload_close`` will be
incremented.
