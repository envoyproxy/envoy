Added stats under ``http.<stat_prefix>.on_demand.`` to the :ref:`on demand filter
<config_http_filters_on_demand>` counting route and cluster discoveries by outcome, in-flight and
cancelled discoveries and the duration of successful ones. The filter also stores the host, virtual host,
route configuration, cluster, outcome and duration of the discoveries in the request's filter state
``envoy.filters.http.on_demand.discovery``, see :ref:`filter state
<config_http_filters_on_demand_filter_state>`, for use in access logs.
