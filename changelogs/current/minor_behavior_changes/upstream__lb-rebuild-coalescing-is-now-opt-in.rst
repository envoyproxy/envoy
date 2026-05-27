Load balancer rebuild coalescing during EDS batch host updates is now opt-in. It was previously enabled
by default. It can be re-enabled by setting the runtime guard
``envoy.reloadable_features.coalesce_lb_rebuilds_on_batch_update`` to ``true``.
