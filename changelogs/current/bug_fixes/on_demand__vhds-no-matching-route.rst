Fixed the on_demand filter waiting forever, or requesting the same virtual host in a loop, when
on-demand VHDS delivers the requested virtual host but none of its routes match the request. The
request now continues to the router, which replies with a 404. This behavior can be temporarily
reverted by setting the runtime guard
``envoy.reloadable_features.on_demand_vhds_require_route_match`` to ``false``.
