Updated :ref:`skip_initial_warming <envoy_v3_api_field_config.route.v3.Vhds.skip_initial_warming>` so
requests paused by :ref:`envoy.filters.http.on_demand <envoy_v3_api_msg_extensions.filters.http.on_demand.v3.OnDemand>`
wait for the in-flight initial VHDS update before issuing per-domain on-demand VHDS requests, and fixed
stale route snapshot refresh and trailer pausing in the ``on_demand`` HTTP filter.
