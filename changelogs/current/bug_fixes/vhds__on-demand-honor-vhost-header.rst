vhds: fixed on-demand VHDS to honor :ref:`vhost_header
<envoy_v3_api_field_config.route.v3.RouteConfiguration.vhost_header>` when configured on the
route configuration instead of always subscribing using the ``:authority`` / ``Host`` header.

