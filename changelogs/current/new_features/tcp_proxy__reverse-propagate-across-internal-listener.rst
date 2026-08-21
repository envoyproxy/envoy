when :ref:`propagate_response_headers
<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TunnelingConfig.propagate_response_headers>`
or :ref:`propagate_response_trailers
<envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.TunnelingConfig.propagate_response_trailers>`
is enabled and the tcp_proxy sits on the inner side of an internal-listener boundary, the
captured response headers/trailers filter state object is now additionally reverse-propagated
to the outer (downstream) connection's filter state at upstream close. A pre-existing object
of the same name is overwritten (last-writer-wins), consistent with forward propagation.
Refs #43977.
