Envoy will now batch worker thread-local cluster additions, updates, and removals during CDS, EDS,
and static bootstrap configuration ingestion, consolidating cross-thread dispatching across all
cluster changes within an update cycle into a single thread-local broadcast per worker thread.
This can be reverted by setting ``envoy.reloadable_features.batch_cluster_updates`` to ``false``.
