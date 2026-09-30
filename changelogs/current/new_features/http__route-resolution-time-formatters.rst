Added the ``%ROUTE_RESOLUTION_TIME_US%`` and ``%ROUTE_RESOLUTION_COUNT%`` access log :ref:`command
operators <config_access_log_command_operators>`, reporting the total wall time in microseconds a
stream spent resolving its route and how many times it resolved. Also added the
:ref:`record_route_resolution_stats
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.record_route_resolution_stats>`
option that records the ``downstream_rq_route_resolution_time_us`` and
``downstream_rq_route_resolutions`` histograms per stream when enabled.
