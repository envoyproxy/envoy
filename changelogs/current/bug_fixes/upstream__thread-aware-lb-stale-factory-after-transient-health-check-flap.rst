Fixed a race condition affecting thread-aware load balancers (for example ``RING_HASH`` and
``MAGLEV``) where, after a transient health-check failure followed by an immediate recovery, a
worker thread could snapshot a stale load balancer factory and leave the recovered host absent
from the ring/table until the next membership change. The thread-aware load balancer now rebuilds
its factory state before the cluster manager posts the corresponding host update to the worker
threads, so a worker can no longer snapshot a stale factory. When
``envoy.reloadable_features.enable_batch_aware_update`` is enabled (the default), the cluster
manager accumulates per-priority host updates and posts them to the worker threads from the single
end-of-cycle member-update callback (once for a whole batch host update, once after each individual
update), instead of posting once per priority; mergeable health-check/weight/metadata updates still
flow through the update merge window. The accumulated update is applied to each worker thread's
priority set as a single batch so the worker-local load balancer rebuilds once for the whole update
instead of once per priority. This can be reverted by setting
``envoy.reloadable_features.enable_batch_aware_update`` to ``false``. The thread-aware load
balancer rebuilds its factory from the priority-update callback; when
``envoy.reloadable_features.coalesce_lb_rebuilds_on_batch_update`` is also enabled it instead
defers the rebuild to the single end-of-cycle member-update callback, coalescing the per-priority
rebuilds of a batch into one (which still lands before the batched post).
