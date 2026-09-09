The on_demand filter, when performing on-demand VHDS, will no longer recreate the stream after a
route configuration update successfully resolves the virtual host. Instead it refreshes the route
configuration snapshot and continues the filter chain. Filters appearing before the on_demand filter
will no longer be invoked twice, and a request whose downstream stream was reset while VHDS was in
flight can no longer trigger a crash while moving the buffered request body. This mirrors the
existing on-demand CDS behavior gated by
``envoy.reloadable_features.on_demand_cluster_no_recreate_stream``. This behavior can be temporarily
reverted by setting the runtime guard
``envoy.reloadable_features.on_demand_vhds_no_recreate_stream`` to ``false``.
