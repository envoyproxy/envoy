A host health change now re-partitions only the priority that holds the host and moves only that
host between the healthy, degraded and excluded host lists, instead of rebuilding every priority. A
host that enters one of these lists is appended to its end rather than keeping its position from the
cluster's host list. EDS clusters are unchanged. This behavior can be reverted by setting the runtime
guard ``envoy.reloadable_features.incremental_health_transition`` to ``false``.
