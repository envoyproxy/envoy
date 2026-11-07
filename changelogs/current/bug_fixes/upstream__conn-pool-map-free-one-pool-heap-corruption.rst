Fixed a heap-corruption crash that occurs when the ``max_connection_pools`` circuit breaker evicts
connection pools. ``ConnPoolMap::freeOnePool()`` destroyed the victim pool inline while erasing its
map slot. ``hasActiveConnections()`` does not account for connecting or idle keepalive clients, so
the victim could still own clients; their destructors re-enter
``ConnPoolImplBase::checkForIdleAndNotify()``, whose still-registered idle callback calls back into
the map (``httpConnPoolIsIdle`` -> ``erasePool``) while the outer erase is mid-flight and destroys
the same slot a second time. The resulting double free corrupts the allocator and later surfaces as
random worker SIGSEGV under per-destination pool churn (for example direct-IP forwarding clusters
with one pool per target). ``freeOnePool()`` now deferred-deletes the evicted pool exactly like
``erasePool()`` does, and prefers fully idle victims so the common eviction path stays quiet.
In addition, ``checkForIdleAndNotify()`` no longer fires idle callbacks once the pool is pending
deferred deletion: at that point the pool has already been removed from its map, and firing with
the captured key could erase a successor pool that re-registered the same key.
