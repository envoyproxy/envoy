The ``/stats/prometheus`` and ``/stats?format=prometheus`` admin endpoints no longer sort the stats by
name, which reduces the CPU cost of Prometheus scrapes with many stats. Each metric family is still
emitted as one group, but the order of families and series can change between scrapes. This
behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.prometheus_stats_unsorted`` to ``false``.
