Fixed a race condition affecting thread-aware load balancers (for example ``RING_HASH`` and
``MAGLEV``) where, after a transient health-check failure followed by an immediate recovery, a
worker thread could snapshot a stale load balancer factory and leave the recovered host absent
from the ring/table until the next membership change. The thread-aware load balancer now rebuilds
its factory state from the priority-update callback for individual host updates, ensuring the
factory is rebuilt before the cluster manager posts the update to worker threads. During a batch
host update the cluster manager now posts the whole batch to the worker threads as a single
cross-thread update at the end of the batch, and applies it to each worker thread's priority set
as a single batch so the worker-local load balancer rebuilds once for the whole update instead of
once per priority; this is enabled by default and can be reverted by
setting ``envoy.reloadable_features.enable_batch_aware_update`` to ``false``. When
``envoy.reloadable_features.coalesce_lb_rebuilds_on_batch_update`` is also enabled, the factory
rebuild is additionally coalesced into a single end-of-batch rebuild (which still lands before the
batched post).
