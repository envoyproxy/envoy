The on_demand filter, when performing on-demand VHDS, will no longer recreate the stream after a
route configuration update successfully resolves the virtual host. Instead it refreshes the route
configuration snapshot and continues decoding the existing stream, so filters appearing before the
on_demand filter are no longer invoked twice. This mirrors the existing on-demand CDS behavior gated
by ``envoy.reloadable_features.on_demand_cluster_no_recreate_stream``. This behavior can be
temporarily reverted by setting the runtime guard
``envoy.reloadable_features.on_demand_vhds_no_recreate_stream`` to ``false``.
