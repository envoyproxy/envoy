Added ``route_extensions``, a new extension point that post-processes the route resolved by route
matching. Extensions are executed in order and the output of each is the input of the next, and may
be configured on a :ref:`route configuration
<envoy_v3_api_field_config.route.v3.RouteConfiguration.route_extensions>`, on a :ref:`virtual host
<envoy_v3_api_field_config.route.v3.VirtualHost.route_extensions>` and on a :ref:`route
<envoy_v3_api_field_config.route.v3.Route.route_extensions>`, evaluated in that order. An extension
may refine the route it is given, drop it, or generate a route of its own for a request that matched
none, so routing decisions can be customized without changing route matching.
