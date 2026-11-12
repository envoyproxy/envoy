Fixed :ref:`tcp_proxy <config_network_filters_tcp_proxy>` in ``upstream_connect_mode: ON_DOWNSTREAM_DATA``
leaving a downstream connection open until ``idle_timeout`` when the client half-closed without
sending any data. Envoy now closes such a connection at once and records
``tcp_proxy_downstream_closed_without_data`` as its downstream local close reason, shown by
``%DOWNSTREAM_LOCAL_CLOSE_REASON%``, and counts it in ``downstream_cx_closed_without_data``. These
connections no longer reach ``idle_timeout``. Other connect modes are unchanged.
