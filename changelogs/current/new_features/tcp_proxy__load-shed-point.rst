Added load shed point ``envoy.load_shed_points.tcp_proxy_on_data`` to close downstream TCP connections
when receiving data under resource pressure, and added the ``downstream_cx_overload_close`` counter stat
to the TCP proxy filter to track connections closed by load shedding.
