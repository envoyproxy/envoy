The runtime guard ``envoy.reloadable_features.coalesce_lb_rebuilds_on_batch_update`` now defaults
to ``true``. A thread-aware load balancer (for example ``RING_HASH`` or ``MAGLEV``) rebuilds its
factory state once at the end of a batch host update, from the single end-of-cycle member-update
callback, instead of once per priority from the per-priority update callback. The rebuild still
lands before the cluster manager posts the update to the worker threads, so this only removes the
redundant per-priority rebuilds of a batch. This can be reverted by setting
``envoy.reloadable_features.coalesce_lb_rebuilds_on_batch_update`` to ``false``.
