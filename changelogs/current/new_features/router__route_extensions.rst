Added :ref:`route extensions
<envoy_v3_api_field_config.route.v3.RouteConfiguration.route_extensions>`, an extension point at the
route configuration, virtual host and route levels that runs a chain over the route resolved for a
request. Each extension receives the route the previous one returned and can replace it, produce one
when nothing matched, or remove it. Clusters an extension references are validated against the
cluster manager at configuration load when :ref:`validate_clusters
<envoy_v3_api_field_config.route.v3.RouteConfiguration.validate_clusters>` is enabled.
