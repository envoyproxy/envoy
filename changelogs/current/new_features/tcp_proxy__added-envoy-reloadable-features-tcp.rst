Added ``envoy.reloadable_features.tcp_proxy_delay_route_selection`` to delay selecting a route until just before the upstream
connection is established. The selection moment depends on the value of
:ref:`upstream_connect_mode<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.upstream_connect_mode>`.
