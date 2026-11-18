Fixed a race condition affecting thread-aware load balancers (for example ``RING_HASH`` and
``MAGLEV``) when ``coalesce_lb_rebuilds_on_batch_update`` is enabled. The load balancer factory is
now rebuilt from the priority-update callback before the cluster manager posts the host update to
worker threads, preventing workers from using stale ring or table state.
