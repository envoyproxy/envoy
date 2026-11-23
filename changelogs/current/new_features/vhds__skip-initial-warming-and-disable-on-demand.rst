Added :ref:`skip_initial_warming <envoy_v3_api_field_config.route.v3.Vhds.skip_initial_warming>` and
:ref:`disable_on_demand <envoy_v3_api_field_config.route.v3.Vhds.disable_on_demand>` to allow listener and
route configuration initialization to complete immediately while holding unmatched requests in
:ref:`envoy.filters.http.on_demand <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>` until
the initial wildcard VHDS update completes (applies to top-level ``rds`` and ``route_config``, not ``scoped_routes``).
